/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef DC_VMU_SAVE_H
#define DC_VMU_SAVE_H
#include <stdint.h>
#include <stdbool.h>
/* The console menus' own options, kept with them (main.c fills and reads them): volume, dead zone,
   sensitivity, steering speed, vibration, FPS; the rest spare. dc_options_loaded: a VMU had them. */
#define DC_OPTIONS 16
extern uint8_t dc_options[DC_OPTIONS];
extern bool dc_options_loaded;
/* What the arcade board keeps with the power off (its EEPROM: the settings; the battery-backed end of the
   sound board's RAM: the course records, bookkeeping and credits) in a file on the first VMU found. */
int dc_vmu_load(void);       /* at boot, before program_reset: 1 loaded, 0 none there, -1 no VMU */
void dc_vmu_frame(uint32_t frame);    /* each frame: a changed copy goes to the VMU (in the background) */
extern volatile uint32_t dc_vmu_state;    /* bits 0-1 load result + 1, bits 8-23 saves written, 24-31 save failures */
#endif
