/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * vram_alloc: a buddy allocator over one block of texture memory. Every texture render_emit makes is a
 * power of two in each dimension, so blocks of power-of-two sizes fit them exactly; allocation and
 * freeing take a few steps whatever the number of textures (KOS's pvr_mem took hundreds of
 * milliseconds a frame once hundreds of sprite textures had come and gone).
 */
#ifndef VRAM_ALLOC_H
#define VRAM_ALLOC_H
#include <stdint.h>

#define VA_MIN_ORDER 8      /* 256-byte blocks at the least */
#define VA_MAX_ORDER 23     /* 8 MiB */

int va_init(uint32_t bytes);           /* offsets 0 .. bytes managed; 0 on success */
int32_t va_alloc(uint32_t bytes);      /* an offset aligned to the block size, or -1 */
void va_free(int32_t offset);
uint32_t va_free_bytes(void);
uint32_t va_largest_free(void);
#endif
