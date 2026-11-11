/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * pal4_twiddle.h: the PowerVR's twiddled layout for 4-bit paletted textures (paletted textures are
 * twiddled only), shared by the console backend and the host check so the host checks the layout.
 * A w x h texture (powers of two) is min(w, h) square blocks side by side; in each block the texel
 * order is Morton order with y in the low bit. A 16-bit word holds four texels, a 2 x 2 square:
 * (x, y) bits 0-3, (x, y + 1) bits 4-7, (x + 1, y) bits 8-11, (x + 1, y + 1) bits 12-15 (as KOS's
 * pvr_txr_load_ex lays them out).
 */
#ifndef DC_PAL4_TWIDDLE_H
#define DC_PAL4_TWIDDLE_H

#include <stdint.h>
#include <stddef.h>

static inline uint32_t pal4_spread(uint32_t v)    /* bits 0-8 to the even bits */
{
	v &= 0x1ff;
	v = (v | v << 8) & 0x00ff00ffu;
	v = (v | v << 4) & 0x0f0f0f0fu;
	v = (v | v << 2) & 0x33333333u;
	v = (v | v << 1) & 0x55555555u;
	return v;
}

/* the word holding the 2 x 2 square whose top left texel is (x, y), both even */
static inline uint32_t pal4_word(int w, int h, int x, int y)
{
	const int min = w < h ? w : h, mask = min - 1;
	return (pal4_spread((uint32_t)((y & mask) >> 1)) | pal4_spread((uint32_t)((x & mask) >> 1)) << 1) +
		(uint32_t)(x / min + y / min) * (uint32_t)(min * min / 4);
}

/* rows y0 .. y0 + n - 1 (y0 and n even) of a w x h texture from one index byte a texel, 'stride' bytes a
   row; 'words' the texture's memory as 16-bit words */
static inline void pal4_write_rows(volatile uint16_t *words, int w, int h, int y0, int n, const uint8_t *idx, int stride)
{
	for (int y = 0; y < n; y += 2)
	{
		const uint8_t *a = idx + (size_t)y * stride, *b = a + stride;
		for (int x = 0; x < w; x += 2)
			words[pal4_word(w, h, x, y0 + y)] = (uint16_t)((a[x] & 15) | (b[x] & 15) << 4 | (a[x + 1] & 15) << 8 |
				(b[x + 1] & 15) << 12);
	}
}

/* (host check) the index at (u, v) */
static inline int pal4_texel(const uint16_t *words, int w, int h, int u, int v)
{
	const uint16_t word = words[pal4_word(w, h, u & ~1, v & ~1)];
	return (word >> (((u & 1) << 3) | ((v & 1) << 2))) & 15;
}

#endif
