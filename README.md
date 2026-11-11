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

**Version 0.2, experimental.** Download it from [Releases](https://github.com/MisterPezz82/OutRunners-Dreamcast/releases).

New in 0.2: the PC version's **title menu** (ARCADE, & KNUCKLES, OPTIONS), a **pause menu**, **vibration**, and
options saved to the VMU.

What has been tested:
- **Version 0.1:** on a Dreamcast with GDEMU, and in the Flycast emulator.
- **The 0.2 disc:** in Flycast it boots through the loading screen and the SEGA intro to the title screen.
- **The menus, the first OPTIONS page, & KNUCKLES > REVERSE and the pause:** checked in Flycast with scripted
  inputs only.
- **Not tested yet:** 0.2 on a real console, a real pad in the menus, the other OPTIONS pages, CLASSIC, TIME
  ATTACK, FULL, vibration and saving options to the VMU. Reports are very welcome.

Known gaps:
- Large distant scenery can **flicker** on some stages (the pyramids, the snow).
- In the busiest stretches a frame can be dropped to keep the game and its music at full speed, and distant
  scenery can appear a moment late.
- **TIME ATTACK has no ghost car**: there isn't enough spare memory for the recording.
- FM sound and sound LFOs aren't reproduced.
- One player only: no 2 PLAYERS and no NETWORK.

## Making the disc

You need:
- your OutRunners ROM set as MAME names it: **`orunners.zip`** (the World set);
- **Windows 10 or 11** (the release package includes everything else), or **Linux/WSL** with `python3`, a C
  compiler and [mkdcdisc](https://gitlab.com/simulant/mkdcdisc) 0.0.4 or later.

Using the release package
([Releases](https://github.com/MisterPezz82/OutRunners-Dreamcast/releases), `OutRunners-DC-0.2.zip`):
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

## Menus

Press Start on the title screen:

| Menu | What's in it |
|---|---|
| **ARCADE** | The arcade game, one player, free play |
| **& KNUCKLES** | CLASSIC (the arcade game with a little more time), TIME ATTACK (no time limit; no ghost car yet), FULL (all the stages in one race), REVERSE (from a goal back to the start) |
| **OPTIONS** | DIFFICULTY, TIME, VOLUME, SOUND TEST, STEERING (the stick's dead zone and sensitivity, the D-pad's steering speed, vibration, an input test), FPS |

In a race, Start pauses the game: CONTINUE, RESTART or QUIT GAME.

## Controls (standard controller, port A)

| Dreamcast | In the game | In the menus |
|---|---|---|
| Analogue stick | Steer (small centre dead zone) | Move |
| D-pad left / right | Steer: the wheel turns while held, at the speed set in OPTIONS > STEERING | Move |
| R trigger (analogue) | Accelerate | |
| L trigger (analogue) | Brake | |
| A / B | Gear shift | A picks a line, B goes back |
| Start | Start; pause a race | Pick a line |
| Y | Music select | |
| D-pad up / down | Previous / next music | Move |

The game runs on **free play**, so there's no coin button. A vibration pack shakes when the cabinet's wheel
motor would, for example in a crash or off the road.

**Changes since 0.1:** Start now opens the menu and pauses a race, A and B work the menus, the D-pad turns the
wheel gradually instead of snapping to full lock, and X no longer inserts a coin.

## Saving

Course records, the game's settings, the OPTIONS menu's settings and its bookkeeping are saved to the first
VMU found, in a file called `ORUNNERS` (about 2 blocks), and loaded when the game starts. Records and settings
are saved within ten seconds of a change; the rest is saved at most every five minutes.

## Sound

The arcade board serves two cabinets and sends each seat's sound to one speaker. Here both sides go to both
speakers, so the music and effects are centred.

## Getting it onto a Dreamcast

The PC port runs the arcade game's logic and draws every frame in software, which a modern computer does
without effort. A Dreamcast has a 200 MHz processor, 16 MB of memory and 8 MB of video memory, and the game's
graphics and sound alone are more than the whole console holds. Most of this port is the work of fitting it in
and keeping it at 60 frames a second:

- **The picture isn't drawn in software.** The arcade board's tile planes and sprites become PowerVR textures
  and quads each frame. Sprite textures are built, cached and thrown out as the scenery goes by, and 16-colour
  textures share palettes, so one drawing serves every colour.
- **The data streams.** The graphics and samples stay compressed on the disc and are read in small pieces in
  the background while you play, so the race never waits for the disc.
- **The sound board still runs.** The arcade's sound board (its own processor and sample chip) is still run,
  but its samples are played by the Dreamcast's sound chip directly, not mixed in software.
- **Every frame has a budget.** A frame's work is budgeted in microseconds. Whatever can't be prepared in time
  waits a frame, far scenery first; the player's car and the road never wait.

The emulators were the other half of the trouble. A build that ran perfectly in Flycast crashed, dropped
sprites, lost its music or showed warped textures on a real console, each time for a reason the emulator
hides:
- the real processor traps on unaligned memory access;
- the real video chip is still drawing one frame while the next is being prepared;
- it interpolates texture coordinates less exactly;
- the real disc and sound hardware are far slower to feed.

Each of those was found from videos of a console and fixed one at a time. If something looks right in an
emulator and wrong on your Dreamcast, that's a bug worth reporting.

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
