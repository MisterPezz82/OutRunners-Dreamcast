/* SPDX-License-Identifier: BSD-3-Clause */
#include "vram_alloc.h"
#include <stdlib.h>
#include <string.h>

#define NIL 0xffffu
#define ST_FREE 0x80u
#define ST_USED 0x40u

static uint32_t units;                 /* in 256-byte units */
static uint16_t *nxt, *prv;
static uint8_t *st;                    /* at a block's first unit: ST_FREE or ST_USED | (order - VA_MIN_ORDER) */
static uint16_t heads[VA_MAX_ORDER + 1];
static uint32_t free_bytes;

static void push(uint32_t u, int order)
{
	st[u] = (uint8_t)(ST_FREE | (order - VA_MIN_ORDER));
	prv[u] = NIL;
	nxt[u] = heads[order];
	if (heads[order] != NIL) prv[heads[order]] = (uint16_t)u;
	heads[order] = (uint16_t)u;
}

static void unlink_block(uint32_t u, int order)
{
	if (prv[u] != NIL) nxt[prv[u]] = nxt[u]; else heads[order] = nxt[u];
	if (nxt[u] != NIL) prv[nxt[u]] = prv[u];
	st[u] = 0;
}

int va_init(uint32_t bytes)
{
	free(nxt); free(prv); free(st);
	units = bytes >> VA_MIN_ORDER;
	if (units == 0 || units >= NIL) { units = 0; return -1; }
	nxt = malloc(units * sizeof(uint16_t));
	prv = malloc(units * sizeof(uint16_t));
	st = calloc(units, 1);
	if (!nxt || !prv || !st) { units = 0; return -1; }
	for (int k = 0; k <= VA_MAX_ORDER; k++) heads[k] = NIL;
	/* the pool as blocks of decreasing size: each starts at a multiple of twice its size, so no block's
	   buddy is ever a block of its own order in another root */
	uint32_t u = 0;
	free_bytes = 0;
	for (int k = VA_MAX_ORDER; k >= VA_MIN_ORDER; k--)
	{
		const uint32_t n = 1u << (k - VA_MIN_ORDER);
		if (units - u >= n) { push(u, k); u += n; free_bytes += 1u << k; }
	}
	return 0;
}

int32_t va_alloc(uint32_t bytes)
{
	int order = VA_MIN_ORDER;
	while (order <= VA_MAX_ORDER && (1u << order) < bytes) order++;
	if (order > VA_MAX_ORDER) return -1;
	int k = order;
	while (k <= VA_MAX_ORDER && heads[k] == NIL) k++;
	if (k > VA_MAX_ORDER) return -1;
	const uint32_t u = heads[k];
	unlink_block(u, k);
	while (k > order)
	{
		k--;
		push(u + (1u << (k - VA_MIN_ORDER)), k);    /* the upper half stays free */
	}
	st[u] = (uint8_t)(ST_USED | (order - VA_MIN_ORDER));
	free_bytes -= 1u << order;
	return (int32_t)(u << VA_MIN_ORDER);
}

void va_free(int32_t offset)
{
	if (offset < 0) return;
	uint32_t u = (uint32_t)offset >> VA_MIN_ORDER;
	if (u >= units || !(st[u] & ST_USED)) return;
	int k = (st[u] & 0x3f) + VA_MIN_ORDER;
	free_bytes += 1u << k;
	st[u] = 0;
	while (k < VA_MAX_ORDER)
	{
		const uint32_t b = u ^ (1u << (k - VA_MIN_ORDER));
		if (b >= units || st[b] != (uint8_t)(ST_FREE | (k - VA_MIN_ORDER))) break;
		unlink_block(b, k);
		if (b < u) u = b;
		k++;
	}
	push(u, k);
}

uint32_t va_free_bytes(void) { return free_bytes; }

uint32_t va_largest_free(void)
{
	for (int k = VA_MAX_ORDER; k >= VA_MIN_ORDER; k--)
		if (heads[k] != NIL) return 1u << k;
	return 0;
}
