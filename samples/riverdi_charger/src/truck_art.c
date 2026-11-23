/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include "truck_art.h"

#define ART_WIDTH 152
#define ART_HEIGHT 136
#define PALETTE_BYTES 32

static const uint8_t ejc216[] = {
#include <ejc216.pal565.inc>
};
static const uint8_t efg216[] = {
#include <efg216.pal565.inc>
};
static const uint8_t etv216i[] = {
#include <etv216i.pal565.inc>
};
static const uint8_t efg425[] = {
#include <efg425.pal565.inc>
};

/* Expand only the selected artwork into one LVGL RGB565 buffer. */
static uint16_t pixels[ART_WIDTH * ART_HEIGHT] __aligned(4);
static const lv_image_dsc_t image = {
	.header = {
		.magic = LV_IMAGE_HEADER_MAGIC,
		.cf = LV_COLOR_FORMAT_RGB565,
		.w = ART_WIDTH,
		.h = ART_HEIGHT,
		.stride = ART_WIDTH * sizeof(uint16_t),
	},
	.data_size = sizeof(pixels),
	.data = (const uint8_t *)pixels,
};

const lv_image_dsc_t *truck_art(enum charger_truck truck)
{
	static const uint8_t *const images[CHARGER_TRUCK_COUNT] = {
		[CHARGER_TRUCK_EJC216] = ejc216,
		[CHARGER_TRUCK_EFG216] = efg216,
		[CHARGER_TRUCK_ETV216I] = etv216i,
		[CHARGER_TRUCK_EFG425] = efg425,
		[CHARGER_TRUCK_CUSTOM] = efg216,
	};
	const uint8_t *data = images[truck];

	if (data == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < ARRAY_SIZE(pixels) / 2; i++) {
		const uint8_t pair = data[PALETTE_BYTES + i];

		pixels[2 * i] = sys_get_le16(&data[2 * (pair >> 4)]);
		pixels[2 * i + 1] = sys_get_le16(&data[2 * (pair & 0x0f)]);
	}
	return &image;
}
