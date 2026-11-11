# OutRunners for Dreamcast

A native **Sega Dreamcast** port of **OutRunners** (Sega, arcade, 1992). It runs the game's own logic and
uses its own PowerVR renderer and AICA sound.

It's built on [cheviman/outrunners-port](https://github.com/cheviman/outrunners-port), the native port of
the verified C decompilation of the arcade game. All the game code comes from there; this repository adds the
Dreamcast port. Upstream's own README is kept at [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md).

> ### Bring your own ROM
> **This repository contains no Sega data**: no graphics, sound, music or program tables from the arcade ROMs.
> You build the disc yourself from your own, legally owned ROM set, and the disc you make is for your own use.
> Please don't share disc images.

## Status

**Experimental.** Version 0.1 is playable. It has been tested in the Flycast emulator and on a Dreamcast with
GDEMU (hardware testing has only just begun). Work towards 0.2 (the PC version's title menu and OPTIONS page)
is in progress.

Known gaps:
- Sample sound can drop for a moment when the game switches sample banks, for example at the start of a race.
- FM music is still emulated on the main CPU, and sound LFOs aren't reproduced.
- In the busiest stretches a frame can be dropped to keep the game and its music at full speed, and distant
  scenery can appear a moment late.
- One player only: no link play or network.

## Making the disc

You need:
- your OutRunners ROM set as MAME names it: **`orunners.zip`** (the World set);
- **Windows 10 or 11** (the release package includes everything else), or **Linux/WSL** with `python3`, a C
  compiler and [mkdcdisc](https://gitlab.com/simulant/mkdcdisc) 0.0.4 or later.

Using a release package:
- **Windows:** drag `orunners.zip` onto `make_disc.bat`, or run it and choose the zip.
- **Linux/WSL:** `./make_disc.sh /path/to/orunners.zip`

It takes two or three minutes. It checks and extracts your ROM set, converts the data for the Dreamcast, and
builds `output/OutRunners.cdi`. That file works with GDEMU, MODE, emulators (Flycast, Redream) or a CD-R. For
GDEMU, copy it into a new numbered folder on the SD card.

## Picture

| Cable | Output |
|---|---|
| VGA box or cable | **480p**: the arcade's 320 × 224 picture at exactly twice its size, each pixel and line doubled by the video hardware. Sharp, with no scaling shimmer or blur. |
| RGB/SCART, S-Video, composite | **240p at 60 Hz**, progressive, like the arcade board's own picture, on PAL consoles too (at 50 Hz the game would run slow). Your TV must accept 60 Hz. |

## Controls (standard controller, port A)

| Dreamcast | Arcade |
|---|---|
| Analogue stick (small centre dead zone) | Steering wheel |
| D-pad left / right | Full lock |
| R trigger (analogue) | Accelerator |
| L trigger (analogue) | Brake |
| A / B | Gear shift up / down |
| X | Insert coin |
| Start | Start |
| Y | Music select |
| D-pad up / down | Previous / next music |
| A + B + X + Y + Start | Leave the game |

## Saving

Course records, the game's settings and its bookkeeping are saved to the first VMU found, in a file called
`ORUNNERS` (about 2 blocks), and loaded when the game starts. Records and settings are saved within ten
seconds of a change; the rest is saved at most every five minutes.

## Sound

The arcade board serves two cabinets and sends each seat's sound to one speaker. Here both sides go to both
speakers, so the music and effects are centred.

## Building from source

You need a [KallistiOS](https://github.com/KallistiOS/KallistiOS) toolchain (an SH4 cross-compiler and KOS).
Clone with the submodules (`lan` and `tools/core` are upstream's own repositories):

```bash
git clone --recurse-submodules https://github.com/MisterPezz82/OutRunners-Dreamcast.git
cd OutRunners-Dreamcast
source /opt/toolchains/dc/kos/environ.sh
cd ports/dreamcast
make runtime-check game-check prototype      # -> build/outrunners-prototype.elf
```

The disc builder (`make_disc.py`, with its `.bat` / `.sh` wrappers) is in `ports/dreamcast/dist/`. The
host-side tools (asset packing, the AICA ADPCM encoder, draw-list checks, Flycast test scripts) are in
`tools/dreamcast/`. The renderer's design notes are in
[docs/dreamcast/PVR-RENDERER-DESIGN.md](docs/dreamcast/PVR-RENDERER-DESIGN.md).

## Where things are

| Path | What's there |
|---|---|
| `ports/dreamcast/` | The port: KallistiOS start-up and tasks, PowerVR renderer (`pvr/`), AICA PCM sound, input, VMU saves, streamed media and the page cache, the disc builder (`dist/`) |
| `tools/dreamcast/` | Host tools and tests for the port |
| `runtime/`, `console/` | Upstream's runtime and console layer, with small changes for the Dreamcast |
| `docs/dreamcast/` | Renderer design and controls |

## Licence and credits

- **Game code and port:** BSD 3-Clause, see [LICENSE](LICENSE). The upstream project is © Sebastian Toyos
  ([cheviman](https://github.com/cheviman)). Third-party licences inside the tree (for example
  `ports/sdl2/prompts/License.txt`) are unchanged.
- **Upstream:** [cheviman/outrunners-port](https://github.com/cheviman/outrunners-port),
  [arcade-decomp](https://github.com/cheviman/arcade-decomp) and
  [arcade-lan](https://github.com/cheviman/arcade-lan). This port wouldn't exist without them.
- **Other projects:** [KallistiOS](https://github.com/KallistiOS/KallistiOS) and
  [mkdcdisc](https://gitlab.com/simulant/mkdcdisc) are separate projects under their own licences.
  [Flycast](https://github.com/flyinghead/flycast) was used for testing.
- **OutRunners** is © SEGA. This is an unofficial fan project and isn't affiliated with or endorsed by SEGA.
