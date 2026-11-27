/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef DC_AICA_PCM_H
#define DC_AICA_PCM_H
#include <stdint.h>
#include <stdbool.h>
#include <s32/multipcm.h>
int dc_aica_pcm_start(const char *folder);                          /* after the media is open */
void dc_aica_pcm_written(const multipcm *chip, int offset, uint8_t data);  /* after each chip write */
void dc_aica_pcm_frame(const multipcm *chip);                       /* once a frame */
extern volatile uint32_t dc_aica_stats[8];
/* OPTIONS > VOLUME, 0-10 (5 the game's own); the pause: every voice silent until it ends */
void dc_aica_set_volume(int volume);
void dc_aica_pause(bool paused);
/* OPTIONS > SOUND's MUSIC, EFFECTS and VOICE: each sample category's gain (256 the sample as it is, 0 none;
   console_sound_gains), on the AICA's level as each voice's sample's category (multipcm_voice.category) has it */
void dc_aica_set_category_gains(const int32_t gains[MULTIPCM_CATEGORIES]);

#endif
