/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RIVERDI_CHARGER_PANEL_H
#define RIVERDI_CHARGER_PANEL_H

#ifdef CONFIG_BOARD_STM32H7B3I_EVAL
#include <errno.h>
#include <zephyr/device.h>

static inline int charger_panel_init(void)
{
	/* The parallel RGB panel uses the LTDC driver and board reset. */
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
