> **Note:** these are the developer notes from the first prototype, and their status line is out of date. Version 0.1 boots and plays with sound and VMU saves. See the [top-level README](../../README.md) for the current status, disc building and controls.

# Dreamcast Port: Experimental OutRunners

**Status:** Prototype CDI built and launched. Boot/rendering not yet verified. No audio, VMU, or FPS claims. No real hardware test.

## Verification
- **Upstream:** Pinned `1a610f5a041aca68760be04872219dcb712f1eec`
- **Build:** All 43 game/sound C files cross-compile. SH4 `-Werror` clean.
- **Sanitizers:** ASan/UBSan pass.
- **Diagnostics:** Flycast 2.7 booted. Workers PASS. 1000 coroutine roundtrips PASS.
- **Assets:** Exhaustive comparison passed (8MiB tiles + 16MiB sprites + 4MiB PCM). Cache payload: 1,835,184 bytes.
- **Size:** Prototype ELF text 853,508, data 17,988, bss 4,806,472 (pre-argc fix).

## Build Commands
```bash
source /opt/toolchains/dc/kos/environ.sh
cd ports/dreamcast
make runtime-check game-check diagnostic prototype
```

## Asset Setup
From repo root:
```bash
python tools/dreamcast/bootstrap_core.py
python extract_assets.py /path/orunners.zip
bash tools/dreamcast/build_probe.sh [asset-probe|test-stream-media]
```
Pack assets:
```bash
pack from asset-probe assets output.assets
split_pack.py output.assets NEWfolder
```
**Note:** Core assets + 3 media files must be inside CDI `/assets`.

## Technical Notes
- **KOS:** 128KiB task stacks default (experimental, needs measurement). Render workers serial.
- **GFX:** Optional `tile_at` callback (resident NULL preserves existing behavior). Exact sprite LE decode. Page cache fixed sizes direct mapped.
- **Controls:**
  - Analog wheel/Dpad: Steer
  - RT: Gas
  - LT: Brake
  - X: Coin
  - Start: Start
  - A: Gear up
  - B: Gear down
  - Y: Music
  - A+B+X+Y+Start: Quit

## Next Steps
1. Confirm boot/render.
2. Fix any SH4 alignment/stack bugs.
3. Audio AICA.
4. Performance profiling and cache miss counter.
5. VMU saves.
6. Full race validation.

## Constraints
- No copyrighted assets in commits.
- GPL Flycast tools separate from BSD game.
- Native Flycast primary, Redream secondary, hardware last.
