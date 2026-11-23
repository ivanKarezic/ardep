/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "panel.h"
#include "charger_uds.h"
#include "truck_art.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

#define INK 0x171a1e
#define MUTED 0x58616c
#define WHITE 0xffffff
#define SURFACE 0xf2f4f5
#define LINE 0xdce1e5
#define YELLOW 0xffc800
#define RED 0xb3261e

enum telemetry_source {
	LOCAL_DEMO,
	CAN_LIVE,
	CAN_STALE,
};

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
	enum telemetry_source source;
	unsigned int percent;
	struct battery_sample sample;
	lv_obj_t *status;
	lv_obj_t *source_label;
	lv_obj_t *percent_text;
	lv_obj_t *progress;
	lv_obj_t *voltage;
	lv_obj_t *current;
	lv_obj_t *remaining;
	lv_obj_t *remaining_title;
	lv_obj_t *action;
	lv_obj_t *dashboard;
	lv_obj_t *truck;
	lv_obj_t *truck_name;
	lv_obj_t *truck_caption;
} charger = { .percent = 64 };

static struct {
	uint16_t warnings;
	uint16_t errors;
	uint16_t warning_history;
	uint16_t error_history;
	uint16_t popup_warnings;
	uint16_t popup_errors;
	lv_obj_t *page;
	lv_obj_t *details;
	lv_obj_t *status;
	lv_obj_t *navigation;
	lv_obj_t *overlay;
	lv_obj_t *accent;
	lv_obj_t *headline;
	lv_obj_t *summary;
} alerts;

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

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width, bool primary,
			lv_event_cb_t callback)
{
	lv_obj_t *obj = lv_button_create(parent);
	lv_obj_t *caption;

	lv_obj_set_pos(obj, x, y);
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
	const struct battery_sample *sample = &charger.sample;

	lv_label_set_text(charger.status, status[charger.state]);
	lv_label_set_text(charger.action, action[charger.state]);
	if (charger.source != LOCAL_DEMO) {
		if (sample->faults || sample->warnings) {
			lv_label_set_text(charger.status, sample->faults ? "Battery error" :
					  "Battery warning");
		}
		lv_label_set_text(charger.source_label,
				  charger.source == CAN_LIVE ? "CAN telemetry live" : "CAN telemetry stale");
		lv_label_set_text_fmt(charger.percent_text, "%u%%", sample->soc_permille / 10);
		lv_bar_set_value(charger.progress, sample->soc_permille / 10, LV_ANIM_OFF);
		lv_label_set_text_fmt(charger.voltage, "%u.%u V",
				      sample->voltage_decivolts / 10, sample->voltage_decivolts % 10);
		lv_label_set_text_fmt(charger.current, "%s%d.%d A", sample->current_deciamps < 0 ? "-" : "",
				      abs(sample->current_deciamps) / 10, abs(sample->current_deciamps) % 10);
		lv_label_set_text(charger.remaining_title, "Battery temperature");
		lv_label_set_text_fmt(charger.remaining, "%s%d.%d C",
				      sample->temperature_decicelsius < 0 ? "-" : "",
				      abs(sample->temperature_decicelsius) / 10,
				      abs(sample->temperature_decicelsius) % 10);
		return;
	}
	lv_label_set_text(charger.source_label, "Local demonstration");
	lv_label_set_text(charger.remaining_title, "Estimated time");
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
	if (charger.source == LOCAL_DEMO && charger.state == CHARGING) {
		if (++charger.percent == 100) {
			charger.state = COMPLETE;
		}
		update_display();
	}
}

static void update_truck(enum charger_truck truck)
{
	static const char *const models[] = {
		[CHARGER_TRUCK_NONE] = "No truck connected",
		[CHARGER_TRUCK_EJC216] = "EJC 216",
		[CHARGER_TRUCK_EFG216] = "EFG 216",
		[CHARGER_TRUCK_ETV216I] = "ETV 216i",
		[CHARGER_TRUCK_EFG425] = "EFG 425",
		[CHARGER_TRUCK_CUSTOM] = "Custom truck",
	};
	static const char *const descriptions[] = {
		[CHARGER_TRUCK_NONE] = "Select a truck in Charger Tool",
		[CHARGER_TRUCK_EJC216] = "Pedestrian stacker",
		[CHARGER_TRUCK_EFG216] = "Three-wheel forklift",
		[CHARGER_TRUCK_ETV216I] = "Reach truck",
		[CHARGER_TRUCK_EFG425] = "Four-wheel forklift",
		[CHARGER_TRUCK_CUSTOM] = "Custom simulation profile",
	};
	const lv_image_dsc_t *art = truck_art(truck);

	lv_label_set_text(charger.truck_name, models[truck]);
	lv_label_set_text(charger.truck_caption, descriptions[truck]);
	if (art) {
		lv_image_set_src(charger.truck, art);
		lv_obj_remove_flag(charger.truck, LV_OBJ_FLAG_HIDDEN);
		lv_obj_invalidate(charger.truck);
	} else {
		lv_obj_add_flag(charger.truck, LV_OBJ_FLAG_HIDDEN);
	}
}

static void update_alert_details(void)
{
	static const char *const warnings[] = {
		"High battery temperature. Check cooling and the battery temperature reading.",
		"Low state of charge. Check battery level before continuing operation.",
	};
	static const char *const errors[] = {
		"Battery overvoltage. Check the measured voltage and charger configuration.",
		"Battery sensor fault. Check the sensor and its wiring.",
	};
	static char details[3600];
	size_t used = 0;
	unsigned int active = POPCOUNT(alerts.warnings) + POPCOUNT(alerts.errors);

	if (active) {
		lv_label_set_text_fmt(alerts.navigation, "Warnings and errors (%u)", active);
	} else {
		lv_label_set_text(alerts.navigation, "Warnings and errors");
	}
	lv_label_set_text_fmt(alerts.status, "%d active warning%s   /   %d active error%s",
			      POPCOUNT(alerts.warnings), POPCOUNT(alerts.warnings) == 1 ? "" : "s",
			      POPCOUNT(alerts.errors), POPCOUNT(alerts.errors) == 1 ? "" : "s");
	for (int group = 0; group < 2; group++) {
		const bool error = group == 0;
		const uint16_t history = error ? alerts.error_history : alerts.warning_history;
		const uint16_t current = error ? alerts.errors : alerts.warnings;
		const char *const *messages = error ? errors : warnings;

		for (unsigned int bit = 0; bit < 16 && used < sizeof(details) - 1; bit++) {
			if (!(history & BIT(bit))) {
				continue;
			}
			used += snprintk(details + used, sizeof(details) - used,
					 "%s %c%02u  |  %s\n%s\n\n",
					 error ? "Error" : "Warning", error ? 'E' : 'W', bit + 1,
					 current & BIT(bit) ? "Active" : "Cleared",
					 bit < ARRAY_SIZE(errors) ? messages[bit] :
					 "Unmapped diagnostic flag. See the target's diagnostic documentation.");
		}
	}
	lv_label_set_text(alerts.details, used ? details : "No warnings or errors recorded.");
}

static void update_alert_overlay(void)
{
	if (!(alerts.popup_errors || alerts.popup_warnings)) {
		lv_obj_add_flag(alerts.overlay, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	const bool has_errors = alerts.popup_errors != 0;

	lv_obj_set_style_bg_color(alerts.accent, lv_color_hex(has_errors ? RED : YELLOW), 0);
	lv_label_set_text(alerts.headline, has_errors ? "Battery error" : "Battery warning");
	lv_label_set_text_fmt(alerts.summary, "%d new warning(s)   /   %d new error(s)",
			      POPCOUNT(alerts.popup_warnings), POPCOUNT(alerts.popup_errors));
	lv_obj_remove_flag(alerts.overlay, LV_OBJ_FLAG_HIDDEN);
}

static void acknowledge_alerts(lv_event_t *event)
{
	alerts.popup_warnings = 0;
	alerts.popup_errors = 0;
	update_alert_overlay();
}

static void show_alerts(lv_event_t *event)
{
	acknowledge_alerts(event);
	lv_obj_add_flag(charger.dashboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(alerts.page, LV_OBJ_FLAG_HIDDEN);
}

static void show_dashboard(lv_event_t *event)
{
	lv_obj_add_flag(alerts.page, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(charger.dashboard, LV_OBJ_FLAG_HIDDEN);
}

static void update_alerts(uint16_t warnings, uint16_t errors)
{
	if (warnings == alerts.warnings && errors == alerts.errors) {
		return;
	}
	alerts.popup_warnings = (alerts.popup_warnings | (warnings & ~alerts.warnings)) & warnings;
	alerts.popup_errors = (alerts.popup_errors | (errors & ~alerts.errors)) & errors;
	alerts.warnings = warnings;
	alerts.errors = errors;
	alerts.warning_history |= warnings;
	alerts.error_history |= errors;
	update_alert_details();
	update_alert_overlay();
}

static void refresh_can(lv_timer_t *timer)
{
	static enum charger_truck displayed_truck;
	struct battery_sample sample;
	const enum charger_truck truck = charger_uds_truck();

	if (truck != displayed_truck) {
		update_truck(truck);
		displayed_truck = truck;
	}
	if (charger_uds_sample(&sample)) {
		if (charger.source != CAN_LIVE || memcmp(&sample, &charger.sample, sizeof(sample))) {
			charger.source = CAN_LIVE;
			charger.sample = sample;
			update_display();
			update_alerts(sample.warnings, sample.faults);
		}
	} else if (charger.source == CAN_LIVE) {
		charger.source = CAN_STALE;
		update_display();
	}
}

static lv_obj_t *metric(lv_obj_t *screen, int x, const char *title, lv_obj_t **title_obj)
{
	lv_obj_t *card = panel(screen, x, 394, 310, 102, WHITE, 12);
	lv_obj_t *caption = label(card, title, 20, 13, &lv_font_montserrat_20, MUTED);

	if (title_obj) {
		*title_obj = caption;
	}
	return label(card, "", 20, 48, &lv_font_montserrat_28, INK);
}

static void create_alert_pages(lv_obj_t *screen)
{
	lv_obj_t *scroll;
	lv_obj_t *card;

	alerts.page = panel(screen, 0, 84, 1024, 516, SURFACE, 0);
	label(alerts.page, "Warnings and errors", 30, 18, &lv_font_montserrat_28, INK);
	alerts.status = label(alerts.page, "", 30, 58, &lv_font_montserrat_20, MUTED);
	button(alerts.page, "Dashboard", 780, 14, 214, false, show_dashboard);
	scroll = panel(alerts.page, 30, 98, 964, 393, WHITE, 12);
	lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
	alerts.details = label(scroll, "", 22, 19, &lv_font_montserrat_20, INK);
	lv_obj_set_width(alerts.details, 908);
	lv_obj_set_style_text_line_space(alerts.details, 6, 0);
	update_alert_details();
	lv_obj_add_flag(alerts.page, LV_OBJ_FLAG_HIDDEN);

	alerts.overlay = panel(lv_layer_top(), 0, 0, 1024, 600, INK, 0);
	lv_obj_set_style_bg_opa(alerts.overlay, LV_OPA_60, 0);
	lv_obj_add_event_cb(alerts.overlay, acknowledge_alerts, LV_EVENT_CLICKED, NULL);
	card = panel(alerts.overlay, 222, 183, 580, 235, WHITE, 14);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
	alerts.accent = panel(card, 0, 0, 580, 8, YELLOW, 0);
	lv_obj_remove_flag(alerts.accent, LV_OBJ_FLAG_CLICKABLE);
	alerts.headline = label(card, "", 27, 30, &lv_font_montserrat_28, INK);
	alerts.summary = label(card, "", 27, 85, &lv_font_montserrat_20, MUTED);
	button(card, "Acknowledge", 27, 148, 245, false, acknowledge_alerts);
	button(card, "View details", 302, 148, 250, true, show_alerts);
	lv_obj_add_flag(alerts.overlay, LV_OBJ_FLAG_HIDDEN);
}

static void create_dashboard(void)
{
	lv_obj_t *screen = lv_screen_active();
	lv_obj_t *header;
	lv_obj_t *session;
	lv_obj_t *wordmark;
	lv_obj_t *truck_card;

	lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, lv_color_hex(SURFACE), 0);

	header = panel(screen, 0, 0, 1024, 84, WHITE, 0);
	panel(header, 0, 80, 1024, 4, YELLOW, 0);
	wordmark = lv_image_create(header);
	lv_image_set_src(wordmark, &logo);
	lv_obj_set_pos(wordmark, 30, 19);
	label(header, "SLHx Advanced Charger", 716, 30, &lv_font_montserrat_20, MUTED);

	charger.dashboard = panel(screen, 0, 0, 1024, 600, SURFACE, 0);
	lv_obj_move_to_index(charger.dashboard, 0);
	session = panel(charger.dashboard, 30, 104, 588, 270, INK, 14);
	label(session, "STATE OF CHARGE", 26, 22, &lv_font_montserrat_20, 0xc2cad1);
	charger.percent_text = label(session, "", 26, 58, &lv_font_montserrat_48, WHITE);
	charger.progress = lv_bar_create(session);
	lv_obj_set_pos(charger.progress, 26, 135);
	lv_obj_set_size(charger.progress, 536, 17);
	lv_obj_set_style_bg_color(charger.progress, lv_color_hex(0x414951), LV_PART_MAIN);
	lv_obj_set_style_bg_color(charger.progress, lv_color_hex(YELLOW), LV_PART_INDICATOR);
	charger.status = label(session, "", 26, 172, &lv_font_montserrat_28, WHITE);
	charger.source_label = label(session, "", 26, 222, &lv_font_montserrat_20, 0xc2cad1);

	truck_card = panel(charger.dashboard, 634, 104, 360, 270, WHITE, 14);
	label(truck_card, "CONNECTED TRUCK", 20, 17, &lv_font_montserrat_20, MUTED);
	charger.truck = lv_image_create(truck_card);
	lv_obj_set_pos(charger.truck, 104, 46);
	charger.truck_name = label(truck_card, "", 20, 184, &lv_font_montserrat_28, INK);
	charger.truck_caption = label(truck_card, "", 20, 228, &lv_font_montserrat_20, MUTED);
	update_truck(CHARGER_TRUCK_NONE);

	charger.voltage = metric(charger.dashboard, 30, "Battery voltage", NULL);
	charger.current = metric(charger.dashboard, 357, "Charge current", NULL);
	charger.remaining = metric(charger.dashboard, 684, "Estimated time", &charger.remaining_title);
	alerts.navigation = button(charger.dashboard, "", 30, 514, 316, false, show_alerts);
	button(charger.dashboard, "Reset demo", 366, 514, 196, false, reset_session);
	charger.action = button(charger.dashboard, "", 706, 514, 288, true, toggle_charge);
	create_alert_pages(screen);
	update_display();
	lv_timer_create(advance_demo, 1500, NULL);
	lv_timer_create(refresh_can, 125, NULL);
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
