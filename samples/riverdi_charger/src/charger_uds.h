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

int charger_uds_init(void);
bool charger_uds_sample(struct battery_sample *sample);

#endif
