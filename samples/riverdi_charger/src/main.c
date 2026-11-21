/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "panel.h"
#include "charger_uds.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#define INK 0x171a1e
#define MUTED 0x58616c
#define WHITE 0xffffff
#define SURFACE 0xf2f4f5
#define LINE 0xdce1e5
#define YELLOW 0xffc800

static const uint8_t logo_pixels[] __aligned(4) = {
#include <jungheinrich.rgb565.inc>
};

static const lv_image_dsc_t logo = {
	.header = {
		.magic = LV_IMAGE_HEADER_MAGIC,
		.cf = LV_COLOR_FORMAT_RGB565,
		.w = 256,
		.h = 39,
		.stride = 512,
	},
	.data_size = sizeof(logo_pixels),
	.data = logo_pixels,
};

enum charge_state {
	READY,
	CHARGING,
	PAUSED,
	COMPLETE,
};

static struct {
	enum charge_state state;
	unsigned int percent;
	lv_obj_t *status;
	lv_obj_t *percent_text;
	lv_obj_t *progress;
	lv_obj_t *voltage;
	lv_obj_t *current;
	lv_obj_t *remaining;
	lv_obj_t *action;
} charger = { .percent = 64 };

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height,
		      uint32_t color, int radius)
{
	lv_obj_t *obj = lv_obj_create(parent);

	lv_obj_remove_style_all(obj);
	lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, height);
	lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, radius, 0);
	return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
		       const lv_font_t *font, uint32_t color)
{
	lv_obj_t *obj = lv_label_create(parent);

	lv_label_set_text(obj, text);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
	return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int width, bool primary,
			lv_event_cb_t callback)
{
	lv_obj_t *obj = lv_button_create(parent);
	lv_obj_t *caption;

	lv_obj_set_pos(obj, x, 514);
	lv_obj_set_size(obj, width, 60);
	lv_obj_set_style_radius(obj, 10, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? YELLOW : WHITE), 0);
	lv_obj_set_style_border_color(obj, lv_color_hex(LINE), 0);
	lv_obj_set_style_border_width(obj, primary ? 0 : 1, 0);
	lv_obj_set_style_shadow_width(obj, 0, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(primary ? 0xe6b400 : LINE), LV_STATE_PRESSED);
	lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
	caption = label(obj, text, 0, 0, &lv_font_montserrat_20, INK);
	lv_obj_center(caption);
	return caption;
}

static void update_display(void)
{
	static const char *const status[] = {
		[READY] = "Ready to charge", [CHARGING] = "Charging",
		[PAUSED] = "Charge paused", [COMPLETE] = "Charge complete",
	};
	static const char *const action[] = {
		[READY] = "Start charge", [CHARGING] = "Pause charge",
		[PAUSED] = "Resume charge", [COMPLETE] = "New session",
	};
	const bool charging = charger.state == CHARGING;
	const unsigned int voltage_tenths = 480 + charger.percent * 64 / 100;

	lv_label_set_text(charger.status, status[charger.state]);
	lv_label_set_text(charger.action, action[charger.state]);
	lv_label_set_text_fmt(charger.percent_text, "%u%%", charger.percent);
	lv_bar_set_value(charger.progress, charger.percent, LV_ANIM_OFF);
	lv_label_set_text_fmt(charger.voltage, "%u.%u V",
			      voltage_tenths / 10, voltage_tenths % 10);
	lv_label_set_text(charger.current, charging ? "32.0 A" : "0.0 A");
	if (charging) {
		lv_label_set_text_fmt(charger.remaining, "%u min", 100 - charger.percent);
	} else {
		lv_label_set_text(charger.remaining, charger.state == COMPLETE ? "0 min" : "--");
	}
}

static void toggle_charge(lv_event_t *event)
{
	switch (charger.state) {
	case READY:
	case PAUSED:
		charger.state = CHARGING;
		break;
	case CHARGING:
		charger.state = PAUSED;
		break;
	case COMPLETE:
		charger.percent = 64;
		charger.state = READY;
		break;
	}
	update_display();
}

static void reset_session(lv_event_t *event)
{
	charger.state = READY;
	charger.percent = 64;
	update_display();
}

static void advance_demo(lv_timer_t *timer)
{
	if (charger.state == CHARGING) {
		if (++charger.percent == 100) {
			charger.state = COMPLETE;
		}
		update_display();
	}
}

static void refresh_can(lv_timer_t *timer)
{
	struct battery_sample sample;
	static bool remote_was_active;

	if (!charger_uds_sample(&sample)) {
		if (remote_was_active) {
			lv_label_set_text(charger.status, "CAN data stale");
		}
		return;
	}
	remote_was_active = true;
	lv_label_set_text(charger.status, sample.faults ? "Battery fault" :
			  sample.warnings ? "Battery warning" : "CAN simulation");
	lv_label_set_text_fmt(charger.percent_text, "%u%%", sample.soc_permille / 10);
	lv_bar_set_value(charger.progress, sample.soc_permille / 10, LV_ANIM_OFF);
	lv_label_set_text_fmt(charger.voltage, "%u.%u V",
			      sample.voltage_decivolts / 10, sample.voltage_decivolts % 10);
	lv_label_set_text_fmt(charger.current, "%s%d.%d A", sample.current_deciamps < 0 ? "-" : "",
			      abs(sample.current_deciamps) / 10, abs(sample.current_deciamps) % 10);
	lv_label_set_text_fmt(charger.remaining, "%s%d.%d C",
			      sample.temperature_decicelsius < 0 ? "-" : "",
			      abs(sample.temperature_decicelsius) / 10,
			      abs(sample.temperature_decicelsius) % 10);
}

static lv_obj_t *metric(lv_obj_t *screen, int x, const char *title)
{
	lv_obj_t *card = panel(screen, x, 372, 308, 116, WHITE, 14);

	label(card, title, 20, 17, &lv_font_montserrat_20, MUTED);
	return label(card, "", 20, 52, &lv_font_montserrat_28, INK);
}

static void create_dashboard(void)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_t *header;
	lv_obj_t *session;
	lv_obj_t *wordmark;

	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, lv_color_hex(SURFACE), 0);

	header = panel(screen, 0, 0, 1024, 84, WHITE, 0);
	panel(header, 0, 80, 1024, 4, YELLOW, 0);
	wordmark = lv_image_create(header);
	lv_image_set_src(wordmark, &logo);
	lv_obj_set_pos(wordmark, 30, 19);
	label(header, "FORKLIFT CHARGER", 716, 30, &lv_font_montserrat_20, MUTED);

	session = panel(screen, 30, 106, 964, 244, INK, 16);
	label(session, "BATTERY STATE OF CHARGE", 24, 22, &lv_font_montserrat_20, 0xc2cad1);
	charger.status = label(session, "", 642, 23, &lv_font_montserrat_28, WHITE);
	charger.percent_text = label(session, "", 24, 66, &lv_font_montserrat_48, WHITE);
	charger.progress = lv_bar_create(session);
	lv_obj_set_pos(charger.progress, 24, 142);
	lv_obj_set_size(charger.progress, 916, 18);
	lv_obj_set_style_bg_color(charger.progress, lv_color_hex(0x414951), LV_PART_MAIN);
	lv_obj_set_style_bg_color(charger.progress, lv_color_hex(YELLOW), LV_PART_INDICATOR);
	label(session, "48 V battery  /  Simulated session", 24, 192,
	      &lv_font_montserrat_20, 0xc2cad1);

	charger.voltage = metric(screen, 30, "Battery voltage");
	charger.current = metric(screen, 358, "Charge current");
	charger.remaining = metric(screen, 686, "Time to full / Temp");
	button(screen, "Reset demo", 30, 206, false, reset_session);
	charger.action = button(screen, "", 706, 288, true, toggle_charge);
	update_display();
	lv_timer_create(advance_demo, 1500, NULL);
	lv_timer_create(refresh_can, 250, NULL);
}

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;

	if (!device_is_ready(display)) {
		printk("LTDC is not ready\n");
		return -ENODEV;
	}
	ret = riverdi_panel_init();
	if (ret < 0) {
		printk("Panel initialization failed (%d)\n", ret);
		return ret;
	}
	ret = riverdi_touch_init();
	if (ret < 0) {
		printk("Touch initialization failed (%d)\n", ret);
	}
	create_dashboard();
	lv_timer_handler();
	display_blanking_off(display);
	ret = charger_uds_init();
	if (ret) {
		printk("CAN/UDS initialization failed (%d)\n", ret);
	}
#ifdef CONFIG_BOOTLOADER_MCUBOOT
	if (ret == 0 && !boot_is_img_confirmed()) {
		ret = boot_write_img_confirmed();
		if (ret) {
			printk("MCUboot confirmation failed (%d)\n", ret);
		}
	}
#endif

	while (true) {
		uint32_t delay = lv_timer_handler();

		k_msleep(CLAMP(delay, 1, 20));
	}
}
