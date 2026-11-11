/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Host check of ports/dreamcast/pvr/drawlist.c against runtime/src/video.c. Built into the headless
 * game with -DDL_CHECK (tools/build_host_headless.sh). Each frame the drawlist is rasterised with
 * the rules the PowerVR renderer will use (depth bands from mixer priority, later sprites deeper
 * within a band, point-sampled zoom) and compared pixel by pixel with s32_video_frame(0).
 *   DL_CHECK_OUT=file.csv   per-frame statistics
 *   DL_CHECK_DUMP=dir       BMPs (reference | drawlist | difference) of frames listed in DL_CHECK_FRAMES
 *   DL_CHECK_FRAMES=a,b,c
 */
#include "../../ports/dreamcast/pvr/drawlist.h"
#include <s32/machine.h>
#include <s32/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t out_rgb[DL_HEIGHT][DL_WIDTH];
static uint32_t frames_checked;
static uint64_t total_mismatch, total_order_diff;
static FILE *csv;
static int frame_cross_group;    /* this frame needs the per-group sprite buffer path */

/* groups a sprite's pixels can take: direct sprites one, indirect ones per table entry */
static unsigned sprite_groups(const dl_frame *f, const dl_sprite *s)
{
	if (!s->indirect)
		return 1u << ((s->color >> f->group_shift) & f->group_mask);
	unsigned g = 0;
	for (int i = 0; i < 16; i++)
		if ((s->table[i] & s->transparent_bits) != s->transparent_bits)
			g |= 1u << ((s->table[i] >> f->group_shift) & f->group_mask);
	return g ? g : 1u;
}

static void bounds(const dl_sprite *s, int *l, int *r, int *t, int *b)
{
	*l = s->flip_x ? s->x - s->width + 1 : s->x;
	*r = *l + s->width - 1;
	*t = s->flip_y ? s->y - s->height + 1 : s->y;
	*b = *t + s->height - 1;
	if (*l < s->clip_left) *l = s->clip_left;
	if (*r > s->clip_right) *r = s->clip_right;
	if (*t < s->clip_top) *t = s->clip_top;
	if (*b > s->clip_bottom) *b = s->clip_bottom;
}

/* the PowerVR path decision: depth bands are exact unless sprites of different groups overlap (or one
   sprite holds several groups) */
static int needs_group_buffers(const dl_frame *f)
{
	for (int i = 0; i < f->sprite_count; i++)
	{
		const unsigned gi = sprite_groups(f, &f->sprites[i]);
		if (gi & (gi - 1)) return 1;
		int l1, r1, t1, b1;
		bounds(&f->sprites[i], &l1, &r1, &t1, &b1);
		if (l1 > r1 || t1 > b1) continue;
		for (int j = i + 1; j < f->sprite_count; j++)
		{
			if (sprite_groups(f, &f->sprites[j]) == gi) continue;
			int l2, r2, t2, b2;
			bounds(&f->sprites[j], &l2, &r2, &t2, &b2);
			if (l2 <= r1 && l1 <= r2 && t2 <= b1 && t1 <= b2) return 1;
		}
	}
	return 0;
}

typedef struct { int priority; int group; uint16_t pixel; int order; } spr_frag;
static spr_frag best[DL_HEIGHT][DL_WIDTH];     /* PowerVR model: deepest */
static uint16_t last[DL_HEIGHT][DL_WIDTH];      /* frame buffer model: last written */

static int clamp31(int v) { return v > 31 ? 31 : (v < 0 ? 0 : v); }
static int shift_down(int value, int count) { return value >= 0 ? value >> count : ~(~value >> count); }

static void splat_sprites(const dl_frame *f)
{
	static uint16_t row_buf[64 * 8];
	for (int y = 0; y < DL_HEIGHT; y++)
		for (int x = 0; x < DL_WIDTH; x++) { best[y][x].priority = -1; last[y][x] = 0xffff; }
	for (int i = 0; i < f->sprite_count; i++)
	{
		const dl_sprite *s = &f->sprites[i];
		if (s->shadow) continue;
		int cached_row = -1;
		for (int ky = 0; ky < s->height; ky++)
		{
			const int y = s->flip_y ? s->y - ky : s->y + ky;
			if (y < s->clip_top || y > s->clip_bottom) continue;
			const int row = (int)(((int64_t)ky * s->y_zoom) >> 16);
			const bool out_row = y >= s->out_top && y <= s->out_bottom;
			for (int kx = 0; kx < s->width; kx++)
			{
				const int x = s->flip_x ? s->x - kx : s->x + kx;
				if (x < s->clip_left || x > s->clip_right) continue;
				if (out_row && x >= s->out_left && x <= s->out_right) continue;
				if (row != cached_row) { dl_sprite_decode_row(s, row, row_buf); cached_row = row; }
				const int col = (int)(((int64_t)kx * s->x_zoom) >> 16);
				const uint16_t v = row_buf[col];
				if (v == 0xffff) continue;
				last[y][x] = v;
				const int group = (v >> f->group_shift) & f->group_mask;
				const int pr = f->groups[group].priority;
				if (pr >= best[y][x].priority)
				{
					best[y][x].priority = pr;
					best[y][x].group = group;
					best[y][x].pixel = v;
					best[y][x].order = i;
				}
			}
		}
	}
}

/* video.c: top_layer, with the frame's layers and one sprite entry per group */
typedef struct { const dl_mix *mix; int index; } entry;

static int build_order(const dl_frame *f, int group, entry *order)
{
	int count = 0;
	for (int i = DL_TEXT; i <= DL_BITMAP; i++)
		if (f->layers[i].on) { order[count].mix = &f->layers[i]; order[count].index = i; count++; }
	order[count].mix = &f->layers[DL_BACKDROP]; order[count].index = DL_BACKDROP; count++;
	for (int a = 0; a < count; a++)
		for (int b = a + 1; b < count; b++)
			if (order[b].mix->priority > order[a].mix->priority) { entry t = order[a]; order[a] = order[b]; order[b] = t; }
	/* the sprite group goes above the first layer of lower priority */
	int place = count;
	for (int i = 0; i < count; i++)
		if (f->groups[group].priority > order[i].mix->priority) { place = i; break; }
	for (int i = count; i > place; i--) order[i] = order[i - 1];
	order[place].mix = &f->groups[group];
	order[place].index = DL_SPRITES;
	return count + 1;
}

static uint16_t layer_raw(const dl_frame *f, int index, int x, int y)
{
	const dl_plane *p = &f->planes[index];
	if (index == DL_BACKDROP) return f->backdrop[y];
	if (!p->on || p->sy[y] < 0) return 0;
	bool inside = false;
	for (int i = 0; i < p->span_count[y]; i++)
		if (x >= p->spans[y][i][0] && x < p->spans[y][i][1]) inside = true;
	if (!inside) return 0;
	const int width = (index == DL_TEXT || index == DL_BITMAP) ? 0x1ff : 0x3ff;
	return dl_plane_pixel(index, (p->sx[y] + x) & width, p->sy[y]);
}

static const entry *top(const dl_frame *f, const entry *e, const entry *end, int x, int y, int sprite_pixel, int *pixel, int *shadow)
{
	for (; e < end; e++)
	{
		if (e->index != DL_SPRITES)
		{
			*pixel = layer_raw(f, e->index, x, y) & 0x1fff;
			if (*pixel != 0 || e->index == DL_BACKDROP) return e;
			continue;
		}
		*pixel = sprite_pixel;
		*shadow = ~sprite_pixel & f->shadow_mask;
		if ((sprite_pixel & 0x7fff) != 0x7fff)
		{
			*pixel = sprite_pixel & f->sprite_bits;
			if ((*pixel & 0x7ffe) != f->shadow_pen) return e;
			*shadow = 1;
		}
	}
	return end - 1;
}

static void write_bmp(const char *path, const uint32_t *ref, int pitch)
{
	FILE *o = fopen(path, "wb");
	if (!o) return;
	const int w = DL_WIDTH * 3, h = DL_HEIGHT, row = w * 3;
	const uint32_t size = 54 + (uint32_t)row * h;
	uint8_t hd[54] = { 'B', 'M' };
	hd[2] = size; hd[3] = size >> 8; hd[4] = size >> 16; hd[10] = 54; hd[14] = 40;
	hd[18] = w; hd[19] = w >> 8; hd[22] = h; hd[26] = 1; hd[28] = 24;
	fwrite(hd, 1, 54, o);
	for (int y = h - 1; y >= 0; y--)
		for (int x = 0; x < w; x++)
		{
			uint32_t c;
			const int px = x % DL_WIDTH;
			if (x < DL_WIDTH) c = ref[y * pitch + px];
			else if (x < 2 * DL_WIDTH) c = out_rgb[y][px];
			else c = ((ref[y * pitch + px] & 0xf8f8f8) == (out_rgb[y][px] & 0xf8f8f8)) ? 0 : 0xff00ff;
			const uint8_t bgr[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
			fwrite(bgr, 1, 3, o);
		}
	fclose(o);
}

void dl_check_frame(uint64_t number)
{
	const dl_frame *f = dl_build();
	int width = 0;
	const uint32_t *ref = s32_video_frame(0, &width);
	uint32_t mismatch = 0, order_diff = 0, blended = 0, shadowed = 0;

	if (!csv && getenv("DL_CHECK_OUT"))
	{
		csv = fopen(getenv("DL_CHECK_OUT"), "w");
		if (csv) fprintf(csv, "frame,display,sprites,unsupported,mismatch,order_diff,blended,shadowed,group_buffers\n");
	}
	splat_sprites(f);
	frame_cross_group = needs_group_buffers(f);
	entry orders[4][DL_LAYERS + 1];
	int counts[4] = { 0 };
	for (int g = 0; g <= f->group_mask && g < 4; g++) counts[g] = build_order(f, g, orders[g]);

	for (int y = 0; y < DL_HEIGHT; y++)
		for (int x = 0; x < DL_WIDTH; x++)
		{
			if (!f->display_on) { out_rgb[y][x] = 0; continue; }
			/* group-buffer path: the frame buffer's last-written pixel; else depth order */
			const int sp = frame_cross_group ? last[y][x] : (best[y][x].priority >= 0 ? best[y][x].pixel : 0xffff);
			if ((best[y][x].priority >= 0 ? best[y][x].pixel : 0xffff) != last[y][x]) order_diff++;
			const int group = (sp >> f->group_shift) & f->group_mask;
			const entry *o = orders[group], *end = o + counts[group];
			int shadow = 0, pixel = 0;
			const entry *first = top(f, o, end, x, y, sp, &pixel, &shadow);
			uint16_t color = dl_color(first->mix, pixel);
			int red = (color & 0x1f) + f->offsets[first->mix->offsets][0];
			int green = ((color >> 5) & 0x1f) + f->offsets[first->mix->offsets][1];
			int blue = ((color >> 10) & 0x1f) + f->offsets[first->mix->offsets][2];
			if (first->mix->blend != 0)
			{
				int below = 0;
				const entry *second = top(f, first + 1, end, x, y, sp, &below, &shadow);
				if (((first->mix->blend >> second->index) & 1) && (second->index != DL_SPRITES || ((first->mix->sprite_blend >> group) & 1)))
				{
					const int k = f->blend_factor;
					color = dl_color(second->mix, below);
					red = shift_down(red * (7 - k) + ((color & 0x1f) + f->offsets[second->mix->offsets][0]) * (k + 1), 3);
					green = shift_down(green * (7 - k) + (((color >> 5) & 0x1f) + f->offsets[second->mix->offsets][1]) * (k + 1), 3);
					blue = shift_down(blue * (7 - k) + (((color >> 10) & 0x1f) + f->offsets[second->mix->offsets][2]) * (k + 1), 3);
					blended++;
				}
			}
			if (shadow) { red = shift_down(red, 1); green = shift_down(green, 1); blue = shift_down(blue, 1); shadowed++; }
			out_rgb[y][x] = (uint32_t)clamp31(red) << 19 | (uint32_t)clamp31(green) << 11 | (uint32_t)clamp31(blue) << 3;
			if (out_rgb[y][x] != (ref[y * S32_VIDEO_MAX_WIDTH + x] & 0xf8f8f8)) mismatch++;
		}

	frames_checked++;
	total_mismatch += mismatch;
	total_order_diff += order_diff;
	if (csv)
		fprintf(csv, "%llu,%d,%d,%u,%u,%u,%u,%u,%d\n", (unsigned long long)number, f->display_on, f->sprite_count,
			f->unsupported, mismatch, order_diff, blended, shadowed, frame_cross_group);
	const char *dir = getenv("DL_CHECK_DUMP"), *list = getenv("DL_CHECK_FRAMES");
	if (dir && list)
	{
		char key[32];
		snprintf(key, sizeof(key), ",%llu,", (unsigned long long)number);
		char padded[1024];
		snprintf(padded, sizeof(padded), ",%s,", list);
		if (strstr(padded, key))
		{
			char path[1024];
			snprintf(path, sizeof(path), "%s/dl_%llu.bmp", dir, (unsigned long long)number);
			write_bmp(path, ref, S32_VIDEO_MAX_WIDTH);
		}
	}
}

/* ---- the Dreamcast disc's loading screen, from the player's own ROM (make_disc.py) ----
   SPLASH_DIR=folder SPLASH_BG=frame SPLASH_CAR=frame SPLASH_RECT=l,t,r,b: at frame SPLASH_BG the board's
   picture goes to folder/bg.bmp; at frame SPLASH_CAR the sprites whose place on screen lies inside the
   rectangle are drawn alone, in list order, to folder/car.bmp (magenta where nothing is drawn).
   SPLASH_LIST=1 lists that frame's sprites. Nothing of the ROM is in this tool: it draws what the set
   the player gives it draws. */
static void splash_bmp(const char *path, const uint32_t *rgb, int w, int h, int pitch)
{
	FILE *o = fopen(path, "wb");
	if (!o) return;
	const int row = (w * 3 + 3) & ~3;
	const uint32_t size = 54 + (uint32_t)row * (uint32_t)h;
	uint8_t hd[54] = { 'B', 'M' };
	hd[2] = (uint8_t)size; hd[3] = (uint8_t)(size >> 8); hd[4] = (uint8_t)(size >> 16); hd[10] = 54; hd[14] = 40;
	hd[18] = (uint8_t)w; hd[19] = (uint8_t)(w >> 8); hd[22] = (uint8_t)h; hd[23] = (uint8_t)(h >> 8); hd[26] = 1; hd[28] = 24;
	fwrite(hd, 1, 54, o);
	for (int y = h - 1; y >= 0; y--)
	{
		int n = 0;
		for (int x = 0; x < w; x++, n += 3)
		{
			const uint32_t c = rgb[y * pitch + x];
			const uint8_t bgr[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
			fwrite(bgr, 1, 3, o);
		}
		for (; n < row; n++) fputc(0, o);
	}
	fclose(o);
}

static void splash_frame(const dl_frame *f, uint64_t number)
{
	const char *dir = getenv("SPLASH_DIR");
	if (!dir) return;
	char path[1024];
	if (getenv("SPLASH_BG") && number == strtoull(getenv("SPLASH_BG"), NULL, 10))
	{
		int width;
		const uint32_t *ref = s32_video_frame(0, &width);
		snprintf(path, sizeof(path), "%s/bg.bmp", dir);
		splash_bmp(path, ref, DL_WIDTH, DL_HEIGHT, S32_VIDEO_MAX_WIDTH);
	}
	if (!getenv("SPLASH_CAR") || number != strtoull(getenv("SPLASH_CAR"), NULL, 10)) return;
	int rl = 0, rt = 0, rr = DL_WIDTH, rb = DL_HEIGHT;
	if (getenv("SPLASH_RECT")) sscanf(getenv("SPLASH_RECT"), "%d,%d,%d,%d", &rl, &rt, &rr, &rb);
	static uint32_t canvas[DL_HEIGHT][DL_WIDTH];
	static uint16_t row_buf[64 * 8];
	for (int y = 0; y < DL_HEIGHT; y++) for (int x = 0; x < DL_WIDTH; x++) canvas[y][x] = 0xff00ff;
	for (int i = 0; i < f->sprite_count; i++)
	{
		const dl_sprite *s = &f->sprites[i];
		int l, r, t, b;
		bounds(s, &l, &r, &t, &b);
		if (getenv("SPLASH_LIST"))
			fprintf(stderr, "sprite %d place %06x at %d,%d-%d,%d shadow %d zoom %d,%d\n", i, (unsigned)s->place, l, t, r, b, s->shadow,
				(int)s->x_zoom, (int)s->y_zoom);
		if (s->shadow || l < rl || r > rr || t < rt || b > rb) continue;
		int cached_row = -1;
		for (int ky = 0; ky < s->height; ky++)
		{
			const int y = s->flip_y ? s->y - ky : s->y + ky;
			if (y < 0 || y >= DL_HEIGHT || y < s->clip_top || y > s->clip_bottom) continue;
			const int row = (int)(((int64_t)ky * s->y_zoom) >> 16);
			for (int kx = 0; kx < s->width; kx++)
			{
				const int x = s->flip_x ? s->x - kx : s->x + kx;
				if (x < 0 || x >= DL_WIDTH || x < s->clip_left || x > s->clip_right) continue;
				if (row != cached_row) { dl_sprite_decode_row(s, row, row_buf); cached_row = row; }
				const uint16_t v = row_buf[(int)(((int64_t)kx * s->x_zoom) >> 16)];
				if ((v & 0x7fff) == 0x7fff) continue;
				const int group = (v >> f->group_shift) & f->group_mask;
				const int pixel = v & f->sprite_bits;
				if ((pixel & 0x7ffe) == f->shadow_pen) continue;
				const uint16_t c = dl_color(&f->groups[group], pixel);    /* xBBBBBGGGGGRRRRR */
				const uint32_t r5 = c & 31, g5 = (c >> 5) & 31, b5 = (c >> 10) & 31;
				canvas[y][x] = (r5 << 3 | r5 >> 2) << 16 | (g5 << 3 | g5 >> 2) << 8 | (b5 << 3 | b5 >> 2);
			}
		}
	}
	snprintf(path, sizeof(path), "%s/car.bmp", dir);
	splash_bmp(path, &canvas[0][0], DL_WIDTH, DL_HEIGHT, DL_WIDTH);
}

/* wraps the video's board hooks: the drawlist sees the sprite lists, and is checked after each frame */
#include <s32/program.h>
static void (*video_frame_shown)(void);
static void (*video_sprites_due)(bool, bool);
void rt_check_frame(uint64_t number);
static void check_frame_shown(void)
{
	video_frame_shown();
	if (getenv("SPLASH_DIR")) splash_frame(dl_build(), program_frames());
	else if (getenv("RT_CHECK_OUT")) rt_check_frame(program_frames());
	else dl_check_frame(program_frames());
}
static void check_sprites_due(bool erase, bool draw) { video_sprites_due(erase, draw); dl_sprites_due(erase, draw); }
void dl_install_check_hooks(void)
{
	video_frame_shown = s32_board.frame_shown;
	video_sprites_due = s32_board.sprites_due;
	if (video_frame_shown) s32_board.frame_shown = check_frame_shown;
	if (video_sprites_due) s32_board.sprites_due = check_sprites_due;
}

/* ---- the render_emit path with a host backend that rasterises as the PowerVR does: pixel-centre
   sampling, nearest texel, wrap or clamp, alpha test on bit 15, depth test 'greater' ---- */
#include "../../ports/dreamcast/pvr/render_emit.h"
#include "../../ports/dreamcast/pvr/pal4_twiddle.h"
#include "../../ports/dreamcast/pvr/frame_hash.h"

#define HT_MAX 4096
typedef struct { uint16_t *px; int w, h; bool pal4; } host_tex;
static host_tex htex[HT_MAX];
static size_t ht_bytes, ht_budget = 5u << 20;    /* (texture memory left beside frame buffers and TA) */
static uint32_t rt_rgb[DL_HEIGHT][DL_WIDTH];
static float rt_depth[DL_HEIGHT][DL_WIDTH];

static int ht_alloc(int w, int h)
{
	if (ht_bytes + (size_t)w * h * 2 > ht_budget) return -1;
	for (int i = 0; i < HT_MAX; i++)
		if (!htex[i].px)
		{
			htex[i].px = calloc((size_t)w * h, 2);
			htex[i].w = w; htex[i].h = h;
			ht_bytes += (size_t)w * h * 2;
			return i;
		}
	return -1;
}

/* RT_HOST_PAL4=1: paletted sprite textures, laid out (twiddled) as the console's */
static uint16_t ht_palette[RT_PAL_BANKS * 16];
static int ht_alloc_pal4(int w, int h)
{
	if (ht_bytes + (size_t)w * h / 2 > ht_budget) return -1;
	for (int i = 0; i < HT_MAX; i++)
		if (!htex[i].px)
		{
			htex[i].px = calloc((size_t)w * h / 4, 2);
			htex[i].w = w; htex[i].h = h; htex[i].pal4 = true;
			ht_bytes += (size_t)w * h / 2;
			return i;
		}
	return -1;
}
static void ht_write_pal4(int t, int y, int h, const uint8_t *idx, int stride)
{
	if (!htex[t].pal4 || (y & 1) || (h & 1)) { fprintf(stderr, "bad paletted write tex %d y %d h %d\n", t, y, h); abort(); }
	pal4_write_rows(htex[t].px, htex[t].w, htex[t].h, y, h, idx, stride);
}
static void ht_pal_set(int bank, const uint16_t colours[16]) { memcpy(&ht_palette[bank * 16], colours, 32); }

static void ht_free(int t)
{
	ht_bytes -= htex[t].pal4 ? (size_t)htex[t].w * htex[t].h / 2 : (size_t)htex[t].w * htex[t].h * 2;
	htex[t].pal4 = false;
	free(htex[t].px);
	htex[t].px = NULL;
}

static void ht_write(int t, int x, int y, int w, int h, const uint16_t *src, int stride)
{
	for (int j = 0; j < h; j++)
		memcpy(&htex[t].px[(y + j) * htex[t].w + x], &src[j * stride], (size_t)w * 2);
}

static uint32_t rgb_of(uint16_t a)
{
	return (uint32_t)((a >> 10) & 31) << 19 | (uint32_t)((a >> 5) & 31) << 11 | (uint32_t)(a & 31) << 3;
}

/* texel * mod + add per channel, as the PowerVR's modulate and offset colour do, kept to 5 bits */
static uint32_t shade(uint16_t a, uint32_t mod, uint32_t add)
{
	uint32_t out = 0;
	for (int c = 0; c < 3; c++)
	{
		const int v5 = (a >> (10 - 5 * c)) & 31;
		const int m = (mod >> (16 - 8 * c)) & 255, ad = (add >> (16 - 8 * c)) & 255;
		int v = v5 * 8 * m / 255 + ad;
		v = v > 255 ? 255 : v;
		out |= (uint32_t)(v & 0xf8) << (16 - 8 * c);
	}
	return out;
}

static int quad_bank_now = -1;
static void ht_quad(int t, bool wrap, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float z,
	uint32_t mod, uint32_t add)
{
	const host_tex *tx = &htex[t];
	if (tx->pal4 != (quad_bank_now >= 0)) { fprintf(stderr, "paletted texture %d drawn %s a bank\n", t, tx->pal4 ? "without" : "with"); abort(); }
	for (int y = (int)y0; y < (int)y1; y++)
		for (int x = (int)x0; x < (int)x1; x++)
		{
			if (z <= rt_depth[y][x]) continue;
			const float fx = ((float)x + 0.5f - x0) / (x1 - x0), fy = ((float)y + 0.5f - y0) / (y1 - y0);
			const float bias = getenv("RT_BIAS") ? (float)atof(getenv("RT_BIAS")) : 0.0f;
			int u = (int)floorf(u0 + (u1 - u0) * fx + bias), v = (int)floorf(v0 + (v1 - v0) * fy + bias);
			if (wrap) { u &= tx->w - 1; v &= tx->h - 1; }
			else { u = u < 0 ? 0 : (u >= tx->w ? tx->w - 1 : u); v = v < 0 ? 0 : (v >= tx->h ? tx->h - 1 : v); }
			const uint16_t c = tx->pal4 ? ht_palette[quad_bank_now * 16 + pal4_texel(tx->px, tx->w, tx->h, u, v)] : tx->px[v * tx->w + u];
			{
				static int dx = -2, dy;
				if (dx == -2) { dx = -1; if (getenv("RT_DEBUG_PIXEL")) sscanf(getenv("RT_DEBUG_PIXEL"), "%d,%d", &dx, &dy); }
				if (x == dx && y == dy)
					fprintf(stderr, "px %d,%d tex %d (%dx%d) wrap %d quad %.1f,%.1f-%.1f,%.1f uv %.3f,%.3f-%.3f,%.3f z %.0f -> u %d v %d c %04x\n",
						x, y, t, tx->w, tx->h, wrap, x0, y0, x1, y1, u0, v0, u1, v1, z, u, v, c);
			}
			if (!(c & 0x8000)) continue;
			rt_depth[y][x] = z;
			rt_rgb[y][x] = shade(c, mod, add);
		}
}

static void ht_quad_pal(int t, int bank, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
	float z, uint32_t mod, uint32_t add)
{
	quad_bank_now = bank;
	ht_quad(t, false, x0, y0, x1, y1, u0, v0, u1, v1, z, mod, add);
	quad_bank_now = -1;
}

static void ht_shadow(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float z)
{
	const host_tex *tx = &htex[t];
	for (int y = (int)y0; y < (int)y1; y++)
		for (int x = (int)x0; x < (int)x1; x++)
		{
			if (z < rt_depth[y][x]) continue;
			const float fx = ((float)x + 0.5f - x0) / (x1 - x0), fy = ((float)y + 0.5f - y0) / (y1 - y0);
			int u = (int)floorf(u0 + (u1 - u0) * fx), v = (int)floorf(v0 + (v1 - v0) * fy);
			u = u < 0 ? 0 : (u >= tx->w ? tx->w - 1 : u); v = v < 0 ? 0 : (v >= tx->h ? tx->h - 1 : v);
			{
				static int dx = -2, dy;
				if (dx == -2) { dx = -1; if (getenv("RT_DEBUG_PIXEL")) sscanf(getenv("RT_DEBUG_PIXEL"), "%d,%d", &dx, &dy); }
				if (x == dx && y == dy)
					fprintf(stderr, "shadow px %d,%d tex %d (%dx%d) z %.0f depth %.0f -> u %d v %d c %04x\n", x, y, t, tx->w, tx->h, z,
						rt_depth[y][x], u, v, tx->px[v * tx->w + u]);
			}
			if (!(tx->px[v * tx->w + u] & 0x8000)) continue;
			const uint32_t c = rt_rgb[y][x];
			rt_rgb[y][x] = ((c >> 1) & 0x7c7c7c) & 0xf8f8f8;
		}
}

static void ht_solid(uint16_t c, float x0, float y0, float x1, float y1, float z, uint32_t mod, uint32_t add)
{
	for (int y = (int)y0; y < (int)y1; y++)
		for (int x = (int)x0; x < (int)x1; x++)
			if (z > rt_depth[y][x]) { rt_depth[y][x] = z; rt_rgb[y][x] = shade(c, mod, add); }
}

/* RT_FAKE_CLOCK=1: a clock that runs a millisecond a reading, so a build with RT_BUILD_US spreads every
   sprite texture over frames, CHUNK rows a frame (the console's resumed builds, checked here) */
static uint64_t fake_clock(void) { static uint64_t t; return t += 1000; }
/* translucent quads of one colour: after the frame's opaque quads, as the PowerVR's translucent list */
static struct { float x0, y0, x1, y1, z; uint32_t argb; } tints[64];
static int tint_count;
static void ht_tint(float x0, float y0, float x1, float y1, float z, uint32_t argb)
{
	if (tint_count < 64) { tints[tint_count].x0 = x0; tints[tint_count].y0 = y0; tints[tint_count].x1 = x1; tints[tint_count].y1 = y1;
		tints[tint_count].z = z; tints[tint_count++].argb = argb; }
}
static void ht_tints_flush(void)
{
	for (int i = 0; i < tint_count; i++)
		for (int y = (int)tints[i].y0; y < (int)tints[i].y1; y++)
			for (int x = (int)tints[i].x0; x < (int)tints[i].x1; x++)
			{
				if (y < 0 || y >= DL_HEIGHT || x < 0 || x >= DL_WIDTH || tints[i].z < rt_depth[y][x]) continue;
				const uint32_t a = tints[i].argb >> 24, c = rt_rgb[y][x];
				uint32_t out = 0;
				for (int ch = 0; ch < 3; ch++)
					out |= ((((c >> (8 * ch)) & 255) * (255 - a) + ((tints[i].argb >> (8 * ch)) & 255) * a) / 255 & 0xf8) << (8 * ch);
				rt_rgb[y][x] = out;
			}
	tint_count = 0;
}
static rt_backend host_backend = { ht_alloc, ht_free, ht_write, ht_quad, ht_shadow, ht_solid, NULL, NULL, NULL, NULL, NULL, ht_tint };

void rt_check_frame(uint64_t number)
{
	static bool ready;
	static FILE *out;
	if (!ready) { if (getenv("RT_HOST_BUDGET")) ht_budget = (size_t)strtoul(getenv("RT_HOST_BUDGET"), NULL, 0); if (getenv("RT_FAKE_CLOCK")) host_backend.clock_us = fake_clock;
		if (getenv("RT_HOST_PAL4")) { host_backend.tex_alloc_pal4 = ht_alloc_pal4; host_backend.tex_write_pal4 = ht_write_pal4; host_backend.pal_set = ht_pal_set; host_backend.quad_pal = ht_quad_pal; } rt_init(&host_backend); ready = true; out = fopen(getenv("RT_CHECK_OUT"), "w");
		if (out) fprintf(out, "frame,mismatch,quads,cells,built,hits,evictions,banks_changed,skipped,tex_fail,tex_bytes,offsets,pal_sprites,banks_full\n"); }
#ifdef ORUNNERS_CONSOLE
	{
		/* RT_PAUSE_FRAME=n: frame n as the Dreamcast's pause shows it (the pause's words through dl_overlay) */
		extern size_t console_pause_sprites(uint8_t *list, size_t room, int choice);
		static uint8_t list[0x800];
		if (getenv("RT_PAUSE_FRAME") && number == strtoull(getenv("RT_PAUSE_FRAME"), NULL, 10))
			dl_overlay(list, console_pause_sprites(list, sizeof(list), 0));
		else
			dl_overlay(NULL, 0);
	}
#endif
	const dl_frame *f = dl_build();
	if (number % FRAME_HASH_EVERY == 0 && getenv("RT_HASH_OUT"))
	{
		static FILE *hf;
		if (!hf) hf = fopen(getenv("RT_HASH_OUT"), "w");
		if (hf) { fprintf(hf, "H%llu %08X %08X\n", (unsigned long long)number, frame_hash_board(), frame_hash_list(f)); fflush(hf); }
	}
	rt_stats s;
	for (int y = 0; y < DL_HEIGHT; y++)
		for (int x = 0; x < DL_WIDTH; x++) { rt_depth[y][x] = 0; rt_rgb[y][x] = 0; }
	rt_frame(f, &s);
	ht_tints_flush();
	if (getenv("RT_SPRITE_PAGES"))
	{
		/* 8 KiB pages of sprite data (2048 words) each frame's sprites read */
		static FILE *o;
		if (!o) o = fopen(getenv("RT_SPRITE_PAGES"), "w");
		if (o)
		{
			fprintf(o, "%llu", (unsigned long long)number);
			static uint8_t seen[2048];
			memset(seen, 0, sizeof(seen));
			for (int i = 0; i < f->sprite_count; i++)
			{
				const dl_sprite *sp = &f->sprites[i];
				const uint32_t first = sp->place, last = sp->place + (uint32_t)sp->words * sp->rows;
				for (uint32_t pg = first / 2048; pg <= (last - 1) / 2048 && pg < 2048; pg++)
					if (!seen[pg]) { seen[pg] = 1; fprintf(o, ",%u", pg); }
			}
			fprintf(o, "\n");
		}
	}
	if (getenv("RT_BITMAP_STATS"))
	{
		int rows = 0, pixels = 0;
		for (int y = 0; y < DL_HEIGHT; y++)
			if (f->layers[DL_BITMAP].on && f->planes[DL_BITMAP].span_count[y])
			{
				rows++;
				for (int i = 0; i < f->planes[DL_BITMAP].span_count[y]; i++)
					for (int x = f->planes[DL_BITMAP].spans[y][i][0]; x < f->planes[DL_BITMAP].spans[y][i][1]; x++)
						pixels += dl_plane_pixel(DL_BITMAP, (f->planes[DL_BITMAP].sx[y] + x) & 0x1ff, f->planes[DL_BITMAP].sy[y]) != 0;
			}
		static FILE *b;
		if (!b) b = fopen(getenv("RT_BITMAP_STATS"), "w");
		if (b) fprintf(b, "%llu,%d,%d\n", (unsigned long long)number, rows, pixels);
	}
	int width;
	const uint32_t *ref = s32_video_frame(0, &width);
	uint32_t mismatch = 0;
	for (int y = 0; y < DL_HEIGHT; y++)
		for (int x = 0; x < DL_WIDTH; x++)
			if (rt_rgb[y][x] != (ref[y * S32_VIDEO_MAX_WIDTH + x] & 0xf8f8f8)) mismatch++;
	if (out)
	{
		int offsets = 0;
		for (int i = 0; i < 2; i++) for (int c = 0; c < 3; c++) offsets |= f->offsets[i][c] != 0;
		fprintf(out, "%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%zu,%d,%u,%u\n", (unsigned long long)number, mismatch, s.quads, s.cells_converted,
			s.sprite_textures_built, s.sprite_cache_hits, s.sprite_evictions, s.palette_banks_changed, s.skipped,
			s.texture_memory_failures, ht_bytes, offsets, s.pal_sprites, s.pal_banks_full);
	}
	const char *dir = getenv("DL_CHECK_DUMP"), *list = getenv("DL_CHECK_FRAMES");
	if (dir && list)
	{
		char key[32], padded[1024], path[1024];
		snprintf(key, sizeof(key), ",%llu,", (unsigned long long)number);
		snprintf(padded, sizeof(padded), ",%s,", list);
		if (strstr(padded, key))
		{
			memcpy(out_rgb, rt_rgb, sizeof(out_rgb));
			snprintf(path, sizeof(path), "%s/rt_%llu.bmp", dir, (unsigned long long)number);
			write_bmp(path, ref, S32_VIDEO_MAX_WIDTH);
		}
	}
}
