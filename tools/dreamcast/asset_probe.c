/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Note: mallinfo2 measures host live allocator usage, NOT exact DC totals/peak.
 */

#include <game/assets.h>
#ifdef __GLIBC__
#include <malloc.h>
#define LIVE_BYTES() ({ struct mallinfo2 m = mallinfo2(); (long long)(m.uordblks + m.hblkhd); })
#define MMAP_BYTES() ((long long)mallinfo2().hblkhd)
#else
#define LIVE_BYTES() 0LL    /* (no glibc allocator figures: the Windows disc builder) */
#define MMAP_BYTES() 0LL
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char **argv)
{
    int pack_mode = 0;
    int input_idx = 1;

    if (argc >= 2 && strcmp(argv[1], "--pack") == 0) {
        pack_mode = 1;
        input_idx = 2;
    }

    if (pack_mode) {
        if (argc < 3 || argc > 4) {
            fprintf(stderr, "Usage: %s --pack <assets_dir> [output_pack]\n", argv[0]);
            return 1;
        }
    } else {
        if (argc < 2 || argc > 3) {
            fprintf(stderr, "Usage: %s <assets_dir> [output_pack]\n", argv[0]);
            return 1;
        }
    }

    const char *input_path = argv[input_idx];
    const char *output_path = (argc > input_idx) ? argv[input_idx + 1] : NULL;

    const long long before = LIVE_BYTES();
    const char *err = pack_mode ? assets_load_pack(input_path) : assets_load(input_path);
    const long long after = LIVE_BYTES();

    if (err) {
        fprintf(stderr, "Loader error: %s\n", err);
        return 1;
    }

    long long delta = after - before;

    printf("{\n");
    printf("  \"allocator_live_delta_bytes\": %lld,\n", delta);
    printf("  \"allocator_live_total_bytes\": %lld,\n", after);
    printf("  \"allocator_mmap_bytes\": %lld,\n", MMAP_BYTES());
    printf("  \"program_data_bytes\": %zu,\n", assets_data_bytes());
    printf("  \"program_piece_count\": %zu\n", assets_piece_count());
    printf("}\n");

    if (output_path) {
        const char *pack_err = assets_pack_write(output_path);
        if (pack_err) {
            fprintf(stderr, "Pack write error: %s\n", pack_err);
            return 1;
        }
    }

    return 0;
}
