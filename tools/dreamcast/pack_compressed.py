#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Compresses a media file into independent raw-deflate pages, for the Dreamcast build to keep whole in
main RAM and unpack a page at a time (runtime/src/inflate.c's inflate_raw): no disc reads while playing.
Format (little-endian): "ORZ1", page size, page count, uncompressed size, then page count + 1 offsets into
the data that follows; a page whose stored length equals the page size is stored as it is.
Usage: pack_compressed.py in.bin out.z page_size"""
import struct, sys, zlib

src, dst, page = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(src, 'rb').read()
count = (len(data) + page - 1) // page
blobs = []
for i in range(count):
    p = data[i * page:(i + 1) * page].ljust(page, b'\xff')
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    z = c.compress(p) + c.flush()
    blobs.append(z if len(z) < page else p)
offsets, at = [], 0
for b in blobs:
    offsets.append(at)
    at += len(b)
offsets.append(at)
with open(dst, 'wb') as f:
    f.write(b'ORZ1' + struct.pack('<III', page, count, len(data)))
    f.write(struct.pack('<%dI' % len(offsets), *offsets))
    for b in blobs:
        f.write(b)
print(f'{src}: {len(data)} bytes in {count} pages of {page} -> {dst}: {16 + 4 * len(offsets) + at} bytes')
