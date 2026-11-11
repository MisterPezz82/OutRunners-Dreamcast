/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef DC_PORT_H
#define DC_PORT_H

#include <stdbool.h>

void dc_controls_update(void);
bool dc_exit_requested(void);
void dc_present_frame(void);
void dc_status(const char *msg);
void dc_status_off(void);

/* startup.c: the credits and the output choice before the game */
#define DC_VIDEO_240P 0
#define DC_VIDEO_480P 1
extern int dc_video_choice;
void dc_startup_screen(void);
void dc_loading_progress(int thousandths, int next);
void dc_loading_message(const char *message);
void dc_loading_finish(void);

/* the console build (input.c): the menu actions newly pressed (ACTION_BIT, console.h), for the pause's own
   loop; true once when Start asked for the pause in a race */
#include <stdint.h>
uint32_t dc_menu_actions(void);
bool dc_pause_wanted(void);

#endif