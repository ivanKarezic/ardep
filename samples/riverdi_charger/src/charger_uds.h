/* SPDX-License-Identifier: Apache-2.0 */

#ifndef RIVERDI_CHARGER_UDS_H
#define RIVERDI_CHARGER_UDS_H

#include <stdbool.h>
#include <stdint.h>

struct battery_sample {
	uint16_t soc_permille;
	uint16_t voltage_decivolts;
	int16_t current_deciamps;
	int16_t temperature_decicelsius;
	uint16_t warnings;
	uint16_t faults;
};

enum charger_truck {
	CHARGER_TRUCK_NONE,
	CHARGER_TRUCK_EJC216,
	CHARGER_TRUCK_EFG216,
	CHARGER_TRUCK_ETV216I,
	CHARGER_TRUCK_EFG425,
	CHARGER_TRUCK_CUSTOM,
	CHARGER_TRUCK_COUNT,
};

enum charger_indicator {
	CHARGER_POWER_STAGE = 0x01,
	CHARGER_PM_ENABLE = 0x02,
};

enum charger_charge_state {
	READY,
	CHARGING,
	PAUSED,
	COMPLETE,
};

int charger_uds_init(void);
bool charger_uds_sample(struct battery_sample *sample);
enum charger_truck charger_uds_truck(void);
uint8_t charger_uds_indicators(void);
enum charger_charge_state charger_charge_state(void);
int charger_set_charging(bool enable);
void charger_reset_charge(void);

#endif
