#!/usr/bin/env python3
"""Packs tiles.bin (one pen per byte, 256 bytes a 16x16 tile) to two pens per byte (low nibble the
even column): tiles4.bin, half the size, for a Dreamcast build that keeps every tile in RAM.
Refuses input with a pen above 15. Usage: pack_tiles4.py tiles.bin tiles4.bin"""
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
if len(data) % 256:
    sys.exit('tiles.bin is not whole tiles')
if max(data) > 15:
    sys.exit('a pen above 15: cannot pack to 4 bits')
out = bytes((data[i] & 15) | (data[i + 1] & 15) << 4 for i in range(0, len(data), 2))
open(dst, 'wb').write(out)
print(f'{src}: {len(data)} bytes, {len(data)//256} tiles -> {dst}: {len(out)} bytes')
