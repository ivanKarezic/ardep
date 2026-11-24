/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RIVERDI_CHARGER_PANEL_H
#define RIVERDI_CHARGER_PANEL_H

#if defined(CONFIG_BOARD_STM32H7B3I_EVAL) || defined(CONFIG_BOARD_STM32F769I_DISCO)
#include <errno.h>
#include <zephyr/device.h>
#endif

#ifdef CONFIG_BOARD_STM32H7B3I_EVAL
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static inline int charger_panel_init(void)
{
	const struct gpio_dt_spec reset =
		GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), panel_reset_gpios);
	int ret;

	if (!gpio_is_ready_dt(&reset)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	k_msleep(20);
	ret = gpio_pin_set_dt(&reset, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(10);
	return 0;
}

static inline int charger_touch_init(void)
{
	return device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))) ? 0 : -ENODEV;
}
#elif defined(CONFIG_BOARD_STM32F769I_DISCO)
static inline int charger_panel_init(void)
{
	/* The OTM8009A display driver resets and initializes this panel at boot. */
	return 0;
}

static inline int charger_touch_init(void)
{
	return device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch))) ? 0 : -ENODEV;
}
#else
int charger_panel_init(void);
int charger_touch_init(void);
#endif

#endif
