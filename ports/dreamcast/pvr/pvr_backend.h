/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef DC_PVR_BACKEND_H
#define DC_PVR_BACKEND_H
#include "drawlist.h"
int dc_pvr_start(void);                    /* after vid_set_mode */
void dc_pvr_frame(const dl_frame *frame);  /* at the start of VBLANK, where the board's video would draw */
extern volatile uint32_t dc_pvr_stats[16];
extern volatile uint32_t dc_pvr_wait_us, dc_pvr_work_us, dc_pvr_ready_wait_us;
#endif
