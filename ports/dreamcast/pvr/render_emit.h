/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * render_emit: turns a dl_frame into textures and depth-ordered quads through a small backend
 * interface, so the same code drives the PowerVR (pvr_backend.c) and a host rasteriser that checks
 * it (tools/dreamcast/drawlist_check.c). Quads are in source pixels (320 x 224); u, v in texels.
 */
#ifndef DC_RENDER_EMIT_H
#define DC_RENDER_EMIT_H

#include "drawlist.h"

typedef struct rt_backend
{
	/* a w x h ARGB1555 texture (powers of two), or -1 when out of texture memory */
	int (*tex_alloc)(int w, int h);
	void (*tex_free)(int handle);
	/* rectangle upload: 'pixels' rows of 'stride' ARGB1555 values */
	void (*tex_write)(int handle, int x, int y, int w, int h, const uint16_t *pixels, int stride);
	/* textured quad (wrap: repeat, else clamp); z larger is nearer; alpha-tested (bit 15). Colour:
	   texel * mod / 255 + add per channel (ARGB8888 values, alpha ignored), clamped */
	void (*quad)(int handle, bool wrap, float x0, float y0, float x1, float y1,
		float u0, float v0, float u1, float v1, float z, uint32_t mod, uint32_t add);
	/* translucent shadow: where the texel's bit 15 is set and z >= the depth there, halve the colour */
	void (*shadow)(int handle, float x0, float y0, float x1, float y1,
		float u0, float v0, float u1, float v1, float z);
	/* opaque solid quad (backdrop), coloured as quads are */
	void (*solid)(uint16_t argb1555, float x0, float y0, float x1, float y1, float z, uint32_t mod, uint32_t add);
	/* optional: microseconds, for the phase timings in rt_stats */
	uint64_t (*clock_us)(void);
	/* optional (NULL: every sprite texture is ARGB1555): 4-bit paletted sprite textures, the colours in
	   palette banks of 16, so one texture serves a graphic in any colours. A w x h texture, freed with
	   tex_free; rows y .. y + h - 1 (y and h even) from one index byte a texel; bank 0..RT_PAL_BANKS-1's
	   colours (ARGB1555, bit 15 clear: transparent); a quad drawn from a paletted texture with a bank */
	int (*tex_alloc_pal4)(int w, int h);
	void (*tex_write_pal4)(int handle, int y, int h, const uint8_t *indices, int stride);
	void (*pal_set)(int bank, const uint16_t colours[16]);
	void (*quad_pal)(int handle, int bank, float x0, float y0, float x1, float y1,
		float u0, float v0, float u1, float v1, float z, uint32_t mod, uint32_t add);
	/* optional (NULL: not drawn): a translucent quad of one colour over what is at or behind z, its alpha
	   the colour's top byte (a shadow sprite's rectangle, the dimmed frame under a port's overlay) */
	void (*tint)(float x0, float y0, float x1, float y1, float z, uint32_t argb);
} rt_backend;

#define RT_PAL_BANKS 64

typedef struct rt_stats
{
	uint32_t cells_converted;
	uint32_t sprite_textures_built;
	uint32_t sprite_cache_hits;
	uint32_t sprite_evictions;
	uint32_t quads;
	uint32_t palette_banks_changed;
	uint32_t skipped;              /* features not drawn (shadow sprites, exclusion windows...) */
	uint32_t texture_memory_failures;
	uint32_t sprites_pending;      /* not drawn: graphics still loading */
	uint32_t sprites_deferred;     /* not drawn: this frame's texture budget spent */
	uint32_t build_us_max, build_texels_max;    /* the frame's slowest sprite texture build (with clock_us) */
	uint32_t sprites_fallback;     /* drawn with the same graphic's last texture while the new one waits */
	uint32_t sprites_missing;      /* not drawn at all: nothing of the graphic ready */
	uint32_t built_recolour;       /* textures started for a graphic already built in other colours */
	uint32_t built_new;            /* textures started for a graphic with no texture yet */
	uint32_t build_us_total;       /* time spent building sprite textures (with clock_us) */
	uint32_t plane_cells_late;     /* tile cells whose update waits for a later frame */
	uint32_t pal_sprites;          /* sprites drawn from paletted textures */
	uint32_t pal_banks_full;       /* sprites that could not get a palette bank (drawn ARGB1555) */
	uint32_t phase_us[5];          /* palette scan, plane textures, backdrop + plane strips, sprites, shadows */
} rt_stats;

void rt_init(const rt_backend *backend);
/* Emits one frame (textures updated first, then quads). The backend must allow texture writes
   at this point (the previous frame's rendering finished). */
void rt_frame(const dl_frame *frame, rt_stats *stats);
/* Optional, before rt_frame for the same frame, while the backend cannot yet take texture writes to
   textures in flight: builds the frame's new sprite textures (fresh textures only) until keep_going()
   says stop or limit_us has passed, so time a backend spends waiting goes into them */
/* the time rt_frame may spend on sprite texture builds (RT_BUILD_US builds; default RT_BUILD_US) */
void rt_set_build_limit(uint32_t us);
extern rt_stats rt_prepare_stats;    /* (what the last rt_prepare counted) */
void rt_prepare(const dl_frame *frame, bool (*keep_going)(void), uint32_t limit_us);

#endif
