/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * drawlist: a renderer-neutral description of one System Multi 32 frame (left screen, 320 x 224),
 * built from the board's memories, for a polygon renderer (PowerVR) and for a host rasteriser that
 * checks it against runtime/src/video.c.
 *
 * Model (see PVR-RENDERER-DESIGN.md):
 *  - Each tile/text/bitmap layer is a "plane" of raw layer pixels (what video.c writes into a layer
 *    buffer: palette offset, 0 transparent); every screen row samples the plane at (sx + x, sy).
 *  - Sprites are decoded records, captured when the board sends the list (the frame shows the list
 *    sent one update earlier, as the frame buffers do).
 *  - The mixer becomes depth bands and palette bases; colour = palette[base + ((p >> shift) & 0xfff0)
 *    + (p & 15)] plus a colour offset set.
 * Only what OutRunners uses is modelled exactly; anything else increments dl_frame.unsupported.
 */
#ifndef DC_DRAWLIST_H
#define DC_DRAWLIST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <s32/video.h>

#define DL_WIDTH 320
#define DL_HEIGHT 224
#define DL_MAX_SPRITES 512
#define DL_MAX_SPANS 8

enum { DL_TEXT, DL_NBG0, DL_NBG1, DL_NBG2, DL_NBG3, DL_BITMAP, DL_SPRITES, DL_BACKDROP, DL_LAYERS };

/* unsupported feature bits */
#define DL_UNSUP_ZOOM      0x0001
#define DL_UNSUP_CLIPWIN   0x0002
#define DL_UNSUP_FLIP      0x0004
#define DL_UNSUP_NBG13     0x0008
#define DL_UNSUP_WIDE      0x0010
#define DL_UNSUP_SPR_RAM   0x0020
#define DL_UNSUP_SPR_FLIPS 0x0040
#define DL_UNSUP_NO_ERASE  0x0080
#define DL_UNSUP_LINEWIN   0x0100
#define DL_UNSUP_SPANS     0x0200

typedef struct dl_mix
{
	bool on;
	uint8_t priority;        /* video.c's mix_layer.priority: (register & 15) << 3 | rank */
	uint16_t palette_base;
	uint8_t shift;
	uint8_t offsets;         /* 0, 1 or 2 (none) */
	uint8_t blend;           /* layers it blends with (bits by DL_ index), 0 none */
	uint16_t sprite_blend;
} dl_mix;

typedef struct dl_plane
{
	bool on;
	int16_t sx[DL_HEIGHT];   /* plane column at screen x = 0; plane width 1024 (NBG), 512 (text, bitmap) */
	int16_t sy[DL_HEIGHT];   /* plane row, or -1: row empty */
	uint8_t span_count[DL_HEIGHT];          /* clip windows: drawn column spans per row */
	int16_t spans[DL_HEIGHT][DL_MAX_SPANS][2];    /* [first, end) */
} dl_plane;

typedef struct dl_sprite
{
	uint32_t place;          /* bank << 20 | word offset of the graphics */
	uint16_t words;          /* source width in 32-bit words */
	uint16_t rows;           /* source height */
	int16_t x, y;            /* screen position of the first pixel drawn (top left unless flipped) */
	int16_t width, height;   /* on screen */
	int32_t x_zoom, y_zoom;  /* 16.16 source pens / rows per screen pixel */
	bool flip_x, flip_y;
	bool bits8;
	bool indirect;
	bool shadow;             /* shadow sprite: clears bit 15 under it */
	bool extra;              /* its colours from the extra palette where one is set (s32_video_extra_at) */
	uint8_t end_pen;
	uint16_t color;          /* direct: 0x8000 | colour bits */
	uint16_t transparent_bits;
	uint16_t table[16];      /* indirect */
	int16_t clip_left, clip_right, clip_top, clip_bottom;     /* inside, inclusive */
	int16_t out_left, out_right, out_top, out_bottom;         /* outside (exclusion), inclusive; empty if right < left */
} dl_sprite;

typedef struct dl_frame
{
	bool display_on;
	dl_mix layers[DL_LAYERS];            /* text..bitmap, backdrop at DL_BACKDROP */
	dl_plane planes[DL_SPRITES];         /* DL_TEXT..DL_BITMAP */
	uint16_t backdrop[DL_HEIGHT];        /* raw backdrop pixel per row */
	/* sprites */
	dl_mix groups[16];                   /* per sprite group: priority, palette base, shift, offsets */
	uint8_t group_shift, group_mask;
	uint16_t sprite_bits;                /* ((1 << group_shift) - 1) & 0x3fff */
	uint16_t shadow_mask, shadow_pen;
	int sprite_count;
	const dl_sprite *sprites;            /* the list shown */
	/* the list the board has sent since, which the next frame shows: a frame's notice of new graphics */
	int next_sprite_count;
	const dl_sprite *next_sprites;
	/* a port's own sprites over the frame (dl_overlay: the pause's words): the list's entries from here
	   on are drawn over everything, the frame under them dimmed; -1 none */
	int overlay_from;
	/* colour */
	int offsets[3][3];
	bool blending;
	int blend_factor;
	uint32_t unsupported;
} dl_frame;

/* Graphics callbacks as video.c has them (the port's s32_graphics) */
void dl_init(const s32_graphics *graphics);

/* Hook for s32_board.sprites_due, called beside video.c's (or instead of it) */
void dl_sprites_due(bool erase, bool draw);

/* A port's own sprites over the frame (video.c's s32_video_overlay_sprites): entries as sprite RAM holds
   them, 16 bytes each, an end command last; drawn over the frames built from now on, which are dimmed
   under them. NULL: none again */
void dl_overlay(const uint8_t *list, size_t size);

/* The frame as the board's memories stand now (call where s32_video_draw_frame would run) */
const dl_frame *dl_build(void);

/* Raw pixel of a plane (layer index DL_TEXT..DL_BITMAP) at plane coordinates; 0 transparent */
uint16_t dl_plane_pixel(int layer, int sx, int sy);

/* One source row of a sprite: per pen position, the pixel the frame buffer would get, or
   0xffff where nothing is drawn (transparent, skipped end code, past the row end). out: words * 8 or * 4 */
bool dl_sprite_decode_row(const dl_sprite *sprite, int row, uint16_t *out);
/* the row's packed words as the sprite RAM holds them (s->words of them): false while they are loading */
bool dl_sprite_row_words(const dl_sprite *sprite, int row, uint32_t *words);

/* Palette colour (RGB555) for a mixer entry and raw pixel */
uint16_t dl_color(const dl_mix *entry, int pixel);

/* For texture builders: a tilemap plane's cell (64 x 32 cells of 16 x 16 over 1024 x 512) and its tile
   bank; a tile's 256 pens (NULL: all pen 15); a text cell (64 x 32 of 8 x 8) and its character; FNV-1a
   of video RAM bytes */
uint16_t dl_plane_cell(int layer, int cx, int cy, int *bank);
const uint8_t *dl_tile_pens(uint32_t tile);
uint16_t dl_text_cell(int cx, int cy, uint32_t *character);
uint32_t dl_video_hash(uint32_t byte_address, uint32_t bytes);
void dl_plane_pages(int layer, int *pages, int *bank);
/* Optional: a whole row of sprite graphics at once (place as dl_sprite.place, 'words' 32-bit words in
   the order sprite_word gives them): 1 read, -1 not available yet (dl_sprite_decode_row then returns
   false), 0 not handled (sprite_word per word instead) */
void dl_set_sprite_row_reader(int (*reader)(uint32_t place, int words, uint32_t *out));
uint16_t dl_video_word(uint32_t index);

#endif
