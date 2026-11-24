/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "panel.h"
#include "gui.h"
#include "charger_uds.h"
#ifdef CONFIG_BOOTLOADER_MCUBOOT
#include <zephyr/dfu/mcuboot.h>
#endif

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;

	if (!device_is_ready(display)) {
		printk("LTDC is not ready\n");
		return -ENODEV;
	}
	ret = charger_panel_init();
	if (ret < 0) {
		printk("Panel initialization failed (%d)\n", ret);
		return ret;
	}
	ret = charger_touch_init();
	if (ret < 0) {
		printk("Touch initialization failed (%d)\n", ret);
	}
	gui_init();
	gui_process();
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
		uint32_t delay = gui_process();

		k_msleep(CLAMP(delay, 1, 20));
	}
}
