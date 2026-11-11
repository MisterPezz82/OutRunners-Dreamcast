/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * drawlist.c: see drawlist.h. Register meanings follow runtime/src/video.c (itself MAME's
 * segas32_v.cpp); each function below names the video.c function it mirrors.
 */
#include "drawlist.h"
#include <s32/machine.h>
#include <string.h>

static s32_graphics graphics;
static int (*row_reader)(uint32_t place, int words, uint32_t *out);

void dl_set_sprite_row_reader(int (*reader)(uint32_t, int, uint32_t *)) { row_reader = reader; }
static dl_frame frame;
#define DL_OVERLAY_SPRITES 96
/* (the third list: the frame's own with the overlay's sprites after them, while there is an overlay) */
static dl_sprite lists[2][DL_MAX_SPRITES];
static dl_sprite overlay_list[DL_MAX_SPRITES + DL_OVERLAY_SPRITES];
static dl_sprite overlay_sprites[DL_OVERLAY_SPRITES];
static int overlay_count;
static dl_sprite *decode_into;
static int *decode_count, decode_room;
static int list_count[2];
static int shown_list;
static uint32_t list_unsupported;

static bool bit(uint32_t value, int number) { return (value >> number) & 1; }

static int sign_extend(uint32_t value, int bits)
{
	const uint32_t sign = 1u << (bits - 1);
	return (int)((value & ((sign << 1) - 1)) ^ sign) - (int)sign;
}

static uint16_t video_word(uint32_t index)
{
	uint16_t word;
	memcpy(&word, &s32_board.video_ram[(index & 0xffff) * 2], sizeof(word));
	return word;
}

static uint8_t video_byte(uint32_t address)
{
	return s32_board.video_ram[(address ^ S32_BYTE_LANE) & (S32_VIDEO_RAM_SIZE - 1)];
}

static uint16_t video_register(uint32_t offset) { return video_word((0x1ff00 + offset) / 2); }
static uint16_t mixer_register(uint32_t offset) { return s32_board.mixer[0][offset / 2]; }

static const uint8_t *word_source;    /* (dl_overlay: a list of the port's in place of sprite RAM) */
static size_t word_source_size;

static uint16_t sprite_word(uint32_t index)
{
	uint16_t word;
	if (word_source)
		return (size_t)index * 2 + 1 < word_source_size ? (uint16_t)(word_source[index * 2] | word_source[index * 2 + 1] << 8) : 0;
	memcpy(&word, &s32_board.sprite_ram[(index & 0xffff) * 2], sizeof(word));
	return word;
}

void dl_init(const s32_graphics *g)
{
	graphics = *g;
	memset(list_count, 0, sizeof(list_count));
	shown_list = 0;
}

/* ---- planes (video.c: tile_pen, page_pixel, text_pixel, bitmap_pixel, pages_of) ---- */

static int tile_pen(uint32_t tile, int x, int y)
{
	if (graphics.tile_count == 0)
		return 15;
	const uint32_t index = tile % graphics.tile_count;
	const uint8_t *pens = graphics.tile_at ? graphics.tile_at(index) : (graphics.tile_pens ? graphics.tile_pens[index] : NULL);
	return pens ? pens[y * 16 + x] : 15;
}

static int pages_of(int number, int *pages)
{
	const uint16_t upper = video_register(0x40 + 4 * (uint32_t)number);
	const uint16_t lower = video_register(0x42 + 4 * (uint32_t)number);
	pages[0] = upper & 0x7f;
	pages[1] = (upper >> 8) & 0x7f;
	pages[2] = lower & 0x7f;
	pages[3] = (lower >> 8) & 0x7f;
	return (s32_board.tilebank_external >> (2 * number)) & 3;
}

static uint16_t page_pixel(int page, int bank, int y, int x)
{
	const uint16_t cell = video_word((uint32_t)page << 9 | (uint32_t)(y >> 4) << 5 | (uint32_t)(x >> 4));
	const uint32_t tile = (uint32_t)bank << 13 | (cell & 0x1fff);
	const int column = (cell & 0x4000) ? 15 - (x & 15) : (x & 15);
	const int line = (cell & 0x8000) ? 15 - (y & 15) : (y & 15);
	const int pen = tile_pen(tile, column, line);
	return pen ? (uint16_t)(((cell >> 4) & 0x1ff) << 4 | pen) : 0;
}

static uint16_t text_pixel(int y, int x)
{
	const uint16_t control = video_register(0x5c);
	const uint16_t cell = video_word((uint32_t)((control >> 4) & 0x1f) << 11 | (uint32_t)(y >> 3) << 6 | (uint32_t)(x >> 3));
	const uint32_t character = (uint32_t)(control & 7) << 9 | (cell & 0x1ff);
	const uint8_t value = video_byte(character * 32 + (uint32_t)(y & 7) * 4 + (uint32_t)(x & 7) / 2);
	const int pen = (x & 1) ? (value & 0x0f) : (value >> 4);
	return pen ? (uint16_t)(((cell >> 9) & 0x7f) << 4 | pen) : 0;
}

uint16_t dl_plane_pixel(int layer, int sx, int sy)
{
	switch (layer)
	{
		case DL_NBG0: case DL_NBG1: case DL_NBG2: case DL_NBG3:
		{
			int pages[4];
			const int bank = pages_of(layer - DL_NBG0, pages);
			sx &= 0x3ff;
			sy &= 0x1ff;
			return page_pixel(pages[((sy >> 8) & 1) * 2 + ((sx >> 9) & 1)], bank, sy & 0xff, sx & 0x1ff);
		}
		case DL_TEXT:
			return text_pixel(sy & 0xff, sx & 0x1ff);
		case DL_BITMAP:
		{
			const bool bits8 = bit(video_register(0x00), 11);
			const int color = (video_register(0x8c) << 4) & 0x1fff0 & ~((1 << (bits8 ? 8 : 4)) - 1);
			const int column = sx & 0x1ff;
			int pixel;
			if (bits8)
			{
				pixel = video_byte(512 * (uint32_t)(sy & 0xff) + (uint32_t)column) + color;
				return (pixel & 0xff) ? (uint16_t)pixel : 0;
			}
			pixel = ((video_word(128 * (uint32_t)(sy & 0x1ff) + (uint32_t)column / 4) >> (4 * (column & 3))) & 0x0f) + color;
			return (pixel & 0x0f) ? (uint16_t)pixel : 0;
		}
		default:
			return 0;
	}
}


/* ---- clip windows (video.c: window_of, sort_by_left, merge_windows, line_window, find_clip_runs,
   draw_row), resolved to drawn spans per row ---- */

typedef struct { int left, right, top, bottom; } area;

static void clip_to(area *t, const area *b)
{
	if (b->left > t->left) t->left = b->left;
	if (b->right < t->right) t->right = b->right;
	if (b->top > t->top) t->top = b->top;
	if (b->bottom < t->bottom) t->bottom = b->bottom;
}

static void window_of(int number, area *w)
{
	const uint32_t at = 0x60 + 8 * (uint32_t)number;
	w->left = video_register(at) & 0x1ff;
	w->top = video_register(at + 2) & 0xff;
	w->right = (video_register(at + 4) & 0x1ff) + 1;
	w->bottom = (video_register(at + 6) & 0xff) + 1;
}

static void sort_by_left(const area *windows, int *order)
{
	for (int index = 1; index < 5; index++)
	{
		const int key = order[index];
		int place = index - 1;
		while (place >= 0 && windows[order[place]].left > windows[key].left)
		{
			order[place + 1] = order[place];
			place--;
		}
		order[place + 1] = key;
	}
}

static void merge_windows(uint16_t *list, const area *windows, const int *order, int set, const area *edges)
{
	uint16_t *end = list;
	*end++ = (uint16_t)edges->left;
	for (int index = 0; index < 5; index++)
	{
		const area *window = &windows[order[index]];
		if (!bit((uint32_t)set, order[index]))
			continue;
		if (end != list + 1 && window->left <= end[-1])
		{
			if (window->right > end[-1])
				end[-1] = (uint16_t)window->right;
		}
		else
		{
			*end++ = (uint16_t)window->left;
			*end++ = (uint16_t)window->right;
		}
	}
	*end = (uint16_t)edges->right;
}

static void line_window(int number, int line, uint32_t table, const area *edges, area *window, int *crossing)
{
	const uint16_t left = video_word(table + 0x100 * (uint32_t)(number - 2) + (uint32_t)line);
	const uint16_t right = video_word(table + 0x200 + 0x100 * (uint32_t)(number - 2) + (uint32_t)line);
	if (left == 0xffff || right == 0xffff)
		*crossing &= ~(1 << number);
	else
	{
		window->left = left & 0x1ff;
		window->right = (right & 0x1ff) + 1;
	}
	clip_to(window, edges);
	if (window->left >= window->right)
		*crossing &= ~(1 << number);
}

/* the runs of one row, alternating drawn/cleared from 'drawn', as draw_row walks them, to spans */
static void row_spans(dl_plane *plane, int y, const uint16_t *runs, bool drawn)
{
	uint8_t on[DL_WIDTH];
	memset(on, 0, sizeof(on));
	for (;; runs++, drawn = !drawn)
	{
		for (int x = runs[0]; x < runs[1] && x < DL_WIDTH; x++)
			on[x] = drawn;
		if (runs[1] > DL_WIDTH - 1)
			break;
	}
	int count = 0;
	for (int x = 0; x < DL_WIDTH;)
	{
		if (!on[x]) { x++; continue; }
		int end = x;
		while (end < DL_WIDTH && on[end]) end++;
		if (count < DL_MAX_SPANS) { plane->spans[y][count][0] = (int16_t)x; plane->spans[y][count][1] = (int16_t)end; count++; }
		else frame.unsupported |= DL_UNSUP_SPANS;
		x = end;
	}
	plane->span_count[y] = (uint8_t)count;
}

typedef struct
{
	bool valid;
	uint16_t regs[20];
	uint16_t r00, r04;
	bool outside;
	int selected;
	uint8_t span_count[DL_HEIGHT];
	int16_t spans[DL_HEIGHT][DL_MAX_SPANS][2];
} window_cache;

static window_cache wcache[4];    /* recent results; line windows (per-row tables) are not cached */

static void plane_windows_compute(dl_plane *plane, bool enabled, bool outside, int selected);

static void plane_windows(dl_plane *plane, bool enabled, bool outside, int selected)
{
	if (!enabled)
	{
		plane_windows_compute(plane, enabled, outside, selected);
		return;
	}
	const uint16_t r04 = video_register(0x04);
	if (bit(r04, 4) || bit(r04, 5))
	{
		plane_windows_compute(plane, enabled, outside, selected);
		return;
	}
	uint16_t regs[20];
	for (int i = 0; i < 20; i++)
		regs[i] = video_register(0x60 + 2 * (uint32_t)i);
	const uint16_t r00 = video_register(0x00) & 0x0200;
	for (int c = 0; c < 4; c++)
	{
		window_cache *w = &wcache[c];
		if (w->valid && w->outside == outside && w->selected == selected && w->r00 == r00 && w->r04 == r04 &&
			memcmp(w->regs, regs, sizeof(regs)) == 0)
		{
			memcpy(plane->span_count, w->span_count, sizeof(w->span_count));
			memcpy(plane->spans, w->spans, sizeof(w->spans));
			return;
		}
	}
	plane_windows_compute(plane, enabled, outside, selected);
	static int next;
	window_cache *w = &wcache[next];
	next = (next + 1) & 3;
	w->valid = true;
	memcpy(w->regs, regs, sizeof(regs));
	w->r00 = r00; w->r04 = r04; w->outside = outside; w->selected = selected;
	memcpy(w->span_count, plane->span_count, sizeof(w->span_count));
	memcpy(w->spans, plane->spans, sizeof(w->spans));
}

static void plane_windows_compute(dl_plane *plane, bool enabled, bool outside, int selected)
{
	static uint16_t lists[32][16];
	uint16_t row_list[16];
	const uint16_t line_control = video_register(0x04);
	const bool line_windows = bit(line_control, 4) || bit(line_control, 5);
	const area edges = { 0, DL_WIDTH, 0, DL_HEIGHT };
	area windows[5];
	int order[5];

	if (!enabled)
	{
		for (int y = 0; y < DL_HEIGHT; y++)
		{
			plane->span_count[y] = 1;
			plane->spans[y][0][0] = 0;
			plane->spans[y][0][1] = DL_WIDTH;
		}
		return;
	}
	if (bit(video_register(0x00), 9)) frame.unsupported |= DL_UNSUP_FLIP;
	for (int number = 0; number < 5; number++)
	{
		window_of(number, &windows[number]);
		clip_to(&windows[number], &edges);
		order[number] = number;
	}
	sort_by_left(windows, order);
	lists[0][0] = 0;
	lists[0][1] = DL_WIDTH;
	for (int set = 1; set < 32; set++)
		if (set & selected)
			merge_windows(lists[set], windows, order, set, &edges);
	for (int y = 0; y < DL_HEIGHT; y++)
	{
		int crossing = 0;
		for (int number = 0; number < 5; number++)
			if (bit((uint32_t)selected, number) && y >= windows[number].top && y < windows[number].bottom)
				crossing |= 1 << number;
		if (!line_windows)
		{
			row_spans(plane, y, lists[crossing], outside);
			continue;
		}
		area row_windows[5];
		int row_order[5];
		for (int number = 0; number < 5; number++)
		{
			row_windows[number] = windows[number];
			row_order[number] = number;
		}
		for (int number = 2; number <= 3; number++)
			if (bit(line_control, number + 2) && bit((uint32_t)selected, number))
				line_window(number, y, (uint32_t)(line_control >> 10) * 0x400, &edges, &row_windows[number], &crossing);
		sort_by_left(row_windows, row_order);
		merge_windows(row_list, row_windows, row_order, crossing, &edges);
		row_spans(plane, y, row_list, outside);
	}
}

/* video.c: flips_of */
static bool layer_flipped(int number)
{
	const uint16_t control = video_register(0x00);
	return bit(control, 9) || bit(control, number);
}

/* video.c: draw_zoomed (rows only, zoom 0x200 exact; other zooms marked unsupported) */
static void zoomed_rows(int number, dl_plane *plane)
{
	const uint32_t scroll = 0x10 + 8 * (uint32_t)number;
	const uint32_t center = 0x30 + 4 * (uint32_t)number;
	int x_zoom = video_register(0x50 + 4 * (uint32_t)number) & 0xfff;
	int y_zoom = bit(video_register(0x00), 14) ? video_register(0x52 + 4 * (uint32_t)number) & 0xfff : x_zoom;
	if (x_zoom < 0x80) x_zoom = 0x80;
	if (y_zoom < 0x80) y_zoom = 0x80;
	if (x_zoom != 0x200 || y_zoom != 0x200) frame.unsupported |= DL_UNSUP_ZOOM;
	if (layer_flipped(number)) frame.unsupported |= DL_UNSUP_FLIP;
	plane_windows(plane, bit(video_register(0x02), 11 + number), bit(video_register(0x02), 6 + number),
		(video_register(0x06) >> (4 * number)) & 0x0f);
	const uint32_t x_step = (0x200u << 20) / (uint32_t)x_zoom;
	const uint32_t y_step = (0x200u << 20) / (uint32_t)y_zoom;
	uint32_t x_start = (uint32_t)(video_register(scroll + 2) & 0x3ff) << 20;
	uint32_t source_y = (uint32_t)(video_register(scroll + 6) & 0x1ff) << 20;
	x_start += (uint32_t)(video_register(scroll) & 0xff00) << 4;
	source_y += (uint32_t)(video_register(scroll + 4) & 0xfe00) << 4;
	x_start -= (uint32_t)sign_extend(video_register(center), x_zoom != 0x200 ? 10 : 9) * x_step;
	source_y -= (uint32_t)sign_extend(video_register(center + 2), y_zoom != 0x200 ? 10 : 9) * y_step;
	for (int y = 0; y < DL_HEIGHT; y++)
	{
		const uint32_t sy = source_y + (uint32_t)y * y_step;
		plane->sx[y] = (int16_t)((x_start >> 20) & 0x3ff);
		plane->sy[y] = (int16_t)((sy >> 20) & 0x1ff);
	}
}

/* video.c: draw_scrolled / draw_scrolled_rows */
static void scrolled_rows(int number, dl_plane *plane)
{
	const uint16_t line_control = video_register(0x04);
	const bool line_windows = bit(line_control, number + 2);
	const uint32_t table = (uint32_t)(line_control >> 10) * 0x400 + 0x100 * (uint32_t)(number - 2);
	const bool row_scroll = bit(line_control, number - 2) && !line_windows;
	const bool row_select = bit(line_control, number) && !line_windows;
	const int x_scroll = (video_register(0x12 + 8 * (uint32_t)number) & 0x3ff) - (video_register(0x30 + 4 * (uint32_t)number) & 0x1ff);
	const int y_scroll = video_register(0x16 + 8 * (uint32_t)number) & 0x1ff;
	if (line_windows) frame.unsupported |= DL_UNSUP_LINEWIN;
	if (layer_flipped(number)) frame.unsupported |= DL_UNSUP_FLIP;
	plane_windows(plane, bit(video_register(0x02), 11 + number), bit(video_register(0x02), 6 + number),
		(video_register(0x06) >> (4 * number)) & 0x0f);
	for (int y = 0; y < DL_HEIGHT; y++)
	{
		int source_y = y_scroll + y;
		int x_start = x_scroll;
		if (row_scroll)
			x_start += video_word(table + (uint32_t)y) & 0x3ff;
		if (row_select)
			source_y = (y_scroll + video_word(table + 0x200 + (uint32_t)y)) & 0x1ff;
		plane->sx[y] = (int16_t)(x_start & 0x3ff);
		plane->sy[y] = (int16_t)(source_y & 0x1ff);
	}
}

/* ---- the mixer (video.c: color_offsets, sprite_blend, groupings, mix) ---- */

static uint8_t color_offsets(bool layer_bit, bool layer_flag)
{
	switch ((bit(mixer_register(0x3e), 15) ? 2 : 0) | (layer_bit ? 1 : 0))
	{
		case 1: return 2;
		case 2: return layer_flag ? 0 : 2;
		default: return layer_flag ? 0 : 1;
	}
}

static uint16_t sprite_blend(int encoding)
{
	const int value = encoding & 0x0f;
	switch ((encoding >> 4) & 3)
	{
		case 0: return (uint16_t)(1 << value);
		case 1: return (uint16_t)((1 << value) | ((1 << value) - 1));
		case 2: return (uint16_t)(~((1 << value) - 1) & 0xffff);
		default: return 0xffff;
	}
}

static const uint8_t grouping_table[16][3] = {
	{ 14, 0x00, 0x01 }, { 14, 0x01, 0x02 }, { 13, 0x03, 0x04 }, { 12, 0x07, 0x08 },
	{ 14, 0x01, 0x00 }, { 13, 0x03, 0x00 }, { 12, 0x07, 0x00 }, { 11, 0x0f, 0x00 },
	{ 14, 0x01, 0x00 }, { 13, 0x03, 0x00 }, { 12, 0x07, 0x00 }, { 11, 0x0f, 0x00 },
	{ 13, 0x01, 0x00 }, { 12, 0x03, 0x00 }, { 11, 0x07, 0x00 }, { 10, 0x0f, 0x00 }
};

uint16_t dl_color(const dl_mix *entry, int pixel)
{
	return s32_board.palette[0][(entry->palette_base + ((pixel >> entry->shift) & 0xfff0) + (pixel & 0x0f)) & 0x3fff];
}

/* ---- sprites (video.c: s32_video_sprites, draw_sprite_list, draw_sprite) ---- */

static void clip_area(int16_t *l, int16_t *r, int16_t *t, int16_t *b)
{
	if (*l < 0) *l = 0;
	if (*r > DL_WIDTH - 1) *r = DL_WIDTH - 1;
	if (*t < 0) *t = 0;
	if (*b > DL_HEIGHT - 1) *b = DL_HEIGHT - 1;
}

static int decode_sprite(uint32_t entry, int x_offset, int y_offset, const int16_t inside[4], const int16_t outside[4], int target)
{
	static const uint16_t transparent_bits[4][4] = {
		{ 0x7fff, 0x3fff, 0x1fff, 0x0fff },
		{ 0x3fff, 0x1fff, 0x0fff, 0x07ff },
		{ 0x3fff, 0x1fff, 0x0fff, 0x07ff },
		{ 0x1fff, 0x0fff, 0x07ff, 0x03ff }
	};
	const uint8_t *latched = s32_board.sprite_control_latched;
	const uint16_t flags = sprite_word(entry);
	const uint16_t size = sprite_word(entry + 1);
	const uint16_t height_word = sprite_word(entry + 2);
	const uint16_t width_word = sprite_word(entry + 3);
	const bool table_inline = bit(flags, 12);
	const bool from_ram = bit(flags, 10);
	const int source_height = size >> 8;
	const int height = height_word & 0x3ff;
	const int width = width_word & 0x3ff;
	const uint32_t bank = ((width_word & 0x2000) >> 13 | (width_word & 0x8000) >> 14) % (graphics.sprite_banks ? graphics.sprite_banks : 1);
	const int screen = bit(width_word, 11);
	const int taken = (bit(flags, 13) && table_inline) ? 2 : 0;
	dl_sprite s;

	if (screen != 0)
		return taken;
	memset(&s, 0, sizeof(s));
	s.indirect = bit(flags, 13);
	s.shadow = bit(flags, 11) && bit(latched[5], 0);
	s.extra = s32_video_extra_used() && (size & S32_SPRITE_EXTRA_PALETTE) != 0;
	s.bits8 = bit(flags, 9);
	s.color = (uint16_t)(0x8000 | (sprite_word(entry + 7) & (s.bits8 ? 0x7f00 : 0x7ff0)));
	const int source_width = s.bits8 ? (size & 0x3f) : ((size >> 1) & 0x3f);
	s.end_pen = (uint8_t)(bit(flags, 8) ? 0 : (s.bits8 ? 0xff : 0x0f));
	if (source_width == 0 || source_height == 0 || width == 0 || height == 0)
		return taken;
	if (from_ram)
	{
		list_unsupported |= DL_UNSUP_SPR_RAM;
		return taken;
	}
	s.transparent_bits = transparent_bits[latched[4] & 3][latched[5] & 3];
	if (s.bits8)
		s.transparent_bits &= 0xfff0;
	if (s.indirect)
	{
		const uint32_t table = table_inline ? entry + 8 : 8 * (uint32_t)(sprite_word(entry + 7) & 0x1fff);
		for (int index = 0; index < 16; index++)
			s.table[index] = (uint16_t)((sprite_word(table + (uint32_t)index) & (s.bits8 ? 0xfff0 : 0xffff)) |
				(bit(latched[5], 0) ? 0x8000 : 0));
	}
	s.place = bank << 20 | ((sprite_word(entry + 6) | (uint32_t)(height_word & 0xf000) << 4) & 0xfffff);
	s.words = (uint16_t)source_width;
	s.rows = (uint16_t)source_height;
	s.width = (int16_t)width;
	s.height = (int16_t)height;
	s.x_zoom = (((s.bits8 ? 4 : 8) * source_width) << 16) / width;
	s.y_zoom = (source_height << 16) / height;
	s.flip_x = bit(flags, 6);
	s.flip_y = bit(flags, 7);

	int x = sign_extend(sprite_word(entry + 5), 12) + (bit(flags, 4) ? x_offset : 0);
	int y = sign_extend(sprite_word(entry + 4), 12) + (bit(flags, 5) ? y_offset : 0);
	switch (flags & 3)
	{
		case 0: case 3: x -= (width - 1) / 2; break;
		case 1: x -= width - 1; break;
		default: break;
	}
	switch ((flags >> 2) & 3)
	{
		case 0: case 3: y -= (height - 1) / 2; break;
		case 1: y -= height - 1; break;
		default: break;
	}
	if (s.flip_x) x += width - 1;
	if (s.flip_y) y += height - 1;
	s.x = (int16_t)x;
	s.y = (int16_t)y;
	s.clip_left = inside[0]; s.clip_right = inside[1]; s.clip_top = inside[2]; s.clip_bottom = inside[3];
	s.out_left = outside[0]; s.out_right = outside[1]; s.out_top = outside[2]; s.out_bottom = outside[3];
	if (decode_into)
	{
		if (*decode_count < decode_room)
			decode_into[(*decode_count)++] = s;
	}
	else if (list_count[target] < DL_MAX_SPRITES)
		lists[target][list_count[target]++] = s;
	return taken;
}

static void decode_list(int target)
{
	int number = 0;
	int16_t inside[4] = { 0, DL_WIDTH - 1, 0, DL_HEIGHT - 1 };
	int16_t outside[4] = { 0, -1, 0, -1 };
	int x_offset = 0, y_offset = 0;

	if (bit(s32_board.sprite_control_latched[6], 0))
		list_unsupported |= DL_UNSUP_WIDE;
	for (int entries = 0; entries < 0x20000 / 16; entries++)
	{
		const uint32_t entry = 8 * (uint32_t)(number & 0x1fff);
		const uint16_t command = sprite_word(entry);
		switch (command >> 14)
		{
			case 0:
				number += 1 + decode_sprite(entry, x_offset, y_offset, inside, outside, target);
				break;
			case 1:
				if (bit(command, 12))
				{
					inside[0] = (int16_t)sign_extend(sprite_word(entry + 2), 12);
					inside[1] = (int16_t)sign_extend(sprite_word(entry + 3), 12);
					inside[2] = (int16_t)sign_extend(command, 12);
					inside[3] = (int16_t)sign_extend(sprite_word(entry + 1), 12);
					clip_area(&inside[0], &inside[1], &inside[2], &inside[3]);
				}
				if (bit(command, 13))
				{
					outside[0] = (int16_t)sign_extend(sprite_word(entry + 6), 12);
					outside[1] = (int16_t)sign_extend(sprite_word(entry + 7), 12);
					outside[2] = (int16_t)sign_extend(sprite_word(entry + 4), 12);
					outside[3] = (int16_t)sign_extend(sprite_word(entry + 5), 12);
				}
				number++;
				break;
			case 2:
				if (bit(command, 13))
				{
					y_offset = sign_extend(sprite_word(entry + 1), 12);
					x_offset = sign_extend(sprite_word(entry + 2), 12);
				}
				number = command & 0x1fff;
				break;
			default:
				return;
		}
	}
}

void dl_overlay(const uint8_t *list, size_t size)
{
	const int16_t inside[4] = { 0, DL_WIDTH - 1, 0, DL_HEIGHT - 1 };
	const int16_t outside[4] = { 0, -1, 0, -1 };

	overlay_count = 0;
	if (!list)
		return;
	word_source = list;
	word_source_size = size;
	decode_into = overlay_sprites;
	decode_count = &overlay_count;
	decode_room = DL_OVERLAY_SPRITES;
	for (uint32_t number = 0; (number + 1) * 16 <= size; number++)
	{
		if ((sprite_word(8 * number) >> 14) != 0)
			break;    /* (the end; the pause's lists have no other commands) */
		number += (uint32_t)decode_sprite(8 * number, 0, 0, inside, outside, 0);
	}
	word_source = NULL;
	decode_into = NULL;
}

#ifdef S32_VIDEO_EXTRA_ELSEWHERE
/* the extra palette (video.h), kept here: the left screen's alone */
static bool extra_used;
static uint16_t extra_palette[S32_PALETTE_ENTRIES];
static uint8_t extra_set[S32_PALETTE_ENTRIES / 8];

void s32_video_use_extra_palette(bool used) { extra_used = used; }
bool s32_video_extra_used(void) { return extra_used; }

void s32_video_extra_colour(int screen, uint32_t index, uint16_t colour)
{
	if (screen != 0)
		return;
	index &= S32_PALETTE_ENTRIES - 1;
	extra_palette[index] = colour;
	extra_set[index >> 3] |= (uint8_t)(1u << (index & 7));
}

bool s32_video_extra_at(int screen, uint32_t index, uint16_t *colour)
{
	index &= S32_PALETTE_ENTRIES - 1;
	if (screen != 0 || !(extra_set[index >> 3] & (1u << (index & 7))))
		return false;
	*colour = extra_palette[index];
	return true;
}
#endif

void dl_sprites_due(bool erase, bool draw)
{
	if (erase)
		list_count[shown_list] = 0;
	if (draw)
	{
		shown_list ^= 1;
		if (!erase)
			list_unsupported |= DL_UNSUP_NO_ERASE;
		decode_list(shown_list ^ 1);
	}
}

/* video.c: draw_sprite_row, for one source row: the pixel each pen position puts in the buffer */
bool dl_sprite_row_words(const dl_sprite *s, int row, uint32_t *words)
{
	const uint32_t bank = s->place >> 20;
	const uint32_t address = (s->place & 0xfffff) + (uint32_t)(s->words * row);
	int got = 0;
	if (row_reader && ((address & 0xfffff) + (uint32_t)s->words <= 0x100000))
		got = row_reader((bank << 20) | (address & 0xfffff), s->words, words);
	if (got < 0)
		return false;
	if (got == 0)
		for (int w = 0; w < s->words; w++)
			words[w] = graphics.sprite_word((bank << 20) | ((address + (uint32_t)w) & 0xfffff));
	return true;
}

bool dl_sprite_decode_row(const dl_sprite *s, int row, uint16_t *out)
{
	const int bits = s->bits8 ? 8 : 4;
	const int pens = 32 / bits;
	const int pen_mask = (1 << bits) - 1;
	const int total = s->words * pens;
	uint32_t address = s->place & 0xfffff;
	const uint32_t bank = s->place >> 20;
	int position = 0;

	uint32_t words[64];
	address += (uint32_t)(s->words * row);
	for (int i = 0; i < total; i++)
		out[i] = 0xffff;
	int got = 0;
	if (row_reader && ((address & 0xfffff) + (uint32_t)s->words <= 0x100000))
		got = row_reader((bank << 20) | (address & 0xfffff), s->words, words);
	if (got < 0)
		return false;    /* (the data is on its way) */
	const bool whole = got > 0;
	for (int w = 0; w < s->words; w++)
	{
		const uint32_t word = whole ? words[w] : graphics.sprite_word((bank << 20) | ((address + (uint32_t)w) & 0xfffff));
		int pen = 0;
		for (int index = 0; index < pens; index++, position++)
		{
			const int skipped = (index == 0 || index == pens - 1) ? s->end_pen : 0;
			int pixel;
			pen = (int)(word >> (32 - bits * (index + 1))) & pen_mask;
			if (pen == skipped)
				continue;
			if (!s->indirect)
			{
				if (pen == 0)
					continue;
				pixel = s->color | pen;
			}
			else
			{
				pixel = s->bits8 ? s->table[pen >> 4] | (pen & 0x0f) : s->table[pen];
				if ((pixel & s->transparent_bits) == s->transparent_bits)
					continue;
			}
			out[position] = (uint16_t)pixel;
		}
		if (s->end_pen != 0 && pen == pen_mask)
			break;
	}
	return true;
}

/* ---- the frame (video.c: s32_video_draw_frame, draw_layers, draw_backdrop, mix) ---- */

const dl_frame *dl_build(void)
{
	const uint16_t control = video_register(0x00);
	const uint16_t off = video_register(0x02);
	const uint16_t more_off = video_register(0x8e);
	int wanted = 0, drawn = 0;

	memset(&frame, 0, sizeof(frame));
	frame.display_on = s32_display_enabled(0);
	frame.unsupported = list_unsupported;
	if (bit(control, 15)) frame.unsupported |= DL_UNSUP_WIDE;
	if (s32_board.sprite_control_latched[2] & 3) frame.unsupported |= DL_UNSUP_SPR_FLIPS;
	for (int index = DL_TEXT; index <= DL_BITMAP; index++)
		if ((mixer_register(0x20 + 2 * (uint32_t)index) & 0x0f) != 0)
			wanted |= 1 << index;

	if (bit((uint32_t)wanted, DL_NBG0) && !bit(off, 0) && !bit(more_off, 1)) { zoomed_rows(0, &frame.planes[DL_NBG0]); drawn |= 1 << DL_NBG0; }
	if (bit((uint32_t)wanted, DL_NBG1) && !bit(off, 1) && !bit(more_off, 2)) { zoomed_rows(1, &frame.planes[DL_NBG1]); drawn |= 1 << DL_NBG1; frame.unsupported |= DL_UNSUP_NBG13; }
	if (bit((uint32_t)wanted, DL_NBG2) && !bit(off, 2) && !bit(more_off, 3) && !bit(control, 12)) { scrolled_rows(2, &frame.planes[DL_NBG2]); drawn |= 1 << DL_NBG2; }
	if (bit((uint32_t)wanted, DL_NBG3) && !bit(off, 3) && !bit(more_off, 4) && !bit(control, 13)) { scrolled_rows(3, &frame.planes[DL_NBG3]); drawn |= 1 << DL_NBG3; frame.unsupported |= DL_UNSUP_NBG13; }
	if (bit((uint32_t)wanted, DL_TEXT) && !bit(off, 4) && !bit(more_off, 0))
	{
		if (bit(control, 9)) frame.unsupported |= DL_UNSUP_FLIP;
		for (int y = 0; y < DL_HEIGHT; y++) { frame.planes[DL_TEXT].sx[y] = 0; frame.planes[DL_TEXT].sy[y] = (int16_t)y; }
		plane_windows(&frame.planes[DL_TEXT], false, false, 0);
		drawn |= 1 << DL_TEXT;
	}
	if (bit((uint32_t)wanted, DL_BITMAP) && !bit(off, 5) && !bit(more_off, 5))
	{
		const bool bits8 = bit(control, 11);
		const int x_scroll = video_register(0x88) & 0x1ff;
		const int y_scroll = video_register(0x8a) & 0x1ff;
		plane_windows(&frame.planes[DL_BITMAP], bit(off, 15), bit(off, 10), 0x10);
		for (int y = 0; y < DL_HEIGHT; y++)
		{
			frame.planes[DL_BITMAP].sx[y] = (int16_t)x_scroll;
			frame.planes[DL_BITMAP].sy[y] = (int16_t)((y + y_scroll) & (bits8 ? 0xff : 0x1ff));
		}
		drawn |= 1 << DL_BITMAP;
	}
	for (int index = DL_TEXT; index <= DL_BITMAP; index++)
		frame.planes[index].on = bit((uint32_t)drawn, index);

	{
		const uint16_t backdrop = video_register(0x5e);
		if (bit(control, 9)) frame.unsupported |= DL_UNSUP_FLIP;
		for (int y = 0; y < DL_HEIGHT; y++)
			frame.backdrop[y] = (uint16_t)((backdrop & 0x1e00) + (bit(backdrop, 15) ? (backdrop + y) & 0x1ff : 0));
	}

	/* the mixer */
	{
		const uint16_t blend_control = mixer_register(0x4e);
		const uint16_t sprite_control = mixer_register(0x4c);
		frame.blending = bit(blend_control, 11);
		frame.blend_factor = (blend_control >> 8) & 7;
		frame.group_shift = grouping_table[sprite_control & 0x0f][0];
		frame.group_mask = grouping_table[sprite_control & 0x0f][1];
		frame.sprite_bits = (uint16_t)(((1 << frame.group_shift) - 1) & 0x3fff);
		frame.shadow_mask = bit(sprite_control, 2) ? 0x8000 : 0;
		frame.shadow_pen = 0x7ffe & frame.sprite_bits;
		for (int channel = 0; channel < 3; channel++)
		{
			frame.offsets[0][channel] = sign_extend(mixer_register(0x40 + 2 * (uint32_t)channel), 6);
			frame.offsets[1][channel] = sign_extend(mixer_register(0x46 + 2 * (uint32_t)channel), 6);
			frame.offsets[2][channel] = 0;
		}
		for (int index = DL_TEXT; index <= DL_BITMAP; index++)
		{
			const uint16_t c = mixer_register(0x20 + 2 * (uint32_t)index);
			const uint16_t b = mixer_register(0x30 + 2 * (uint32_t)index);
			dl_mix *m = &frame.layers[index];
			m->on = bit((uint32_t)drawn, index) && (c & 0x0f) != 0;
			m->priority = (uint8_t)((c & 0x0f) << 3 | (6 - index));
			m->palette_base = (uint16_t)((c & 0x00f0) << 6);
			m->shift = (c >> 8) & 3;
			m->blend = frame.blending ? (uint8_t)((b >> 6) & 0xff) : 0;
			m->sprite_blend = sprite_blend(b & 0x3f);
			m->offsets = color_offsets(bit(mixer_register(0x3e), index), bit(b, 14));
		}
		{
			dl_mix *m = &frame.layers[DL_BACKDROP];
			m->on = true;
			m->priority = 1 << 3;
			m->palette_base = (uint16_t)((mixer_register(0x2c) & 0x00f0) << 6);
			m->shift = (mixer_register(0x2c) >> 8) & 3;
			m->offsets = color_offsets(bit(mixer_register(0x3e), 8), bit(mixer_register(0x3e), 14));
		}
		for (int group = 0; group <= frame.group_mask; group++)
		{
			const uint16_t c = mixer_register(2 * (uint32_t)(grouping_table[sprite_control & 0x0f][2] | group));
			dl_mix *m = &frame.groups[group];
			m->on = true;
			m->priority = (uint8_t)((c & 0x0f) << 3 | 7);
			m->palette_base = (uint16_t)((((sprite_control & 3) != 3) ? c : sprite_control) & 0x00f0) << 6;
			m->shift = (c >> 8) & 3;
			m->offsets = color_offsets(bit(mixer_register(0x3e), 6), bit(sprite_control, 15));
		}
	}
	frame.sprites = lists[shown_list];
	frame.sprite_count = list_count[shown_list];
	frame.next_sprites = lists[shown_list ^ 1];
	frame.next_sprite_count = list_count[shown_list ^ 1];
	frame.overlay_from = -1;
	if (overlay_count)
	{
		memcpy(overlay_list, lists[shown_list], (size_t)frame.sprite_count * sizeof(dl_sprite));
		memcpy(&overlay_list[frame.sprite_count], overlay_sprites, (size_t)overlay_count * sizeof(dl_sprite));
		frame.sprites = overlay_list;
		frame.overlay_from = frame.sprite_count;
		frame.sprite_count += overlay_count;
		frame.next_sprite_count = 0;
	}
	return &frame;
}

/* ---- cell access for texture builders ---- */

uint16_t dl_plane_cell(int layer, int cx, int cy, int *bank)
{
	int pages[4];
	*bank = pages_of(layer - DL_NBG0, pages);
	const int page = pages[((cy >> 4) & 1) * 2 + ((cx >> 5) & 1)];
	return video_word((uint32_t)page << 9 | (uint32_t)(cy & 15) << 5 | (uint32_t)(cx & 31));
}

const uint8_t *dl_tile_pens(uint32_t tile)
{
	if (graphics.tile_count == 0)
		return NULL;
	const uint32_t index = tile % graphics.tile_count;
	return graphics.tile_at ? graphics.tile_at(index) : (graphics.tile_pens ? graphics.tile_pens[index] : NULL);
}

uint16_t dl_text_cell(int cx, int cy, uint32_t *character)
{
	const uint16_t control = video_register(0x5c);
	const uint16_t cell = video_word((uint32_t)((control >> 4) & 0x1f) << 11 | (uint32_t)(cy & 31) << 6 | (uint32_t)(cx & 63));
	*character = (uint32_t)(control & 7) << 9 | (cell & 0x1ff);
	return cell;
}

uint32_t dl_video_hash(uint32_t byte_address, uint32_t bytes)
{
	uint32_t h = 2166136261u;
	for (uint32_t i = 0; i < bytes; i++)
		h = (h ^ s32_board.video_ram[(byte_address + i) & (S32_VIDEO_RAM_SIZE - 1)]) * 16777619u;
	return h;
}

void dl_plane_pages(int layer, int *pages, int *bank)
{
	*bank = pages_of(layer - DL_NBG0, pages);
}

uint16_t dl_video_word(uint32_t index)
{
	return video_word(index);
}
