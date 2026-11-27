/**
 *    Name:        sound_mix.c
 *    Description: OPTIONS > SOUND's MUSIC, EFFECTS and VOICE (issue 57): the MultiPCM's samples by what they are,
 *                 and the gains the settings give the sound chips
 *    Version:     0.1.0
 *    Created:     25-11-2026
 *    Modified:    25-11-2026
 *    Author:      Sebastian Toyos
 *
 *    A player's issue 57: the DJ over the radio's songs. The board's sound is the YM3438's (the songs) and the
 *    MultiPCM's samples (the songs' drums and instruments, the engines, the tyres, the crashes, the scenery, the
 *    DJ and the other voices). Each MultiPCM voice takes its sample's category as its key goes on
 *    (sound_chips_set_categories: console_sample_category, from console/pcm_categories.c), and each category its
 *    gain (sound_chips_set_gains: console_sound_gains); 5 each is the board's sound to the bit.
 */
#include "console.h"
#include "console_internal.h"

#include "s32/machine.h"

/* A MultiPCM sample's category, by its address as the chip has it: where that is in the pcm region through the
   banks now (the windows' pages, as the voice's key goes on), and the sample there (console_pcm_samples, by their
   offsets); a place in none: EFFECTS */
uint8_t console_sample_category(uint32_t address)
{
	const uint32_t at = s32_pcm_region_address(address);
	size_t low = 0, high = console_pcm_sample_count;

	while (low < high)    /* (past the last sample that starts at or before it) */
	{
		const size_t middle = (low + high) / 2;

		if (console_pcm_samples[middle].offset <= at)
			low = middle + 1;
		else
			high = middle;
	}
	if (low > 0 && at < console_pcm_samples[low - 1].offset + console_pcm_samples[low - 1].length)
		return console_pcm_samples[low - 1].category;
	return PCM_EFFECTS;
}

/* The gains SOUND's settings give the sound chips: MUSIC the YM3438's and the songs' samples', EFFECTS and VOICE
   their samples', each its setting over 5 (5 the game's own, 10 twice it, 0 none); MULTIPCM_CATEGORIES of 'pcm' */
void console_sound_gains(double *fm, int32_t *pcm)
{
	const console_window *settings = &console_window_settings;

	*fm = settings->music_volume / 5.0;
	pcm[PCM_MUSIC] = settings->music_volume * 256 / 5;
	pcm[PCM_EFFECTS] = settings->effects_volume * 256 / 5;
	pcm[PCM_VOICE] = settings->voice_volume * 256 / 5;
	pcm[3] = 256;    /* (none of the categories: as it is) */
}
