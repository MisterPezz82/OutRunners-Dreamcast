# PowerVR renderer for OutRunners on Dreamcast: design (8 Nov 2026)

## Evidence this design rests on

Flycast 2.7 frame timers (emulated, not hardware timing), attract mode, frames 193-256:
whole frame 527 ms; software layer composition and mixing 410 ms; sprite list drawing 72 ms;
sound end-of-frame 4 ms; RGB888 to RGB565 present 9 ms; remainder (game logic plus up to
16.7 ms vblank wait) about 31 ms. Video is about 91% of the frame.

Host render census (`S32_RENDER_CENSUS`, left screen; attract 9,000 frames, race 11,026 frames):

| Feature | Attract | Race |
|---|---|---|
| Layers drawn | text, NBG0, NBG2, bitmap | same |
| NBG1, NBG3 | never | never |
| Tilemap zoom (NBG0/1) | never | never |
| Clip windows on tilemaps | never | never |
| NBG2 row scroll + row select | 34% of frames | 93% (the road) |
| Sprites per frame (median / p95 / max) | 53 / 73 / 104 | 59 / 86 / 113 |
| of which zoomed (median) | 31 | 36 |
| indirect colour tables (median / max) | 18 / 68 | 11 / 51 |
| 8-bit sprites (median) | 5 | 1 |
| shadow-flag sprites, sprites from RAM | 0 | 0 |
| Sprite pixels drawn (median / p95) | 161k / 371k | 118k / 411k |
| 16-colour palette banks per frame (median / p95 / max) | 75 / 117 / 149 | 83 / 115 / 129 |
| Palette entries changed per frame (median / p95) | 0 / 8 | 0 / 16 |
| Mixer blending enabled | always | always |
| Colour offsets non-zero | 21% | 3.5% |
| Sprite grouping | 13 (4 groups) | 13 |

(Text and bitmap colours are not in the bank count. The census "clipped sprites" counter
reported 100% and is suspected to be a measurement fault; to be checked.)

## Decisions

1. **Direct-colour textures, not the hardware palette.** The PowerVR palette holds 1,024
   entries (64 banks of 16 in 4-bit mode); frames use 75 to 150 banks. Palette RAM is almost
   static frame to frame, so textures are converted on the CPU to ARGB1555 (pen 0
   transparent) and cached, keyed by graphic plus a hash of the 16 palette entries it uses.
   A palette change only invalidates textures using the changed bank.
2. **Punch-through polygon list** for tiles and sprites (1-bit alpha), so the hardware
   sorts by depth with no CPU sorting. Translucent list only for shadow pens and blending.
3. **Depth from the mixer priority.** Each layer and each sprite group gets a depth band from
   its mixer priority (registers 0x00-0x2C, as `mix()` orders them); within the sprite layer,
   later sprites in the list get greater depth inside the band (painter's order of the
   sprite frame buffer).
4. **NBG0**: scrolled tile quads, no zoom or clipping needed for this game.
5. **NBG2 (road)**: tiles rendered once into a render-to-texture page image; then up to 224
   horizontal strips, each sampling the source row picked by row select at the x offset
   given by row scroll (two quads where a strip wraps across pages).
6. **Sprites**: one quad per sprite at its screen size; texture at source size (point
   sampling reproduces zoom pen repeat/skip closely). Indirect tables become their own
   palette key. End codes and the opaque flag are resolved during texture decode.
   Shadow pen (0x7ffe class) pixels become 50% black translucent.
7. **Text layer and bitmap layer**: CPU-built textures, rebuilt only when their video RAM
   or palette changes (checksum).
8. **Colour offsets**: positive offsets via the vertex offset colour; negative offsets via
   vertex colour modulation (approximate). Blending: translucent polygons once the census
   shows which layer pairs blend.
9. **Reference**: the existing software renderer stays as the correctness oracle; a
   host-side draw list must reproduce its frame hashes before any PowerVR code is trusted.

## Known approximations to verify against reference frames

- A later sprite with a lower-priority group overwriting an earlier higher-priority sprite's
  pixels in the frame buffer; depth testing will show the earlier sprite instead.
- Sprite buffer persistence when the game does not erase it between lists.
- Exact zoom sampling and end-of-row behaviour for flipped, clipped sprites.

## Open questions (next census pass)

- Which layer pairs actually blend, and how often (blend is enabled every frame).
- Whether the bitmap layer has visible pixels in this game.
- Sprite clip windows in practice (fix the census counter).
- Texture cache size: distinct sprite graphics per frame and per race.

## Draw list validation (host, 8 Nov 2026)

`ports/dreamcast/pvr/drawlist.c` describes each frame as planes (per-row source position plus
clip-window spans), backdrop rows, mixer bands and sprite records captured when the board sends
the list. `tools/dreamcast/drawlist_check.c` (built with -DDL_CHECK) rasterises it with the
PowerVR rules and compares every pixel with `video.c`:

- Attract, 3,200 frames: 3,190 of 3,192 displayed frames pixel-identical (frames 0-1 are
  power-on frames before the display is set up).
- Race (race_to_goal script), 9,752 frames: with pure depth ordering 9,176 exact (94%); all
  differences are sprite-versus-sprite overlaps across priority groups (e.g. Golden Gate towers,
  frames 7715-8372). Emulating the hardware sprite buffer per group makes every frame exact
  (9,646 of 9,648; only frames 0-1 differ).
- Clip windows are used (bitmap layer, windows), so they are modelled as per-row drawn spans.
- No blending ever applies in practice (blend enabled but never matches a layer pair).
- No zoomed tilemaps, NBG1/NBG3, sprite flips, shadow sprites or sprites from RAM occur.

Consequences:
- Phase A: direct path (depth = group band + list order). Exact in 94% of race frames.
- Phase B: sprites in cross-group conflict are rendered through per-group sprite buffers
  (render to texture in list order, other groups erase), then composited at their bands. A
  bounding-box conflict test fires in about 98% of frames, so it must be limited to the sprites
  that really conflict, or use pixel-accurate tests.

## Phase A implementation (8 Nov 2026)

Files (ports/dreamcast/pvr/): drawlist.[ch] (frame description), render_emit.[ch] (texture
building, dirty tracking, sprite texture cache, quad emission; portable), pvr_backend.[ch] (KOS PVR:
OP backdrop, PT tiles and sprites, TR shadow pens; 2x scale into 640 x 448 at y 16).

- Tile planes: NBG 1024 x 512 and text 512 x 256 ARGB1555 non-twiddled textures; a cell is
  reconverted only when its word, tile bank, palette bank version or mixer base/shift change;
  bitmap rows when their video RAM or the palette changes. Strips per row span, merged when rows
  continue each other (NBG0 is one quad; the road one strip per line).
- Sprites: LRU texture cache keyed by graphic, colour/table, group setup and the versions of the
  palette banks the sprite can use. Shadow pens get a second texture drawn translucent black 50% at
  the sprite's depth (GEQUAL, no depth write).
- Colour offsets: positive via the offset colour (exact); negative via vertex modulation
  (approximate: the PowerVR cannot subtract), so fades differ slightly from the board.
- Host check through the same emitter (RT_CHECK_OUT=... with the DL_CHECK build): frames without
  colour offsets differ in about 0.9% of pixels (attract mean 673 px of 71,680), all on zoomed
  sprite edges: the board's zoom steps are truncated fixed point, so pens chosen at exact texel
  boundaries differ by one; not reproducible with float interpolation in one direction for all
  zooms. The real chip samples at 2x anyway.

Flycast 2.7 result (14:36 run, emulated timing): attract and demo races render recognisably
(title, Greek village demo race with road strips, music select). About 25 game frames per second in
the last window (62 frames per 150 vblanks) against 2 with the software mixer. Per frame: 40 ms
total; PVR preparation including waiting for the previous scene 13 ms; sound end-of-frame 4 ms;
present 0; remainder about 23 ms (game logic and the board, not yet split). Sprite cache: 30-39 hits,
0 builds per frame in steady attract; 7 cells reconverted per frame; 1.1 MB texture memory free.

## Next

1. Split the remaining ~23 ms (game logic, bus access, sound CPU) with timers; real hardware timing.
2. Phase B: sprite groups through per-group buffers where sprites really conflict.
3. Exact negative colour offsets if fades look wrong in practice (palette-side conversion).
4. Confirm the 7 cells per frame are genuine changes (animated tiles) and not signature churn.

## Frame-time work (8 Nov 2026, afternoon)

Measured split before (Flycast, attract): 40 ms frame = sound CPU + FM/PCM synthesis 20.3 ms,
PowerVR preparation 7.5 ms, draw-list build ~5.5 ms, game logic ~6.5 ms, PowerVR wait 0.

Changes, each checked on the host (frame comparison unchanged; game and sound-board state
hashes identical over 4,000 race frames where the sound path changed):
1. S32_SOUND_SILENT (Dreamcast default until AICA output exists): chip registers, timers and busy
   times run; samples are not synthesised. Saves ~9 ms.
2. sound_cpu.c board_time(): (cycles * 5) >> 1 instead of a 64-bit multiply/divide (a library
   call on SH4) for each of ~4,900 Z80 blocks a frame. Sound CPU 11.5 -> 4.4 ms.
3. Clip-window spans cached by window registers; draw-list build ~5.5 -> ~0.4 ms.
4. Tiles resident in RAM: tiles4.bin (tools/pack_tiles4.py, two pens a byte, 4 MiB) loaded at
   boot; no tile disc reads while playing. Sprite cache 4 MiB with 32 KiB pages (a race touches
   111 such pages against 282 of 8 KiB). Heap left: ~3.2 MiB.
5. Plane textures: only visible cells scanned; per-cell 16-colour tables; a 1 MiB RAM cache of
   converted tiles, because the game double-buffers NBG0 by switching pages (281 cells every other
   frame). Page flip cost 18.9 -> 6.3 -> ~4 ms, store-queue uploads (sq_fast_cpy) for whole tile
   rows and sprite textures; plane phase max now 5.3 ms.
6. Palette scan by words (518 -> 159 us); compiled PVR headers cached per texture; sprite texel
   conversion memoised per pixel value; sprite rows read in one cache call.

Result (Flycast, emulated timing): steady attract stretches run at 60 fps with frames vblank-bound
(sound CPU 4.4 ms, PVR work 2-3 ms, game logic ~2.4 ms). Remaining slow windows are scene changes
and sprite-heavy demo races: a sprite texture build that misses the sprite cache reads the disc
synchronously (single builds up to ~32 ms). On a real GD-ROM each scattered read costs a seek, so
these stalls would be worse on hardware.

## Next decisions

- Sprite data loading: asynchronous page loading (loader thread, sprite shown a frame or two later),
  and/or per-route prefetch into the 3 MiB of free heap; measure on hardware.
- Audio: the YM3438 and MultiPCM synthesis cost ~9 ms a frame on SH4 even before output. Options:
  (a) keep software synthesis at a lower output rate with an optimised FM core; (b) MultiPCM voices
  mapped onto AICA hardware channels (samples in sound RAM, ADPCM) with FM in software; (c) music
  pre-rendered from the user's ROM and streamed from disc, sound effects synthesised. Choose before
  starting the AICA work.

## Audio plan input: MultiPCM sample working set (host census, 8 Nov 2026)

Built with -DS32_PCM_CENSUS (runtime/src/assets_media.c, off by default), ORUNNERS_PCM_CENSUS=file.
Sample ROM present: 4,174,638 bytes. Played per complete route (race_to_goal, routes 22, 24, 25,
27, 28, 29): 1,837-1,901 KiB of 1 KiB blocks each; union of all seven 1,996 KiB. Attract was not
captured (time limit). The samples are 8-bit; as 4-bit Yamaha ADPCM the union is about 1 MiB, so the
whole used set can be loaded into the AICA's 2 MiB sound RAM at boot, leaving room for the driver
and the software FM stream: no disc streaming for audio, so audio adds no disc stalls. Raw 8-bit
(about 2 MiB) would not fit beside them. MultiPCM and AICA are both Yamaha designs (octave plus
10-bit pitch, AR/D1R/DL/D2R/RR envelopes, LFO), so voice registers map closely.

## Audio stage 1: MultiPCM on the AICA (8 Nov 2026, implemented)

- Bank switching: the chip's 2 MiB space is a fixed first MiB of the ROM plus two 512 KiB windows
  (1-1.5 MiB, 1.5-2 MiB) whose ROM page the bank register (port B0) selects. Host census: bank changes
  6 times in a whole race session (game start, race music, ending).
- Sound RAM layout: KOS driver; aica_fixed.bin (548,354 bytes: the fixed MiB's 77 distinct sample
  starts as AICA ADPCM, PCMS 2, which restores the decoder state at each loop per MAME aica.cpp);
  two 512 KiB window copies (8-bit) reloaded from pcm.bin on bank changes. Free after setup: 548,384
  bytes (for the FM stream later).
- Encoder: source/tools/dreamcast/aica_adpcm_encode.c, closed-loop look-ahead (depth 4) against
  the AICA decode; SNR over the bank 25.5 dB (greedy 22.2 dB). tools/make_aica_pcm.py writes
  aica_fixed.bin and aica_index.bin (run with AICA_ENCODER=build/aica_adpcm_encode).
- Runtime: ports/dreamcast/aica_pcm.c, fed by a new observer in runtime/src/sound_chips.c after every
  MultiPCM write (sound_chips_pcm_observer); 28 AICA channels reserved with snd_sfx_chn_alloc; level
  glides stepped per frame; LFOs not mapped yet.
- Test input: assets/autoplay.txt on the disc (build/autoplay.txt when packaging) drives the controls
  with the headless test scripts; this ROM's attract mode plays no sound.
- Flycast (16:15 run, race_to_goal script): 1,122 key-ons by frame 3,500, 5-15 voices sounding,
  6 page loads, 1 key-on with no matching ADPCM stream, no load failures; per-frame cost about 7 us.
- Reference audio from the host's software emulation, same script, frames 2,100-4,200:
  build/host-run/wav/race-reference-race-start.wav (FM + samples) and
  race-pcm-only-race-start.wav (samples only; -DS32_FM_MUTE host build), 55,555 Hz stereo.

Open: boot is slow in Flycast (stage 2, opening media incl. the 4 MiB tile load, about 34 s; stage 5
sample upload about 4.5 s): reads should use larger contiguous transfers. Race frame rate is 40-60 fps
with dips from sprite texture builds and sprite disc reads (sprite loader is the next video item).
