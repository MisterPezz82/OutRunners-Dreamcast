/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * pvr_backend.c: render_emit's backend on the Dreamcast PowerVR (KOS pvr API). The 320 x 224 board
 * picture is drawn 1:1 into the 320 x 240 video mode, 8 lines down. Quads are recorded
 * per list while render_emit runs, then sent opaque, punch-through, translucent.
 */
#include "render_emit.h"
#include "pvr_backend.h"
#include "vram_alloc.h"
#include "pal4_twiddle.h"
#include "frame_hash.h"
#include <s32/program.h>
#include "../dc_port.h"
#include "../diag_screen.h"
#include <kos.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/pvr/pvr_pal.h>
#include <dc/biosfont.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define MAX_TEX 1024
#define TEX_RESERVE (64 * 1024)      /* texture memory left to KOS beside the pool */
/* (the board's 320 x 224 drawn 1:1 into the 320 x 240 video mode, which the video hardware doubles for
   the display: a quarter of the fill work of drawing it at 640 x 448, and the arcade's own pixels) */
#define SX 1.0f
#define SY 1.0f
#define OY 8.0f

static struct
{
	pvr_ptr_t p;
	int32_t off;                /* in the pool */
	uint16_t w, h;
	uint8_t compiled;           /* bit 0 wrap header, bit 1 clamp header, bit 2 shadow header, bit 3 paletted */
	bool pal4;                  /* 4-bit paletted, twiddled */
	uint32_t scene, prev_scene; /* the last two scenes that drew from it */
	pvr_poly_hdr_t hdr[4];
} tex[MAX_TEX];

/* each list's recorded headers and vertices, 32 bytes an entry: two sets, one being recorded while the
   submit thread sends the other */
#define OP_BYTES (16 * 1024)
#define PT_BYTES (192 * 1024)
#define TR_BYTES (32 * 1024)
static uint8_t list_op[2][OP_BYTES] __attribute__((aligned(32)));
static uint8_t list_pt[2][PT_BYTES] __attribute__((aligned(32)));
static uint8_t list_tr[2][TR_BYTES] __attribute__((aligned(32)));
static size_t used_op, used_pt, used_tr;    /* (of the set being recorded) */
static int cur;                              /* the set being recorded */
static uint32_t overflows;
/* the scene being recorded; scenes scene_no - 1 (queued or rendering) and scene_no - 2 (rendering) may
   still be drawn from */
static uint32_t scene_no = 4;
volatile uint32_t dc_pvr_stats[16];
volatile uint32_t dc_pvr_wait_us, dc_pvr_work_us, dc_pvr_ready_wait_us;    /* evidence for the Flycast capture (render_emit's rt_stats) */

volatile uint32_t dc_probe_alloc_us, dc_probe_row_us;    /* (worst allocation, worst sprite-row read, per window) */
static uint32_t alloc_acc;

static int t_alloc_inner(int w, int h, bool pal4);
static int t_alloc(int w, int h)
{
	const uint64_t t = timer_us_gettime64();
	const int r = t_alloc_inner(w, h, false);
	alloc_acc += (uint32_t)(timer_us_gettime64() - t);
	return r;
}
static int t_alloc_pal4(int w, int h)
{
	const uint64_t t = timer_us_gettime64();
	const int r = t_alloc_inner(w, h, true);
	alloc_acc += (uint32_t)(timer_us_gettime64() - t);
	return r;
}

static uint8_t *pool;    /* texture memory taken from KOS once, divided by vram_alloc */

static int t_alloc_inner(int w, int h, bool pal4)
{
	const size_t bytes = pal4 ? (size_t)w * (size_t)h / 2 : (size_t)w * (size_t)h * 2;
	if (!pool) return -1;
	for (int i = 0; i < MAX_TEX; i++)
		if (!tex[i].p)
		{
			const int32_t off = va_alloc((uint32_t)bytes);
			if (off < 0) return -1;
			tex[i].p = (pvr_ptr_t)(pool + off); tex[i].off = off; tex[i].w = (uint16_t)w; tex[i].h = (uint16_t)h;
			tex[i].compiled = 0;
			tex[i].pal4 = pal4;
			tex[i].scene = tex[i].prev_scene = 0;
			return i;
		}
	return -1;
}

static void t_free(int t)
{
	const uint64_t t0 = timer_us_gettime64();
	va_free(tex[t].off);
	alloc_acc += (uint32_t)(timer_us_gettime64() - t0);
	tex[t].p = NULL;
}

/* rows straight into texture memory (non-twiddled), two texels a 32-bit write */
static void write_now(uint16_t *first16, int pitch_texels, int w, int h, const uint16_t *src, int stride)
{
	uint8_t *first = (uint8_t *)first16;
	/* through the store queues, 32 bytes at a time, when everything lines up (whole tile rows,
	   sprite textures), else two texels a 32-bit write */
	if (((uintptr_t)first & 31) == 0 && (w * 2) % 32 == 0 && ((uintptr_t)src & 7) == 0 && (stride * 2) % 8 == 0 &&
		(pitch_texels * 2) % 32 == 0)
	{
		uint8_t *sq = (uint8_t *)sq_lock(first);
		const size_t pitch = (size_t)pitch_texels * 2;
		for (int j = 0; j < h; j++)
			sq_fast_cpy(sq + (size_t)j * pitch, src + (size_t)j * stride, (size_t)w * 2 / 32);
		sq_wait();
		sq_unlock();
		return;
	}
	for (int j = 0; j < h; j++)
	{
		uint32_t *d = (uint32_t *)(first16 + (size_t)j * pitch_texels);
		const uint16_t *s = src + (size_t)j * stride;
		for (int i = 0; i < w; i += 2)
			*d++ = (uint32_t)s[i] | (uint32_t)s[i + 1] << 16;
	}
}

/* A submit thread sends each recorded scene to the PowerVR: it waits for the previous scene's render to
   finish, puts in the texture writes held for this scene, and sends the lists. The game's frame logic
   and render_emit's next frame meanwhile go on, with one copy of each plane texture: a write to a
   texture a scene in flight draws from is held in main RAM until then; any other goes straight in. */
#define STAGE_BYTES (192 * 1024)
#define STAGE_RECS 4096
typedef struct { uint16_t *dst; uint32_t off; uint16_t pitch, w, h; } stage_rec;
typedef struct
{
	size_t used_op, used_pt, used_tr;
	uint8_t *stage;
	size_t stage_used;
	int stage_count;
	stage_rec recs[STAGE_RECS];
} job_t;
static job_t jobs[2];
static semaphore_t jobs_free, jobs_ready;
static kthread_t *submitter;
static bool writes_direct;               /* (after a drain, this frame's writes go straight in) */
static uint32_t stage_overflows;
static volatile uint32_t submit_ready_us, submit_render_us, submit_send_us;

static void flush_stage(job_t *j)
{
	for (int i = 0; i < j->stage_count; i++)
	{
		const stage_rec *r = &j->recs[i];
		write_now(r->dst, r->pitch, r->w, r->h, (const uint16_t *)(j->stage + r->off), r->w);
	}
	j->stage_used = 0;
	j->stage_count = 0;
}

static volatile uint32_t vtx_used, vtx_used_max;
static void *submit_main(void *arg)
{
	(void)arg;
	for (int n = 0;; n ^= 1)
	{
		dc_submit_where = 'I';
		sem_wait(&jobs_ready);
		job_t *j = &jobs[n];
		dc_submit_where = 'Y';
		const uint64_t t0 = timer_us_gettime64();
		pvr_wait_ready();             /* (the previous scene has started rendering) */
		const uint64_t t1 = timer_us_gettime64();
		dc_submit_where = 'D';
		if (j->stage_count)
			pvr_wait_render_done();
		dc_submit_where = 'L';   /* (and, before held writes to its textures go in, finished) */
		const uint64_t t2 = timer_us_gettime64();
		flush_stage(j);
		dc_submit_where = 'X';
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);
		if (j->used_op) pvr_prim(list_op[n], j->used_op);
		pvr_list_finish();
		pvr_list_begin(PVR_LIST_PT_POLY);
		if (j->used_pt) pvr_prim(list_pt[n], j->used_pt);
		pvr_list_finish();
		pvr_list_begin(PVR_LIST_TR_POLY);
		if (j->used_tr) pvr_prim(list_tr[n], j->used_tr);
		pvr_list_finish();
		pvr_scene_finish();
		{
			/* (the scene's vertex buffer use, for the overlay: the TA has taken all its data by now) */
			const uint32_t used = PVR_GET(PVR_TA_VERTBUF_POS) - PVR_GET(PVR_TA_VERTBUF_START);
			vtx_used = used;
			if (used > vtx_used_max) vtx_used_max = used;
		}
		submit_ready_us = (uint32_t)(t1 - t0);
		submit_render_us = (uint32_t)(t2 - t1);
		submit_send_us = (uint32_t)(timer_us_gettime64() - t2);
		sem_signal(&jobs_free);
	}
	return NULL;
}

/* (main thread, out of room to hold writes: everything queued sent and rendered, then straight in) */
static void drain(void)
{
	sem_wait(&jobs_free);        /* the other set: sent */
	pvr_wait_ready();
	pvr_wait_render_done();
	sem_signal(&jobs_free);
	flush_stage(&jobs[cur]);
	writes_direct = true;
}

static void mark_used(int t)
{
	if (tex[t].scene != scene_no) { tex[t].prev_scene = tex[t].scene; tex[t].scene = scene_no; }
}

/* (scenes scene_no - 1 to - 3 may be queued or rendering: the submit thread waits for a render to finish
   only before held writes) */
static bool in_flight(int t) { return !writes_direct && tex[t].scene + 3 >= scene_no; }

static void t_write(int t, int x, int y, int w, int h, const uint16_t *src, int stride)
{
	uint16_t *first = (uint16_t *)tex[t].p + (size_t)y * tex[t].w + x;
	const size_t bytes = ((size_t)w * h * 2 + 31) & ~(size_t)31;
	job_t *j = &jobs[cur];
	if (in_flight(t) && (!j->stage || j->stage_count == STAGE_RECS || j->stage_used + bytes > STAGE_BYTES))
	{
		stage_overflows++;
		drain();
	}
	if (!in_flight(t))
	{
		write_now(first, tex[t].w, w, h, src, stride);
		return;
	}
	uint16_t *d = (uint16_t *)(j->stage + j->stage_used);
	for (int k = 0; k < h; k++)
		memcpy(d + (size_t)k * w, src + (size_t)k * stride, (size_t)w * 2);
	j->recs[j->stage_count++] = (stage_rec){ first, (uint32_t)j->stage_used, tex[t].w, (uint16_t)w, (uint16_t)h };
	j->stage_used += bytes;
}

static void *reserve(uint8_t *list, size_t *used, size_t size, size_t bytes)
{
	if (*used + bytes > size) { overflows++; return NULL; }
	void *p = list + *used;
	*used += bytes;
	return p;
}

static void put_quad(pvr_vertex_t *v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
	float z, uint32_t argb, uint32_t oargb)
{
	const float X0 = x0 * SX, X1 = x1 * SX, Y0 = y0 * SY + OY, Y1 = y1 * SY + OY;
	const float px[4] = { X0, X0, X1, X1 }, py[4] = { Y1, Y0, Y1, Y0 };
	const float pu[4] = { u0, u0, u1, u1 }, pv[4] = { v1, v0, v1, v0 };
	for (int i = 0; i < 4; i++)
	{
		v[i].flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		v[i].x = px[i]; v[i].y = py[i]; v[i].z = z;
		v[i].u = pu[i]; v[i].v = pv[i];
		v[i].argb = argb; v[i].oargb = oargb;
	}
}

static void q_textured(int t, bool wrap, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
	float z, uint32_t mod, uint32_t add)
{
	uint8_t *p = reserve(list_pt[cur], &used_pt, PT_BYTES, sizeof(pvr_poly_hdr_t) + 4 * sizeof(pvr_vertex_t));
	if (!p) return;
	const int which = wrap ? 0 : 1;
	if (!(tex[t].compiled & (1 << which)))
	{
		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED, tex[t].w, tex[t].h, tex[t].p,
			PVR_FILTER_NEAREST);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.gen.specular = PVR_SPECULAR_ENABLE;
		cxt.txr.env = PVR_TXRENV_MODULATE;
		cxt.txr.uv_clamp = wrap ? PVR_UVCLAMP_NONE : PVR_UVCLAMP_UV;
		cxt.depth.comparison = PVR_DEPTHCMP_GREATER;
		pvr_poly_compile(&tex[t].hdr[which], &cxt);
		tex[t].compiled |= (uint8_t)(1 << which);
	}
	mark_used(t);
	memcpy(p, &tex[t].hdr[which], sizeof(pvr_poly_hdr_t));
	const float iw = 1.0f / (float)tex[t].w, ih = 1.0f / (float)tex[t].h;
	put_quad((pvr_vertex_t *)(p + sizeof(pvr_poly_hdr_t)), x0, y0, x1, y1, u0 * iw, v0 * ih, u1 * iw, v1 * ih, z,
		mod | 0xff000000u, add & 0x00ffffffu);
}

static void q_shadow(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float z)
{
	uint8_t *p = reserve(list_tr[cur], &used_tr, TR_BYTES, sizeof(pvr_poly_hdr_t) + 4 * sizeof(pvr_vertex_t));
	if (!p) return;
	if (!(tex[t].compiled & 4))
	{
		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED, tex[t].w, tex[t].h, tex[t].p,
			PVR_FILTER_NEAREST);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.txr.env = PVR_TXRENV_MODULATEALPHA;     /* shadow texel (black, alpha 1) x vertex (black, alpha 0.5) */
		cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
		cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvr_poly_compile(&tex[t].hdr[2], &cxt);
		tex[t].compiled |= 4;
	}
	mark_used(t);
	memcpy(p, &tex[t].hdr[2], sizeof(pvr_poly_hdr_t));
	const float iw = 1.0f / (float)tex[t].w, ih = 1.0f / (float)tex[t].h;
	put_quad((pvr_vertex_t *)(p + sizeof(pvr_poly_hdr_t)), x0, y0, x1, y1, u0 * iw, v0 * ih, u1 * iw, v1 * ih, z,
		0x80000000u, 0);
}

/* paletted textures are only written while new (never drawn yet), so straight in */
static uint32_t pal4_writes_in_flight;
static void t_write_pal4(int t, int y, int h, const uint8_t *idx, int stride)
{
	if (in_flight(t)) pal4_writes_in_flight++;
	pal4_write_rows((volatile uint16_t *)tex[t].p, tex[t].w, tex[t].h, y, h, idx, stride);
}

static void pal_set(int bank, const uint16_t colours[16])
{
	for (int i = 0; i < 16; i++)
		pvr_set_pal_entry((uint32_t)(bank * 16 + i), colours[i]);
}

static void q_pal(int t, int bank, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
	float z, uint32_t mod, uint32_t add)
{
	uint8_t *p = reserve(list_pt[cur], &used_pt, PT_BYTES, sizeof(pvr_poly_hdr_t) + 4 * sizeof(pvr_vertex_t));
	if (!p) return;
	if (!(tex[t].compiled & 8))
	{
		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, PVR_TXRFMT_PAL4BPP | PVR_TXRFMT_4BPP_PAL(0) | PVR_TXRFMT_TWIDDLED,
			tex[t].w, tex[t].h, tex[t].p, PVR_FILTER_NEAREST);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.gen.specular = PVR_SPECULAR_ENABLE;
		cxt.txr.env = PVR_TXRENV_MODULATE;
		cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
		cxt.depth.comparison = PVR_DEPTHCMP_GREATER;
		pvr_poly_compile(&tex[t].hdr[3], &cxt);
		tex[t].compiled |= 8;
	}
	mark_used(t);
	memcpy(p, &tex[t].hdr[3], sizeof(pvr_poly_hdr_t));
	((uint32_t *)p)[3] |= PVR_TXRFMT_4BPP_PAL((uint32_t)bank);    /* (the texture word: its palette bank) */
	const float iw = 1.0f / (float)tex[t].w, ih = 1.0f / (float)tex[t].h;
	put_quad((pvr_vertex_t *)(p + sizeof(pvr_poly_hdr_t)), x0, y0, x1, y1, u0 * iw, v0 * ih, u1 * iw, v1 * ih, z,
		mod | 0xff000000u, add & 0x00ffffffu);
}

static uint32_t argb8888(uint16_t c)
{
	const uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
	return 0xff000000u | (r << 19 | r << 14) | (g << 11 | g << 6) | (b << 3 | b >> 2);
}

static void q_solid(uint16_t c, float x0, float y0, float x1, float y1, float z, uint32_t mod, uint32_t add)
{
	uint8_t *p = reserve(list_op[cur], &used_op, OP_BYTES, sizeof(pvr_poly_hdr_t) + 4 * sizeof(pvr_vertex_t));
	if (!p) return;
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.gen.specular = PVR_SPECULAR_ENABLE;
	cxt.depth.comparison = PVR_DEPTHCMP_GREATER;
	pvr_poly_compile((pvr_poly_hdr_t *)p, &cxt);
	/* colour * mod, as the textured quads' modulate does */
	uint32_t col = argb8888(c), out = 0xff000000u;
	for (int i = 0; i < 3; i++)
	{
		const uint32_t ch = (col >> (16 - 8 * i)) & 255, m = (mod >> (16 - 8 * i)) & 255;
		out |= (ch * m / 255) << (16 - 8 * i);
	}
	put_quad((pvr_vertex_t *)(p + sizeof(pvr_poly_hdr_t)), x0, y0, x1, y1, 0, 0, 0, 0, z, out, add & 0x00ffffffu);
}

/* a translucent quad of one colour, after the opaque lists: over what is at or behind z */
static void q_tint(float x0, float y0, float x1, float y1, float z, uint32_t argb)
{
	static pvr_poly_hdr_t hdr;
	static bool compiled;
	uint8_t *p = reserve(list_tr[cur], &used_tr, TR_BYTES, sizeof(pvr_poly_hdr_t) + 4 * sizeof(pvr_vertex_t));
	if (!p) return;
	if (!compiled)
	{
		pvr_poly_cxt_t cxt;
		pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
		cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		pvr_poly_compile(&hdr, &cxt);
		compiled = true;
	}
	memcpy(p, &hdr, sizeof(pvr_poly_hdr_t));
	put_quad((pvr_vertex_t *)(p + sizeof(pvr_poly_hdr_t)), x0, y0, x1, y1, 0, 0, 0, 0, z, argb, 0);
}

static uint64_t clock_us(void) { return timer_us_gettime64(); }
static const rt_backend backend = { t_alloc, t_free, t_write, q_textured, q_shadow, q_solid, clock_us,
	t_alloc_pal4, t_write_pal4, pal_set, q_pal, q_tint };

/* The stats overlay (Start + Y toggles it, dc_overlay_on): two lines of figures over the top of the
   picture, from the BIOS font (12 x 24) drawn at half size. Each figure covers the last 64 frames:
     M  sprites not drawn at all          F  sprites drawn with a stand-in texture
     D  frames that missed a vblank       W  the worst frame, ms
     P  the worst frame's PowerVR work (sprite builds and lists), ms
     V  free texture memory, KiB          U  the worst sprite page unpack, ms
     E  textures out of memory (total)    S  held texture writes that had to drain (total) */
volatile uint32_t dc_build_budget_us;
volatile int dc_safe_mode;
#ifdef DC_TEST_OVERLAY
volatile int dc_overlay_on = 1;    /* (test builds, -DDC_TEST_OVERLAY: on from the start; Start + Y hides it) */
#else
volatile int dc_overlay_on;
#endif
volatile int dc_fps_on;    /* (the console menus' OPTIONS > FPS) */
static const char glyphs[] = "0123456789 MFDWPVUESCNBXG.YQR/ALHZTK";
#define GLYPH_W 12
#define GLYPH_H 24
static int font_tex = -1;
static pvr_poly_hdr_t font_hdr;
static char overlay_line[5][56];

/* the font texture: the BIOS font's 12 x 24 glyphs reduced to 6 x 12, a texel lit where any of its 2 x 2
   pixels is (so the one-pixel strokes survive) */
#define FONT_TW 256
#define FONT_TH 16
static void font_init(void)
{
	const int n = (int)sizeof(glyphs) - 1;
	font_tex = t_alloc(FONT_TW, FONT_TH);
	if (font_tex < 0) return;
	static uint16_t big[GLYPH_H * 512];
	static uint16_t small[FONT_TW * FONT_TH] __attribute__((aligned(32)));
	memset(big, 0, sizeof(big));
	bfont_set_foreground_color(0xffff);
	bfont_set_background_color(0x0000);
	for (int i = 0; i < n; i++)
		bfont_draw(&big[i * GLYPH_W], 512, true, (uint8_t)glyphs[i]);
	for (int y = 0; y < FONT_TH; y++)
		for (int x = 0; x < FONT_TW; x++)
		{
			bool lit = false;
			if (y < GLYPH_H / 2)
				lit = big[(y * 2) * 512 + x * 2] || big[(y * 2) * 512 + x * 2 + 1] ||
					big[(y * 2 + 1) * 512 + x * 2] || big[(y * 2 + 1) * 512 + x * 2 + 1];
			small[y * FONT_TW + x] = lit ? 0xffff : 0x8000;    /* white text on opaque black */
		}
	write_now((uint16_t *)tex[font_tex].p, FONT_TW, FONT_TW, FONT_TH, small, FONT_TW);
	pvr_poly_cxt_t cxt;
	pvr_poly_cxt_txr(&cxt, PVR_LIST_PT_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED, FONT_TW, FONT_TH,
		tex[font_tex].p, PVR_FILTER_NEAREST);
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.txr.env = PVR_TXRENV_REPLACE;
	cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
	cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
	pvr_poly_compile(&font_hdr, &cxt);
}

static void overlay_text(float x, float y, const char *str)
{
	uint8_t *p = reserve(list_pt[cur], &used_pt, PT_BYTES, sizeof(pvr_poly_hdr_t));
	if (!p) return;
	memcpy(p, &font_hdr, sizeof(font_hdr));
	const float gw = GLYPH_W / 2, gh = GLYPH_H / 2;
	for (; *str; str++, x += gw)
	{
		const char *g = strchr(glyphs, *str);
		if (!g) continue;
		const float u0 = (float)(g - glyphs) * gw / (float)FONT_TW, u1 = u0 + gw / (float)FONT_TW;
		pvr_vertex_t *v = reserve(list_pt[cur], &used_pt, PT_BYTES, 4 * sizeof(pvr_vertex_t));
		if (!v) return;
		put_quad(v, x, y, x + gw, y + gh, u0, 0.0f, u1, gh / (float)FONT_TH, 1000000.0f, 0xffffffffu, 0);
	}
}

int dc_pvr_start(void)
{
	pvr_init_params_t params = {
		{ PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_32 },
		256 * 1024,    /* vertex buffer (the console's busiest scenes used under 50 KiB) */
		0, 0, 0,
		/* extra OPBs: many strips and sprites share tiles. (Test images F and G, with 16 here, a 512 or
		   192 KiB vertex buffer and the PowerVR's error events enabled, changed nothing visible and had
		   no music on the console: all back as in image E, which had music) */
		3,
		0
	};
	/* 320 x 240, the board's 320 x 224 1:1 in it: on a VGA box at 480p the video hardware doubles each
	   pixel and each line (sharp 2x); at 240p 60 Hz NTSC timing, on PAL consoles too, through any cable */
	vid_set_mode(dc_video_choice == DC_VIDEO_480P ? DM_320x240_VGA : DM_320x240_NTSC, PM_RGB565);
	if (pvr_init(&params) != 0)
		return -1;
	pvr_set_bg_color(0.0f, 0.0f, 0.0f);
	pvr_set_pal_format(PVR_PAL_ARGB1555);    /* (the paletted sprite textures' banks) */
	for (int i = 0; i < 2; i++) jobs[i].stage = memalign(32, STAGE_BYTES);    /* (without, writes drain) */
	/* all the texture memory left after the frame and vertex buffers, as one pool */
	size_t pool_bytes = pvr_mem_available();
	pool_bytes = pool_bytes > TEX_RESERVE ? (pool_bytes - TEX_RESERVE) & ~(size_t)255 : 0;
	while (pool_bytes >= 1024 * 1024 && !(pool = (uint8_t *)pvr_mem_malloc(pool_bytes)))
		pool_bytes -= 64 * 1024;
	if (pool && va_init((uint32_t)pool_bytes) != 0) pool = NULL;
	rt_init(&backend);
	font_init();
	/* KOS switches to a thread an interrupt woke (the end of a render) at its next scheduler tick: 1 ms
	   instead of the default 10 ms, so the submit thread and the frame follow a render without lag */
	thd_set_hz(1000);
	sem_init(&jobs_free, 2);
	sem_init(&jobs_ready, 0);
	/* above the game's threads, so a finished render is followed up at once */
	kthread_attr_t attr = { 0 };
	attr.stack_size = 16 * 1024;
	attr.prio = PRIO_DEFAULT - 1;
	attr.label = "pvr-submit";
	submitter = thd_create_ex(&attr, submit_main, NULL);
	return submitter ? 0 : -1;
}

/* While the set to record into is still being sent, render_emit builds the frame's new sprite textures
   (fresh textures: written at once, never held) instead of the frame just waiting */
/* Depth compaction (a test, Y + D-pad left; line 3 "Z"): render_emit's depths are priority * 1024 + the
   sprite's place in the list, up to about 130,000 and one apart, so neighbours differ by under one part
   in 100,000. An emulator compares them as exact 32-bit floats; the console's PowerVR works with less
   precision, and sprites that overlap (a logo's pieces, zoomed lettering) could then lose the depth test
   to each other here and there. This replaces each depth with its rank among the frame's depths, as
   1.004 ^ rank: the same order, each step four parts in 1,000. Equal depths stay equal (a shadow and
   its sprite). */
volatile int dc_depth_compact = 0;
#define DEPTH_MAX 4096
static float depth_lut[DEPTH_MAX];
static float depth_seen[DEPTH_MAX];
static int depth_cmp(const void *a, const void *b)
{
	const float x = *(const float *)a, y = *(const float *)b;
	return x < y ? -1 : x > y;
}
static void compact_depths(void)
{
	uint8_t *lists[3] = { list_op[cur], list_pt[cur], list_tr[cur] };
	const size_t used[3] = { used_op, used_pt, used_tr };
	if (depth_lut[0] == 0.0f)
	{
		float v = 1.0f;
		for (int i = 0; i < DEPTH_MAX; i++) { depth_lut[i] = v; v *= 1.004f; if (v > 1.0e6f) v = 1.0e6f; }
	}
	/* the depths in use: each strip's last vertex has its quad's */
	int n = 0;
	float last = -1.0f;
	for (int l = 0; l < 3; l++)
		for (size_t o = 0; o + 32 <= used[l]; o += 32)
		{
			const pvr_vertex_t *v = (const pvr_vertex_t *)(lists[l] + o);
			if (v->flags != PVR_CMD_VERTEX_EOL || v->z == last) continue;
			last = v->z;
			if (n < DEPTH_MAX) depth_seen[n++] = v->z;
		}
	if (n == 0) return;
	qsort(depth_seen, (size_t)n, sizeof(float), depth_cmp);
	int u = 1;
	for (int i = 1; i < n; i++) if (depth_seen[i] != depth_seen[u - 1]) depth_seen[u++] = depth_seen[i];
	for (int l = 0; l < 3; l++)
		for (size_t o = 0; o + 32 <= used[l]; o += 32)
		{
			pvr_vertex_t *v = (pvr_vertex_t *)(lists[l] + o);
			if (v->flags != PVR_CMD_VERTEX && v->flags != PVR_CMD_VERTEX_EOL) continue;
			int lo = 0, hi = u - 1;
			while (lo < hi) { const int mid = (lo + hi) / 2; if (depth_seen[mid] < v->z) lo = mid + 1; else hi = mid; }
			v->z = depth_lut[lo];
		}
}

static bool have_set;
static uint32_t fallback_acc, missing_acc, prepare_acc;
static bool set_still_busy(void)
{
	if (have_set) return false;
	if (sem_trywait(&jobs_free) == 0) { have_set = true; return false; }
	return true;
}

void dc_pvr_frame(const dl_frame *frame)
{
	rt_stats stats;

	const uint64_t t0 = timer_us_gettime64();
	have_set = false;
	dc_where = 'P';
	/* Safe mode (Y + A, a test): every scene sent and finished rendering before this frame's texture
	   writes, so nothing written can be in use by the PowerVR; slower, for telling a timing fault from
	   any other */
	const bool safe = dc_safe_mode;
	if (safe)
	{
		sem_wait(&jobs_free);
		sem_wait(&jobs_free);    /* (both sets sent) */
		pvr_wait_ready();
		pvr_wait_render_done();
		sem_signal(&jobs_free);
		have_set = true;
	}
	else
		rt_prepare(frame, set_still_busy, 12000);
	const uint64_t tp = timer_us_gettime64();
	dc_where = 'W';
	if (!have_set) sem_wait(&jobs_free);
	dc_where = 'R';    /* (a set to record into: the one sent two scenes ago) */
	const uint64_t t1 = timer_us_gettime64();
	dc_pvr_ready_wait_us = (uint32_t)(t1 - tp);
	prepare_acc += (uint32_t)(tp - t0);
	writes_direct = safe;
	used_op = used_pt = used_tr = 0;
	/* Sprite builds get the frame's spare time, found by feedback: a frame that had to wait for its list
	   set (t0 to t1: the game is ahead of the display) had time to spare, and the next may build for
	   longer; a frame that did not wait is late, and builds give way so the game keeps its 60 frames a
	   second (the sound runs a frame at a time with it). Between 1 and 8 ms. */
	static uint64_t last_handover;
	static int32_t budget = RT_BUILD_US;
	{
		const uint32_t waited = (uint32_t)(t1 - t0);
		if (waited > 1500) budget += 500;
		else if (waited < 300) budget -= 1000;
		if (budget < 1000) budget = 1000;
		if (budget > 8000) budget = 8000;
		rt_set_build_limit((uint32_t)budget);
		dc_build_budget_us = (uint32_t)budget;
	}
	rt_frame(frame, &stats);
	{
		/* (overlay line 5, averages over 64 frames in ms: the game and sound since the last frame's
		   hand-over / waiting for the list set / planes / sprites without builds / sprite builds) */
		static uint32_t acc[5], n, late;
		late += stats.plane_cells_late;
		const uint32_t build = stats.build_us_total;
		const uint32_t spr = stats.phase_us[3] + stats.phase_us[4];
		acc[0] += last_handover ? (uint32_t)(t0 - last_handover) : 0;
		acc[1] += (uint32_t)(t1 - t0);
		acc[2] += stats.phase_us[0] + stats.phase_us[1] + stats.phase_us[2];
		acc[3] += spr > build ? spr - build : 0;
		acc[4] += build;
		if (++n == 64)
		{
			/* (then L tile cells carried over, Q the sound board's ms a frame, part of the first figure,
			   and U the misaligned accesses done by hand in the window: each is a trap) */
			extern volatile uint32_t dc_sound_us_total, dc_unaligned_count;
			static uint32_t last_sound, last_unaligned;
			const uint32_t snd = (dc_sound_us_total - last_sound) / 64;
			for (int i = 0; i < 5; i++) acc[i] /= 64;
			snprintf(overlay_line[4], sizeof(overlay_line[4]), "H%u.%u/%u.%u/%u.%u/%u.%u/%u.%u L%u Q%u.%u U%u",
				(unsigned)(acc[0] / 1000), (unsigned)(acc[0] / 100 % 10), (unsigned)(acc[1] / 1000), (unsigned)(acc[1] / 100 % 10),
				(unsigned)(acc[2] / 1000), (unsigned)(acc[2] / 100 % 10), (unsigned)(acc[3] / 1000), (unsigned)(acc[3] / 100 % 10),
				(unsigned)(acc[4] / 1000), (unsigned)(acc[4] / 100 % 10), (unsigned)late,
				(unsigned)(snd / 1000), (unsigned)(snd / 100 % 10), (unsigned)(dc_unaligned_count - last_unaligned));
			last_unaligned = dc_unaligned_count; last_sound = dc_sound_us_total;
			memset(acc, 0, sizeof(acc)); n = 0; late = 0;
		}
	}
	{
		/* the overlay's figures, over each 64 frames */
		static uint64_t last_t;
		static uint32_t w_max, p_max, dropped, ov_missing, ov_fallback, ov_recolour, ov_new, ov_pal, ov_full, ov_pending, ov_deferred, ov_gmin = 99999;
		const uint64_t now = timer_us_gettime64();
		if (last_t)
		{
			const uint32_t dt = (uint32_t)(now - last_t);
			if (dt > w_max) w_max = dt;
			if (dt > 20000) dropped += (dt + 8000) / 16683 - 1;
		}
		last_t = now;
		const uint32_t work = (uint32_t)(now - t0);
		if (work > p_max) p_max = work;
		ov_missing += stats.sprites_missing;
		ov_fallback += stats.sprites_fallback;
		ov_recolour += stats.built_recolour + rt_prepare_stats.built_recolour;
		ov_new += stats.built_new + rt_prepare_stats.built_new;
		ov_pending += stats.sprites_pending + rt_prepare_stats.sprites_pending;
		ov_deferred += stats.sprites_deferred;
		ov_pal += stats.pal_sprites;
		ov_full += stats.pal_banks_full;
		if (dc_build_budget_us < ov_gmin) ov_gmin = dc_build_budget_us;
		if ((scene_no & 63u) == 0u)
		{
			extern uint32_t dc_media_unpack_us_peek(void);
			snprintf(overlay_line[0], sizeof(overlay_line[0]), "M%u F%u D%u W%u.%u P%u.%u V%u G%u", (unsigned)ov_missing,
				(unsigned)ov_fallback, (unsigned)dropped, (unsigned)(w_max / 1000), (unsigned)(w_max / 100 % 10),
				(unsigned)(p_max / 1000), (unsigned)(p_max / 100 % 10), (unsigned)(va_free_bytes() / 1024), (unsigned)(ov_gmin / 1000));
			const uint32_t u = dc_media_unpack_us_peek();
			snprintf(overlay_line[1], sizeof(overlay_line[1]), "C%u N%u B%u X%u U%u.%u E%u S%u", (unsigned)ov_recolour,
				(unsigned)ov_new, (unsigned)ov_pal, (unsigned)ov_full, (unsigned)(u / 1000), (unsigned)(u / 100 % 10), (unsigned)stats.texture_memory_failures,
				(unsigned)stage_overflows);
			{
				pvr_stats_t ps = { 0 };
				pvr_get_stats(&ps);
				extern volatile int dc_safe_mode;
				extern bool rt_pal_enabled;
				snprintf(overlay_line[2], sizeof(overlay_line[2]), "Y%u/%u R%u.%u A%d L%d Z%d",
					(unsigned)(vtx_used / 1024), (unsigned)(vtx_used_max / 1024),
					(unsigned)(ps.rnd_last_time / 1000000), (unsigned)(ps.rnd_last_time / 100000 % 10),
					dc_safe_mode, rt_pal_enabled ? 1 : 0, dc_depth_compact);
			}
			/* (T: sprite builds that waited for an unpack; K: builds that ran out of the frame's time) */
			{
				/* (sound, from aica_pcm.c: A notes started and U notes left silent (sample not mapped or
				   its bank still loading) in the window; L bank loads finished and E load errors in all;
				   then each bank window's page, 9 while it is loading) */
				extern volatile uint32_t dc_aica_stats[8];
				extern int dc_aica_window_state(int window);
				extern volatile uint32_t dc_frames_skipped;
				static uint32_t last_on, last_un, last_skipped;    /* (S at the end: frames not drawn, to keep time) */
				const uint32_t on = dc_aica_stats[0], un = dc_aica_stats[1];
				snprintf(overlay_line[3], sizeof(overlay_line[3]), "T%u K%u A%u U%u L%u E%u W%d.%d S%u", (unsigned)ov_pending,
					(unsigned)ov_deferred, (unsigned)(on - last_on), (unsigned)(un - last_un), (unsigned)dc_aica_stats[2],
					(unsigned)dc_aica_stats[4], dc_aica_window_state(0), dc_aica_window_state(1), (unsigned)(dc_frames_skipped - last_skipped));
				last_on = on; last_un = un; last_skipped = dc_frames_skipped;
			}
			ov_pending = ov_deferred = 0;
			w_max = p_max = dropped = ov_missing = ov_fallback = ov_recolour = ov_new = ov_pal = ov_full = 0; ov_gmin = 99999;
		}
		if (dc_fps_on && font_tex >= 0)
		{
			/* OPTIONS > FPS: the frames drawn in the last second, at the picture's top right */
			static uint64_t since;
			static int count, shown_fps;
			char text[8];
			const uint64_t now = timer_us_gettime64();
			count++;
			if (now - since >= 1000000) { shown_fps = (int)((uint64_t)count * 1000000 / (now - since)); if (since == 0) shown_fps = 0; since = now; count = 0; }
			snprintf(text, sizeof(text), "%2d", shown_fps > 99 ? 99 : shown_fps);
			overlay_text((float)(DL_WIDTH - 16), 2.0f, text);
		}
		if (dc_overlay_on && font_tex >= 0)
		{
			overlay_text(2.0f, 1.0f, overlay_line[0]);
			overlay_text(2.0f, 13.0f, overlay_line[1]);
			overlay_text(2.0f, 25.0f, overlay_line[2]);
			overlay_text(2.0f, 37.0f, overlay_line[3]);
			overlay_text(2.0f, 49.0f, overlay_line[4]);
		}
	}
	if (dc_depth_compact) compact_depths();
	job_t *j = &jobs[cur];
	j->used_op = used_op; j->used_pt = used_pt; j->used_tr = used_tr;
	sem_signal(&jobs_ready);
	last_handover = timer_us_gettime64();
	dc_where = 'H';
	cur ^= 1;
	scene_no++;
	dc_pvr_wait_us = dc_pvr_ready_wait_us;
	dc_pvr_work_us = (uint32_t)(timer_us_gettime64() - t1);
	/* (3, 4, 15: over the last 64 scenes, sprites drawn from a fallback texture, sprites not drawn at
	   all, and the time spent building while waiting, in ms) */
	missing_acc += stats.sprites_missing;
	fallback_acc += stats.sprites_fallback;

	dc_pvr_stats[0] = stats.quads;
	dc_pvr_stats[1] = stats.cells_converted;
	dc_pvr_stats[2] = stats.sprite_textures_built;
	/* (3, 4: the worst sprite build of the last 64 scenes, microseconds and texture texels) */
	if ((scene_no & 63u) == 0u)
	{
		dc_pvr_stats[3] = fallback_acc; dc_pvr_stats[4] = missing_acc;
		{ extern volatile uint32_t dc_unaligned_count; dc_pvr_stats[15] = dc_unaligned_count; }    /* (15: misaligned accesses done by hand) */
		fallback_acc = missing_acc = prepare_acc = 0;
	}
	dc_pvr_stats[5] = stats.texture_memory_failures;
	dc_pvr_stats[6] = overflows;
	dc_pvr_stats[7] = va_free_bytes();
	/* (phases 0-3 as the worst of the last 64 scenes, so short spikes show in the capture) */
	static uint32_t phase_max[4];
	if ((scene_no & 63u) == 0u) { for (int i = 0; i < 4; i++) { dc_pvr_stats[8 + i] = phase_max[i]; phase_max[i] = 0; } }
	for (int i = 0; i < 4; i++) if (stats.phase_us[i] > phase_max[i]) phase_max[i] = stats.phase_us[i];
	/* (13: the worst frame's texture allocation time, 14: the worst sprite-row read, each per 64 scenes) */
	static uint32_t alloc_max, row_max;
	extern volatile uint32_t dc_row_us_frame;
	if (alloc_acc > alloc_max) alloc_max = alloc_acc;
	if (dc_row_us_frame > row_max) row_max = dc_row_us_frame;
	alloc_acc = 0; dc_row_us_frame = 0;
	if ((scene_no & 63u) == 0u) { extern uint32_t dc_media_unpack_us_max(void); dc_pvr_stats[13] = dc_media_unpack_us_max(); alloc_max = row_max = 0; }
	{ extern volatile uint32_t dc_vmu_state; dc_pvr_stats[14] = dc_vmu_state; }    /* (14: the VMU saves, vmu_save.h) */
	(void)submit_render_us;
	dc_pvr_stats[6] = overflows + stage_overflows * 65536u;
}
