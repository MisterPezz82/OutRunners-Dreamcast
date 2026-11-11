/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * frame_hash.h (a test): hashes of a frame's inputs, shared by the console and the host check, to tell
 * where the console departs from the emulators. board: the game's video RAM, sprite RAM and left-screen
 * palette (the game logic's output); list: the drawlist's sprites and plane rows (what the renderer is
 * given). Same values on the console and the host for the same frame: the input is the same.
 */
#ifndef DC_FRAME_HASH_H
#define DC_FRAME_HASH_H

#include "drawlist.h"
#include <s32/machine.h>

#define FRAME_HASH_EVERY 300u

static inline uint32_t fh_bytes(uint32_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
	return h;
}
static inline uint32_t fh_word(uint32_t h, uint32_t v) { return fh_bytes(h, &v, 4); }

static inline uint32_t frame_hash_board(void)
{
	uint32_t h = 2166136261u;
	h = fh_bytes(h, s32_board.video_ram, S32_VIDEO_RAM_SIZE);
	h = fh_bytes(h, s32_board.sprite_ram, S32_SPRITE_RAM_SIZE);
	h = fh_bytes(h, s32_board.palette[0], sizeof(s32_board.palette[0]));
	return h;
}

static inline uint32_t frame_hash_list(const dl_frame *f)
{
	uint32_t h = 2166136261u;
	h = fh_word(h, (uint32_t)f->sprite_count);
	for (int i = 0; i < f->sprite_count; i++)
	{
		const dl_sprite *s = &f->sprites[i];
		h = fh_word(h, s->place);
		h = fh_word(h, (uint32_t)s->words << 16 | s->rows);
		h = fh_word(h, (uint32_t)(uint16_t)s->x << 16 | (uint16_t)s->y);
		h = fh_word(h, (uint32_t)(uint16_t)s->width << 16 | (uint16_t)s->height);
		h = fh_word(h, (uint32_t)s->x_zoom); h = fh_word(h, (uint32_t)s->y_zoom);
		h = fh_word(h, (uint32_t)s->flip_x | s->flip_y << 1 | s->bits8 << 2 | s->indirect << 3 | s->shadow << 4 | (uint32_t)s->end_pen << 8);
		h = fh_word(h, (uint32_t)s->color << 16 | s->transparent_bits);
		h = fh_word(h, (uint32_t)(uint16_t)s->clip_left << 16 | (uint16_t)s->clip_right);
		h = fh_word(h, (uint32_t)(uint16_t)s->clip_top << 16 | (uint16_t)s->clip_bottom);
	}
	for (int p = DL_TEXT; p <= DL_BITMAP; p++)
	{
		h = fh_bytes(h, f->planes[p].sx, sizeof(f->planes[p].sx));
		h = fh_bytes(h, f->planes[p].sy, sizeof(f->planes[p].sy));
	}
	return h;
}

#endif
