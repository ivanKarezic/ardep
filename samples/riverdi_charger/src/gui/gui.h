/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RIVERDI_CHARGER_GUI_H
#define RIVERDI_CHARGER_GUI_H

#include <stdbool.h>
#include <stdint.h>

void gui_init(void);
uint32_t gui_process(void);

bool gui_get_power_module_enable(void);
uint16_t gui_get_current_setpoint_100ma(void);
uint16_t gui_get_fan_pwm_setpoint_pct(void);

void gui_set_actual_voltage_10mv(uint16_t voltage_10mv);
void gui_set_actual_current_100ma(uint16_t current_100ma);
void gui_set_actual_temperature_2c(int8_t temperature_2c);
void gui_set_status_bitfield(uint16_t status);

#endif // RIVERDI_CHARGER_GUI_H