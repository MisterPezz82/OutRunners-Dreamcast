/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef DC_DIAG_SCREEN_H
#define DC_DIAG_SCREEN_H
#include <stdint.h>
/* where the threads are, for the crash and freeze screens: main thread F frame callback, T board time,
   S sound end of frame, P render prepare, W waiting for a list set, R rt_frame, H handed over, G game;
   submit thread I idle, Y waiting for the previous render to start, D for it to finish, L flushing held
   texture writes, X sending the lists */
extern volatile char dc_where, dc_submit_where;
void dc_diag_screen(const char *title, uint32_t code, uint32_t pc, uint32_t pr, uint32_t sp);
void dc_diag_screen_hold(const char *title, uint32_t code, uint32_t pc, uint32_t pr, uint32_t sp);    /* (never returns) */
void dc_diag_start_watchdog(void);
#endif
