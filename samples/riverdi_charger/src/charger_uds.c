/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <ardep/iso14229.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/app_version.h>
#include "charger_uds.h"

#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#endif

#define DID_BATTERY 0xf200
#define DID_PROFILE 0xf201
#define DID_TRUCK 0xf202
#define DID_INDICATORS 0xf203
#define DID_CHARGE 0xf204
#define DID_LAYOUT 0xf205
#define DID_VERSION 0xf189
#define BATTERY_WIRE_SIZE 12
#define BENCH_P2_MS 5

static const struct device *const can = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static struct iso14229_zephyr_instance uds;
static struct battery_sample battery = {640, 520, 0, 250, 0, 0};
static int64_t last_update = -1;
static atomic_t selected_truck;
static atomic_t indicators;
static atomic_t charge_state;
K_MUTEX_DEFINE(battery_lock);

bool charger_uds_sample(struct battery_sample *sample)
{
	k_mutex_lock(&battery_lock, K_FOREVER);
	*sample = battery;
	bool fresh = last_update >= 0 && k_uptime_get() - last_update < 5000;
	k_mutex_unlock(&battery_lock);
	return fresh;
}

enum charger_truck charger_uds_truck(void)
{
	return atomic_get(&selected_truck);
}

uint8_t charger_uds_indicators(void)
{
	return atomic_get(&indicators);
}

enum charger_charge_state charger_charge_state(void)
{
	return atomic_get(&charge_state);
}

int charger_set_charging(bool enable)
{
	int ret = 0;

	k_mutex_lock(&battery_lock, K_FOREVER);
	if (enable) {
		if (!charger_uds_truck() || charger_charge_state() == COMPLETE) {
			ret = -EPERM;
		} else {
			atomic_set(&charge_state, CHARGING);
		}
	} else {
		atomic_cas(&charge_state, CHARGING, PAUSED);
	}
	k_mutex_unlock(&battery_lock);
	return ret;
}

void charger_reset_charge(void)
{
	k_mutex_lock(&battery_lock, K_FOREVER);
	atomic_set(&charge_state, READY);
	k_mutex_unlock(&battery_lock);
}

static UDSErr_t read_did(UDSServer_t *srv, UDSRDBIArgs_t *args)
{
	static const uint8_t profile[] = {
		'R', 'V', 'C', '1',
#ifdef CONFIG_BOOTLOADER_MCUBOOT
		1,
#else
		0,
#endif
	};
	static const char version[] = APP_VERSION_STRING;
	struct battery_sample sample;
	uint8_t wire[BATTERY_WIRE_SIZE];

	switch (args->dataId) {
	case DID_PROFILE:
		return args->copy(srv, profile, sizeof(profile));
	case DID_VERSION:
		return args->copy(srv, version, sizeof(version) - 1);
	case DID_TRUCK:
		wire[0] = charger_uds_truck();
		return args->copy(srv, wire, 1);
	case DID_INDICATORS:
		wire[0] = charger_uds_indicators();
		return args->copy(srv, wire, 1);
	case DID_CHARGE:
		wire[0] = charger_charge_state();
		return args->copy(srv, wire, 1);
	case DID_LAYOUT:
#ifdef CONFIG_BOARD_STM32F769I_DISCO
		wire[0] = 1;
#else
		wire[0] = 0;
#endif
		return args->copy(srv, wire, 1);
	case DID_BATTERY:
		charger_uds_sample(&sample);
		sys_put_be16(sample.soc_permille, wire);
		sys_put_be16(sample.voltage_decivolts, wire + 2);
		sys_put_be16(sample.current_deciamps, wire + 4);
		sys_put_be16(sample.temperature_decicelsius, wire + 6);
		sys_put_be16(sample.warnings, wire + 8);
		sys_put_be16(sample.faults, wire + 10);
		return args->copy(srv, wire, sizeof(wire));
	default:
		return UDS_NRC_RequestOutOfRange;
	}
}

static UDSErr_t write_did(const UDSWDBIArgs_t *args)
{
	if (args->dataId == DID_CHARGE) {
		if (args->len != 1) {
			return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
		}
		if (args->data[0] > 1) {
			return UDS_NRC_RequestOutOfRange;
		}
		return charger_set_charging(args->data[0]) ? UDS_NRC_ConditionsNotCorrect :
			UDS_PositiveResponse;
	}
	if (args->dataId == DID_INDICATORS) {
		if (args->len != 2) {
			return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
		}
		const uint8_t mask = args->data[0];
		const uint8_t value = args->data[1];
		atomic_val_t previous;

		if (!mask || (mask & ~(CHARGER_POWER_STAGE | CHARGER_PM_ENABLE)) || (value & ~mask)) {
			return UDS_NRC_RequestOutOfRange;
		}
		do {
			previous = atomic_get(&indicators);
		} while (!atomic_cas(&indicators, previous, (previous & ~mask) | value));
		return UDS_PositiveResponse;
	}
	if (args->dataId == DID_TRUCK) {
		if (args->len != 1) {
			return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
		}
		if (args->data[0] >= CHARGER_TRUCK_COUNT) {
			return UDS_NRC_RequestOutOfRange;
		}
		k_mutex_lock(&battery_lock, K_FOREVER);
		if (atomic_set(&selected_truck, args->data[0]) != args->data[0]) {
			atomic_set(&charge_state, READY);
			last_update = -1;
		}
		k_mutex_unlock(&battery_lock);
		return UDS_PositiveResponse;
	}
	if (args->dataId != DID_BATTERY) {
		return UDS_NRC_RequestOutOfRange;
	}
	if (args->len != BATTERY_WIRE_SIZE) {
		return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
	}

	struct battery_sample sample = {
		.soc_permille = sys_get_be16(args->data),
		.voltage_decivolts = sys_get_be16(args->data + 2),
		.current_deciamps = (int16_t)sys_get_be16(args->data + 4),
		.temperature_decicelsius = (int16_t)sys_get_be16(args->data + 6),
		.warnings = sys_get_be16(args->data + 8),
		.faults = sys_get_be16(args->data + 10),
	};

	if (sample.soc_permille > 1000 || sample.voltage_decivolts > 1200 ||
	    sample.current_deciamps < -3000 || sample.current_deciamps > 3000 ||
	    sample.temperature_decicelsius < -400 || sample.temperature_decicelsius > 1500) {
		return UDS_NRC_RequestOutOfRange;
	}
	k_mutex_lock(&battery_lock, K_FOREVER);
	battery = sample;
	last_update = k_uptime_get();
	if (sample.soc_permille == 1000) {
		atomic_cas(&charge_state, CHARGING, COMPLETE);
	} else {
		atomic_cas(&charge_state, COMPLETE, READY);
	}
	k_mutex_unlock(&battery_lock);
	return UDS_PositiveResponse;
}

#ifdef CONFIG_BOOTLOADER_MCUBOOT
#define ERASE_SECONDARY 0xff00
#define VERIFY_AND_STAGE 0xff01
#define SECONDARY_ID PARTITION_ID(slot1_partition)
/* Leave a complete sector after the image data for swap metadata. */
#define MAX_IMAGE_SIZE (PARTITION_SIZE(slot1_partition) - 0x20000)
#define TRANSFER_PAYLOAD 512

BUILD_ASSERT(IS_ENABLED(CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_SCRATCH),
	     "Riverdi UDS requires MCUboot swap using scratch");

static struct flash_img_context image;
static enum { UPDATE_EMPTY, UPDATE_ERASED, UPDATE_DOWNLOADING, UPDATE_DOWNLOADED, UPDATE_STAGED } phase;
static size_t image_size;
static size_t received;

static void abandon_download(void)
{
	if (phase == UPDATE_DOWNLOADING && image.flash_area != NULL) {
		flash_area_close(image.flash_area);
		image.flash_area = NULL;
	}
	phase = UPDATE_EMPTY;
}

static UDSErr_t programming_routine(const UDSRoutineCtrlArgs_t *args)
{
	const struct flash_area *slot;
	uint8_t buffer[256];
	int rc;

	if (args->ctrlType != 1) {
		return UDS_NRC_SubFunctionNotSupported;
	}
	if (args->id == ERASE_SECONDARY && args->len == 0 && phase != UPDATE_STAGED) {
		abandon_download();
		if (flash_area_open(SECONDARY_ID, &slot)) {
			return UDS_NRC_GeneralProgrammingFailure;
		}
		rc = flash_area_erase(slot, 0, slot->fa_size);
		flash_area_close(slot);
		if (rc) {
			return UDS_NRC_GeneralProgrammingFailure;
		}
		phase = UPDATE_ERASED;
		return UDS_PositiveResponse;
	}
	if (args->id != VERIFY_AND_STAGE || args->len != 4 || phase != UPDATE_DOWNLOADED) {
		return UDS_NRC_RequestSequenceError;
	}
	if (flash_area_open(SECONDARY_ID, &slot)) {
		return UDS_NRC_GeneralProgrammingFailure;
	}
	uint32_t crc = 0;
	for (size_t offset = 0; offset < image_size; offset += sizeof(buffer)) {
		size_t length = MIN(sizeof(buffer), image_size - offset);
		rc = flash_area_read(slot, offset, buffer, length);
		if (rc) {
			flash_area_close(slot);
			return UDS_NRC_GeneralProgrammingFailure;
		}
		crc = crc32_ieee_update(crc, buffer, length);
	}
	flash_area_close(slot);
	if (crc != sys_get_be32(args->optionRecord) || boot_request_upgrade(BOOT_UPGRADE_TEST)) {
		return UDS_NRC_GeneralProgrammingFailure;
	}
	phase = UPDATE_STAGED;
	return UDS_PositiveResponse;
}

static UDSErr_t programming_event(UDSEvent_t event, void *arg)
{
	switch (event) {
	case UDS_EVT_RoutineCtrl:
		return programming_routine(arg);
	case UDS_EVT_RequestDownload: {
		UDSRequestDownloadArgs_t *args = arg;
		if (phase != UPDATE_ERASED || args->addr != NULL || args->dataFormatIdentifier != 0 ||
		    args->size == 0 || args->size > MAX_IMAGE_SIZE) {
			return UDS_NRC_RequestOutOfRange;
		}
		if (flash_img_init_id(&image, SECONDARY_ID)) {
			return UDS_NRC_GeneralProgrammingFailure;
		}
		phase = UPDATE_DOWNLOADING;
		image_size = args->size;
		received = 0;
		args->maxNumberOfBlockLength = TRANSFER_PAYLOAD + 2;
		return UDS_PositiveResponse;
	}
	case UDS_EVT_TransferData: {
		const UDSTransferDataArgs_t *args = arg;
		if (phase != UPDATE_DOWNLOADING || args->len == 0 || args->len > TRANSFER_PAYLOAD ||
		    args->len > image_size - received) {
			return UDS_NRC_RequestSequenceError;
		}
		if (flash_img_buffered_write(&image, args->data, args->len, false)) {
			abandon_download();
			return UDS_NRC_GeneralProgrammingFailure;
		}
		received += args->len;
		return UDS_PositiveResponse;
	}
	case UDS_EVT_RequestTransferExit:
		if (phase != UPDATE_DOWNLOADING || received != image_size ||
		    ((UDSRequestTransferExitArgs_t *)arg)->len != 0) {
			return UDS_NRC_RequestSequenceError;
		}
		if (flash_img_buffered_write(&image, NULL, 0, true)) {
			abandon_download();
			return UDS_NRC_GeneralProgrammingFailure;
		}
		phase = UPDATE_DOWNLOADED;
		return UDS_PositiveResponse;
	case UDS_EVT_EcuReset:
		return phase == UPDATE_STAGED && ((UDSECUResetArgs_t *)arg)->type == 1 ?
			UDS_PositiveResponse : UDS_NRC_ConditionsNotCorrect;
	default:
		return UDS_NRC_ServiceNotSupported;
	}
}
#endif

static UDSErr_t uds_event(struct iso14229_zephyr_instance *inst, UDSEvent_t event,
			  void *arg, void *context)
{
	switch (event) {
	case UDS_EVT_ReadDataByIdent:
		return read_did(&inst->server, arg);
	case UDS_EVT_WriteDataByIdent:
		return inst->server.sessionType == 1 ? write_did(arg) : UDS_NRC_ConditionsNotCorrect;
	case UDS_EVT_DiagSessCtrl: {
		UDSDiagSessCtrlArgs_t *args = arg;
		if (args->type == 1) {
			inst->server.p2_ms = BENCH_P2_MS;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
			if (phase != UPDATE_STAGED) {
				abandon_download();
				inst->server.xferIsActive = false;
			}
#endif
			return UDS_PositiveResponse;
		}
#ifdef CONFIG_BOOTLOADER_MCUBOOT
		if (args->type == 2 && boot_is_img_confirmed()) {
			inst->server.p2_ms = UDS_SERVER_DEFAULT_P2_MS;
			inst->server.s3_ms = 30000;
			args->p2_ms = 25000;
			args->p2_star_ms = 30000;
			return UDS_PositiveResponse;
		}
#endif
		return UDS_NRC_SubFunctionNotSupported;
	}
	case UDS_EVT_SessionTimeout:
		inst->server.p2_ms = BENCH_P2_MS;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
		if (phase != UPDATE_STAGED) {
			abandon_download();
			inst->server.xferIsActive = false;
		}
#endif
		return UDS_PositiveResponse;
#ifdef CONFIG_BOOTLOADER_MCUBOOT
	case UDS_EVT_DoScheduledReset:
		sys_reboot(SYS_REBOOT_COLD);
		return UDS_PositiveResponse;
#endif
	default:
#ifdef CONFIG_BOOTLOADER_MCUBOOT
		if (inst->server.sessionType == 2) {
			return programming_event(event, arg);
		}
#endif
		return UDS_NRC_ServiceNotSupported;
	}
}

int charger_uds_init(void)
{
	const UDSISOTpCConfig_t address = {
		/* ARDEP filters on source_addr and transmits on target_addr. */
		.source_addr = 0x7e0, .target_addr = 0x7e8,
		.source_addr_func = UDS_TP_NOOP_ADDR, .target_addr_func = UDS_TP_NOOP_ADDR,
	};
	int rc;

	if (!device_is_ready(can)) {
		return -ENODEV;
	}
	rc = iso14229_zephyr_init(&uds, &address, can, NULL);
	if (rc == 0) {
		uds.server.p2_ms = BENCH_P2_MS;
		rc = uds.set_callback(&uds, uds_event);
	}
	if (rc == 0) {
		rc = can_start(can);
	}
	if (rc == 0) {
		rc = uds.thread_start(&uds);
	}
	return rc;
}
