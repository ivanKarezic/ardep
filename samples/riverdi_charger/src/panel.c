/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <lvgl.h>
#include <soc.h>
#include <stm32h7xx_hal_dsi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mipi_dsi.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include "panel.h"

#define CHARGER_IO DT_NODELABEL(charger_io)
#define PANEL_WIDTH 1024
#define PANEL_HEIGHT 600
#define TOUCH_ADDRESS 0x41
#define TOUCH_REPORT 0x48
#define TOUCH_ACTIVE BIT(6)
#define TOUCH_CONTACT_SIZE 5
#define TOUCH_COUNT_OFFSET 61
#define RIVERDI_DSI_PHY_THS_ZERO 213U

static const struct gpio_dt_spec panel_reset = GPIO_DT_SPEC_GET(CHARGER_IO, panel_reset_gpios);
static const struct pwm_dt_spec backlight = PWM_DT_SPEC_GET(CHARGER_IO);
static const struct gpio_dt_spec touch_reset = GPIO_DT_SPEC_GET(CHARGER_IO, touch_reset_gpios);
static const struct gpio_dt_spec touch_irq = GPIO_DT_SPEC_GET(CHARGER_IO, touch_int_gpios);
static const struct device *const dsi = DEVICE_DT_GET(DT_NODELABEL(mipi_dsi));
static const struct device *const touch_bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static struct gpio_callback touch_callback;
static lv_indev_data_t last_touch;
K_SEM_DEFINE(touch_pending, 0, 1);

static int reset_device(const struct gpio_dt_spec *reset, int hold_ms, int settle_ms)
{
	int ret = gpio_pin_configure_dt(reset, GPIO_OUTPUT_ACTIVE);

	if (ret < 0) {
		return ret;
	}
	k_msleep(hold_ms);
	ret = gpio_pin_set_dt(reset, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(settle_ms);
	return 0;
}

static int apply_riverdi_dsi_phy(void)
{
	DSI_HandleTypeDef host = { .Instance = DSI };

	if (HAL_DSI_Stop(&host) != HAL_OK) {
		return -EIO;
	}
	CLEAR_BIT(DSI->VNPCR, DSI_VNPCR_NPSIZE);
	CLEAR_BIT(DSI->LPMCR, DSI_LPMCR_VLPSIZE);
	if (HAL_DSI_SetPHYTimings(&host, DSI_THS_ZERO, ENABLE,
				  RIVERDI_DSI_PHY_THS_ZERO) != HAL_OK ||
	    HAL_DSI_Start(&host) != HAL_OK) {
		return -EIO;
	}
	return 0;
}

int riverdi_panel_init(void)
{
	/* The STM32 host fetches a 16-bit value at offset one for this packet. */
	static const uint8_t panel_setup[][3] __aligned(2) = {
		{0x87, 0x5a}, {0xb0, 0x80}, {0xb2, 0x50}, {0x80, 0x4b},
		{0x81, 0xff}, {0x82, 0x1a}, {0x83, 0x88}, {0x84, 0x8f},
		{0x85, 0x35}, {0x86, 0xb0},
	};
	const struct mipi_dsi_device panel = {
		.data_lanes = 2,
		.pixfmt = MIPI_DSI_PIXFMT_RGB888,
		.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM,
		.timings = {
			.hactive = PANEL_WIDTH, .hsync = 70, .hbp = 160, .hfp = 160,
			.vactive = PANEL_HEIGHT, .vsync = 10, .vbp = 23, .vfp = 12,
		},
	};
	int ret;

	if (!device_is_ready(dsi) || !gpio_is_ready_dt(&panel_reset) ||
	    !pwm_is_ready_dt(&backlight)) {
		return -ENODEV;
	}
	ret = pwm_set_pulse_dt(&backlight, 0);
	if (ret < 0) {
		return ret;
	}
	ret = reset_device(&panel_reset, 20, 120);
	if (ret < 0) {
		return ret;
	}
	ret = mipi_dsi_attach(dsi, 0, &panel);
	if (ret < 0) {
		return ret;
	}
	ret = apply_riverdi_dsi_phy();
	if (ret < 0) {
		return ret;
	}
	ret = mipi_dsi_dcs_write(dsi, 0, MIPI_DCS_SOFT_RESET, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(120);

	for (size_t i = 0; i < ARRAY_SIZE(panel_setup); i++) {
		ret = mipi_dsi_generic_write(dsi, 0, panel_setup[i], 2);
		if (ret < 0) {
			return ret;
		}
	}
	k_msleep(50);
	ret = mipi_dsi_dcs_write(dsi, 0, MIPI_DCS_EXIT_SLEEP_MODE, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(120);
	ret = mipi_dsi_dcs_write(dsi, 0, MIPI_DCS_SET_DISPLAY_ON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(120);
	return pwm_set_cycles(backlight.dev, backlight.channel, 1000, 998, backlight.flags);
}

static void touch_interrupt(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_sem_give(&touch_pending);
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
	uint8_t report[64];

	if (k_sem_take(&touch_pending, K_NO_WAIT) == 0) {
		int ret = i2c_read(touch_bus, report, sizeof(report), TOUCH_ADDRESS);

		last_touch.state = LV_INDEV_STATE_RELEASED;
		if (ret == 0 && report[0] == TOUCH_REPORT) {
			for (size_t i = 0; i < MIN(report[TOUCH_COUNT_OFFSET], 10); i++) {
				const uint8_t *contact = &report[1 + i * TOUCH_CONTACT_SIZE];
				uint16_t x = sys_get_le16(contact + 1);
				uint16_t y = sys_get_le16(contact + 3);

				if ((contact[0] & TOUCH_ACTIVE) && x < PANEL_WIDTH &&
				    y < PANEL_HEIGHT) {
					last_touch.point.x = x;
					last_touch.point.y = y;
					last_touch.state = LV_INDEV_STATE_PRESSED;
					break;
				}
			}
		}
	}
	data->point = last_touch.point;
	data->state = last_touch.state;
}

int riverdi_touch_init(void)
{
	lv_indev_t *pointer;
	int ret;

	if (!device_is_ready(touch_bus) || !gpio_is_ready_dt(&touch_reset) ||
	    !gpio_is_ready_dt(&touch_irq)) {
		return -ENODEV;
	}
	ret = reset_device(&touch_reset, 100, 1000);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_configure_dt(&touch_irq, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}
	gpio_init_callback(&touch_callback, touch_interrupt, BIT(touch_irq.pin));
	ret = gpio_add_callback(touch_irq.port, &touch_callback);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&touch_irq, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	pointer = lv_indev_create();
	if (pointer == NULL) {
		return -ENOMEM;
	}
	lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(pointer, read_touch);
	return 0;
}
