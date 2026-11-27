"""
Name:        make_icon.py
Description: makes the project's icon: Sebas's car (ports/sdl/icon/car.png) on a plate in a sunset's colours
Version:     0.3.0
Created:     30-10-2026
Modified:    25-11-2026
Author:      Sebastian Toyos

Usage: python tools/make_icon.py
       python tools/make_icon.py --ico PICTURE ICO


As SegaSonic's: the icon of the Switch's build (ports/switch/icon.jpg, 256 x 256: the plate to the edges,
which the HOME menu rounds itself), of the Android app (ports/android/res/mipmap-xxxhdpi: an adaptive icon's
two layers, the plate to the edges and the car in the middle 66 of its 108, which the launcher shapes and
moves itself; the icon whole for the launchers before Android 8; and the picture the APK shows in a player's
own picture's place until one is put there, ic_launcher_art.png) and of the computers' executables
(ports/sdl/icon: outrunners.png, 1024 x 1024, outrunners.ico and outrunners.icns) and of the iOS app
(ports/ios/Assets.xcassets: icon_1024.png, the plate to the edges with no see-through: iOS rounds it itself).
The plate is drawn at twice
the size and scaled down, so its edges are smooth; the car, a picture of pixels, is enlarged a whole number of
times, its pixels kept square (Sebas: the car he chose, from a Steam page that offers it free to use).

--ico makes the Windows executables' icon of a picture of the player's own instead (CMakeLists.txt, with
local/icon_pc.png or ORUNNERS_PC_ICON): square, the picture whole in the middle of it, see-through around.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "ports" / "sdl" / "icon"
CAR = OUT / "car.png"
SWITCH = ROOT / "ports" / "switch" / "icon.jpg"
IOS = ROOT / "ports" / "ios" / "Assets.xcassets" / "AppIcon.appiconset" / "icon_1024.png"
ANDROID = ROOT / "ports" / "android" / "res" / "mipmap-xxxhdpi"
ANDROID_ICON = 192              # (the launcher's icon at xxxhdpi: 48 dp)
ANDROID_LAYER = 432             # (an adaptive icon's layer at xxxhdpi: 108 dp)
ANDROID_SAFE = 66 / 108         # (the part of a layer every launcher's shape shows)
ANDROID_ART = 288               # (a picture in what the launcher shows of a layer at xxxhdpi: 72 dp)
SIZE = 1024
SCALE = 2                       # (the plate drawn at 2048, then scaled down)
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
CAR_WIDTH = 0.74                # the car's width, of the icon's

PLATE_TOP = np.array([0x24, 0x1A, 0x6E])     # the plate: a sunset, deep blue at the top to orange
PLATE_MIDDLE = np.array([0xC8, 0x2E, 0x6A])
PLATE_BOTTOM = np.array([0xFF, 0x8C, 0x2E])


def coverage(distance, pixel):
    """How much of a pixel a shape covers, from its signed distance to the edge (negative inside)."""
    return np.clip(0.5 - distance / pixel, 0.0, 1.0)


def rounded_square(x, y, left, top, right, bottom, radius):
    """Signed distance to a square with rounded corners."""
    cx, cy = (left + right) / 2, (top + bottom) / 2
    qx = np.abs(x - cx) - ((right - left) / 2 - radius)
    qy = np.abs(y - cy) - ((bottom - top) / 2 - radius)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return outside + np.minimum(np.maximum(qx, qy), 0) - radius


def plate(margin=48, radius=210):
    """The plate, the sky from the top down; 'margin' and 'radius' are its room around it and its corners'
    (0: to the edges)."""
    size = SIZE * SCALE
    unit = size / SIZE
    y, x = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    x, y = x / unit, y / unit           # (in the 1024 icon's pixels)
    image = np.zeros((size, size, 4))
    cover = coverage(rounded_square(x, y, margin, margin, SIZE - margin, SIZE - margin, max(radius, 1)), 1.0 / unit)
    shade = np.clip((y - margin) / (SIZE - 2 * margin), 0, 1)[..., None]
    upper = PLATE_TOP * (1 - shade * 2) + PLATE_MIDDLE * (shade * 2)
    lower = PLATE_MIDDLE * (2 - shade * 2) + PLATE_BOTTOM * (shade * 2 - 1)
    image[..., :3] = np.where(shade < 0.5, upper, lower)
    image[..., 3] = cover
    image = Image.fromarray(np.clip(image * [1, 1, 1, 255], 0, 255).round().astype(np.uint8), "RGBA")
    return image.resize((SIZE, SIZE), Image.LANCZOS)


def with_car(image, width=CAR_WIDTH):
    """The car in the middle of 'image' (RGBA, SIZE square), about 'width' of it wide: the picture's pixels
    enlarged a whole number of times"""
    car = Image.open(CAR).convert("RGBA")
    car = car.crop(car.getbbox())
    times = max(1, round(SIZE * width / car.width))
    car = car.resize((car.width * times, car.height * times), Image.NEAREST)
    image = image.copy()
    image.alpha_composite(car, ((SIZE - car.width) // 2, (SIZE - car.height) // 2))
    return image


def nothing():
    return Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    icon = with_car(plate())
    icon.save(OUT / "outrunners.png")
    icon.save(OUT / "outrunners.ico", sizes=[(side, side) for side in ICO_SIZES])
    icon.save(OUT / "outrunners.icns")
    print(f"written: outrunners.png, outrunners.ico, outrunners.icns in {OUT.relative_to(ROOT)}")
    whole = with_car(plate(margin=0, radius=0)).convert("RGB")
    SWITCH.parent.mkdir(parents=True, exist_ok=True)
    whole.resize((256, 256), Image.LANCZOS).save(SWITCH, quality=95)
    print(f"written: {SWITCH.relative_to(ROOT)}")
    IOS.parent.mkdir(parents=True, exist_ok=True)
    whole.resize((1024, 1024), Image.LANCZOS).save(IOS)    # (iOS's: the square whole and opaque, the system rounds it)
    print(f"written: {IOS.relative_to(ROOT)}")
    ANDROID.mkdir(parents=True, exist_ok=True)
    icon.resize((ANDROID_ICON, ANDROID_ICON), Image.LANCZOS).save(ANDROID / "ic_launcher.png")
    background = plate(margin=0, radius=0).convert("RGB")
    background.resize((ANDROID_LAYER, ANDROID_LAYER), Image.LANCZOS).save(ANDROID / "ic_launcher_background.png")
    foreground = with_car(nothing(), CAR_WIDTH * ANDROID_SAFE)
    foreground.resize((ANDROID_LAYER, ANDROID_LAYER), Image.LANCZOS).save(ANDROID / "ic_launcher_foreground.png")
    whole.resize((ANDROID_ART, ANDROID_ART), Image.LANCZOS).save(ANDROID / "ic_launcher_art.png")
    print(f"written: ic_launcher.png, ic_launcher_background.png, ic_launcher_foreground.png, ic_launcher_art.png "
          f"in {ANDROID.relative_to(ROOT)}")


def picture_ico(picture, ico):
    """An .ico of a picture of the player's own, its sizes the project's icon's."""
    image = Image.open(picture).convert("RGBA")
    side = max(image.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, (side - image.height) // 2))
    Path(ico).parent.mkdir(parents=True, exist_ok=True)
    square.resize((256, 256), Image.LANCZOS).save(ico, sizes=[(size, size) for size in ICO_SIZES])


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--ico":
        picture_ico(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 1:
        main()
    else:
        sys.exit("Usage: python tools/make_icon.py [--ico PICTURE ICO]")
