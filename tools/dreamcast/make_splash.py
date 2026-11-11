#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""The Dreamcast disc's loading screen, put together on the player's machine from two pictures the
player's own ROM set draws (splash frames tool: the title screen, and one of its cars alone on magenta).
Usage: make_splash.py bg.bmp car.bmp splash.bin [VERSION]

splash.bin (little endian): "ORSP", u16 width 320, u16 height 224, the picture as RGB565; then the car:
u16 width, u16 height, RGB565 with 0xF81F where it is clear; then u16 road_y (the row its wheels stand on).
The lettering is this project's own 5 x 7 font below, coloured like the game's. No ROM data is in this
file or anywhere in the package."""
import struct, sys

FONT = {
    'A': ".###. #...# #...# ##### #...# #...# #...#", 'B': "####. #...# #...# ####. #...# #...# ####.",
    'C': ".###. #...# #.... #.... #.... #...# .###.", 'D': "####. #...# #...# #...# #...# #...# ####.",
    'E': "##### #.... #.... ####. #.... #.... #####", 'F': "##### #.... #.... ####. #.... #.... #....",
    'G': ".###. #...# #.... #.### #...# #...# .###.", 'H': "#...# #...# #...# ##### #...# #...# #...#",
    'I': ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.", 'J': "..### ...#. ...#. ...#. ...#. #..#. .##..",
    'K': "#...# #..#. #.#.. ##... #.#.. #..#. #...#", 'L': "#.... #.... #.... #.... #.... #.... #####",
    'M': "#...# ##.## #.#.# #.#.# #...# #...# #...#", 'N': "#...# ##..# #.#.# #..## #...# #...# #...#",
    'O': ".###. #...# #...# #...# #...# #...# .###.", 'P': "####. #...# #...# ####. #.... #.... #....",
    'Q': ".###. #...# #...# #...# #.#.# #..#. .##.#", 'R': "####. #...# #...# ####. #.#.. #..#. #...#",
    'S': ".#### #.... #.... .###. ....# ....# ####.", 'T': "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    'U': "#...# #...# #...# #...# #...# #...# .###.", 'V': "#...# #...# #...# #...# #...# .#.#. ..#..",
    'W': "#...# #...# #...# #.#.# #.#.# ##.## #...#", 'X': "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    'Y': "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..", 'Z': "##### ....# ...#. ..#.. .#... #.... #####",
    '0': ".###. #...# #..## #.#.# ##..# #...# .###.", '1': "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    '2': ".###. #...# ....# ...#. ..#.. .#... #####", '3': "####. ....# ....# .###. ....# ....# ####.",
    '4': "...#. ..##. .#.#. #..#. ##### ...#. ...#.", '5': "##### #.... ####. ....# ....# #...# .###.",
    '6': ".###. #.... #.... ####. #...# #...# .###.", '7': "##### ....# ...#. ..#.. .#... .#... .#...",
    '8': ".###. #...# #...# .###. #...# #...# .###.", '9': ".###. #...# #...# .#### ....# ....# .###.",
    ' ': "..... ..... ..... ..... ..... ..... .....", '.': "..... ..... ..... ..... ..... .##.. .##..",
    '-': "..... ..... ..... .###. ..... ..... .....", '(': "...#. ..#.. .#... .#... .#... ..#.. ...#.",
    ')': ".#... ..#.. ...#. ...#. ...#. ..#.. .#...", ':': "..... .##.. .##.. ..... .##.. .##.. .....",
    '!': "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..", '/': "....# ....# ...#. ..#.. .#... #.... #....",
    ',': "..... ..... ..... ..... .##.. ..#.. .#...", "'": "..#.. ..#.. .#... ..... ..... ..... .....",
}

W, H = 320, 224


def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM' or struct.unpack_from('<H', data, 28)[0] != 24:
        raise SystemExit(f'{path}: not a 24-bit BMP')
    offset = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    row = (w * 3 + 3) & ~3
    rows = []
    for y in range(abs(h)):
        at = offset + (abs(h) - 1 - y if h > 0 else y) * row
        rows.append([(data[at + 3 * x + 2], data[at + 3 * x + 1], data[at + 3 * x]) for x in range(w)])
    return rows


def rgb565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def text_width(text, scale):
    return len(text) * 6 * scale - scale


def draw_text(img, x, y, text, scale, top, bottom, edge=(16, 16, 72), shadow=(0, 0, 24)):
    """the game's look: letters filled top to bottom from one colour to another, a dark edge all round and
    a shadow below right"""
    lit = set()
    for i, ch in enumerate(text.upper()):
        rows = FONT.get(ch, FONT[' ']).split()
        for gy in range(7):
            for gx in range(5):
                if rows[gy][gx] == '#':
                    for sy in range(scale):
                        for sx in range(scale):
                            lit.add((x + (i * 6 + gx) * scale + sx, y + gy * scale + sy))
    def put(px, py, c):
        if 0 <= px < W and 0 <= py < H:
            img[py][px] = c
    for (px, py) in lit:
        put(px + scale, py + scale, shadow)
        put(px + scale + 1, py + scale, shadow)
    for (px, py) in lit:
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if (px + dx, py + dy) not in lit:
                    put(px + dx, py + dy, edge)
    for (px, py) in lit:
        put(px, py, mix(top, bottom, (py - y) / max(1, 7 * scale - 1)))


def centred(img, y, text, scale, top, bottom):
    draw_text(img, (W - text_width(text, scale)) // 2, y, text, scale, top, bottom)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    version = sys.argv[4] if len(sys.argv) > 4 else ''
    bg = read_bmp(sys.argv[1])
    car_frame = read_bmp(sys.argv[2])
    if len(bg) != H or len(bg[0]) != W or len(car_frame) != H:
        raise SystemExit('unexpected picture size')
    img = [list(row) for row in bg]

    # the lower part: a dark panel over the beach (and the arcade's coin prompt), for the credits
    top = 143
    for y in range(top, H):
        for x in range(W):
            shade = mix((8, 16, 72), (0, 0, 32), (y - top) / (H - top))
            img[y][x] = mix(img[y][x], shade, 0.88) if y < top + 3 else shade
    for x in range(W):                                   # a gold line and a white one along its top
        img[top][x] = (255, 208, 0)
        img[top + 1][x] = (255, 248, 200)
        img[top + 2][x] = (200, 96, 0)

    gold, orange = (255, 240, 64), (255, 112, 0)
    white, sky = (255, 255, 255), (120, 200, 255)
    centred(img, 150, 'DREAMCAST PORT BY PEZZ82', 2, gold, orange)
    centred(img, 168, 'ORIGINAL PC PORT BY SEBASTIAN TOYOS (CHEVIMAN)', 1, white, sky)
    note = 'UNOFFICIAL FAN PORT - NOT ENDORSED BY SEGA'
    centred(img, 177, note + (f' - V{version}' if version else ''), 1, (176, 184, 216), (112, 120, 168))

    # the car alone: its box, magenta outside it
    key = (255, 0, 255)
    xs = [x for row in car_frame for x, c in enumerate(row) if c != key]
    ys = [y for y, row in enumerate(car_frame) if any(c != key for c in row)]
    if not xs:
        raise SystemExit('no car in the car picture')
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    car = [row[x0:x1] for row in car_frame[y0:y1]]
    road_y = H - 4                                       # the loading bar is the road under its wheels

    out = bytearray(b'ORSP')
    out += struct.pack('<HH', W, H)
    for row in img:
        out += struct.pack(f'<{W}H', *[rgb565(c) for c in row])
    out += struct.pack('<HH', x1 - x0, y1 - y0)
    for row in car:
        out += struct.pack(f'<{len(row)}H', *[0xF81F if c == key else (rgb565(c) if rgb565(c) != 0xF81F else 0xF81E) for c in row])
    out += struct.pack('<H', road_y)
    open(sys.argv[3], 'wb').write(out)
    print(f'{sys.argv[3]}: {len(out)} bytes, car {x1 - x0} x {y1 - y0}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
