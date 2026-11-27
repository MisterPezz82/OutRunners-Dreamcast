# The console build: what it will change, and in what order

The port builds two games from the same decompiled program, as segasonic-port does:

- **OutRunners Arcade**: the decompiled game as it is, the same frame by frame as the reference (both
  cabinet screens side by side, coins, the test mode).
- **OutRunners**: the console version, the arcade game made for playing at home. The game's functions
  `console/replaced.txt` lists get the console's own versions (`console/*.c`, `ORUNNERS_CONSOLE`); their
  definitions in `src/` are written as `CONSOLE_ORIGINAL(name)`, which is `original_name` in the console
  build and the game's own name in the arcade one, so the arcade build runs the same program.

What changes the decompilation (src/, symbols.txt, tools/, runtime/) is done in outrunners-decomp and
merged here; this repository adds console/, the ports and their documents.

## What the game has to build on

- **Two seats, two screens.** Seat 1 is the left screen (screen block 208000, I/O chip 1, the window's
  left picture, the keyboard's player); seat 2 the right one (218000). Each runs its own frames, at 30 a
  second, swapping tasks every VBLANK; they share video RAM (seat 1 draws on NBG0 and NBG2, seat 2 on NBG1
  and NBG3) and the text layer. Both screens are 320 x 224 (video control bit 15 clear).
- **A one player game** takes the seat whose start is pressed; the other screen waits for challengers
  (game mode 7, link.c) for the entry time, then goes back to the title. **Two players** on one cabinet:
  the second start in the entry time joins the same race (race_entries_update); the first car past a fork
  takes everyone that way.
- **Sound**: each seat's requests go to their own command byte and their own copy of the MIDI handler;
  seat 1's notes are panned to the left speaker only, seat 2's to the right (pans 7 and 9, nothing on the
  other side). The two seats share the MultiPCM's 24 voices: a hidden seat that plays can take voices from
  the one shown.
- **Coins**: with free play (coin setting 26, `free_play` FF) coins are not counted and every seat's
  prompt is TURN THE IGNITION KEY with its key; the credit line says FREE PLAY. There is no PRESS START.
- **No debug mode**, used or unused. What there is: the test mode's settings (below); `flip_screen` (in
  the EEPROM, no test mode item); the staff credits code in the car presentation (shift down twice, up
  twice, left, right, left, right, brake, gas); unused, a kanji text viewer on the bitmap layer, a timing
  counter on the text layer, and the FBI and EPA notices unpacked at boot and never shown.
- **The test mode's settings**: GAME ASSIGNMENTS: difficulty 1-8 (the traffic), time difficulty -5..+5
  seconds by stage (PRE-STAGE, STAGE 1-4, FINAL STAGE; the program reads them one stage early and never
  the last), steering wheel reaction (the wheel's motor), advertise sound; COIN ASSIGNMENTS; NETWORK
  ASSIGNMENTS: stand alone, master or slave, cabinet 0-3, ISDN; volume (the six analog controls' range);
  SOUND (the 122 sounds); BOOKKEEPING; BACKUP DATA CLEAR.
- **Pictures**: the title (two scrolling tilemaps, the logo, its oval board, SEGA and (C) SEGA 1992 as
  sprites); the ranking's course panoramas across both screens; the route map's world map (two zoomable
  tilemaps); the car presentation's checked backdrop; car select's frames and turntable; the name entry's
  certificate; the WELCOME OUTRUNNERS letter. Nothing shows at boot but the cabinet's LCD (SEGA
  Enterprises).
- **The map**: 31 stages (0 the pre-stage, then levels of 2, 4, 6 and 8 stages, then the ten goals 15-1E,
  Australia to Kenya); each fork has two branches, by the side of the road (fork_next_stages). Everything
  that loads a stage reads the screen's first_stage: the demo races already start on any stage, a game
  always on 0 (the checkpoint count, the goal test, the route map and the start time assume it).
- **The link**: up to four cabinets (cabinet 0-3, master and slaves), 8 players, through the link board
  (EPR-15033); the board model has it with no other cabinet on the line (runtime/src/machine.c, MAME's
  simulation).

## The guidelines (Sebas, 29-10-2026)

1. **The build.** console/ and console/replaced.txt; the core's decompiler (arcade-decomp) writes
   CONSOLE_ORIGINAL for a game's console list beside BOARD_ORIGINAL for the board's (nothing changes where
   a game has no list); `OutRunners` and `OutRunners Arcade` (windows, .apps), a headless console build
   for scripted checks with `--actions` (the console's actions, for screenshots and checks); an icon of our
   own (tools/make_icon.py), nothing of the game's in the executables. As SegaSonic NSP and SegaSonic APK
   do, the Switch's and Android's makers take a picture of the player's own as the icon (icon.jpg beside
   them, 256 x 256): Sebas's is the arcade flyer's art, which stays out of the repository (Sega's).
2. **One screen.** The console shows seat 1's screen, the left one, and not the right one, whose task
   still runs (the program runs both seats). 2 PLAYERS shows both, side by side or one over the other (an
   option: HORIZONTAL, VERTICAL).
3. **One sound.** Both speakers get what is heard (the mix of the chips' outputs, runtime/src/sound_chips.c,
   after the hashes): in one player games the seat shown, in 2 PLAYERS both; the hidden seat kept silent at
   its source, so that it takes no voices.
4. **No coins.** Free play always, ARCADE too: no INSERT COIN and no credit line; the ignition key prompt
   stays, as the game's own start prompt.
5. **The SEGA presentation.** Before the title, a screen of its own: the title's SEGA (the sprite
   `title/title_pictures` row `sega_logo`, 64 x 16, 4 bits), large with the board's sprite zoom, its
   letters dark; cars race across it, as the title's logo cars race across the logo, and the letters
   light up as they pass, with the sound of an engine starting (sound 21, as a game starts); then, once
   its roar is over, the game's own voice of SEGA's (the test mode's sound 107: sound 58 of sound set A,
   the sound program back on its set 0 after it), and the title.
6. **The title's menu**, over the title (the logo and the palm beach), where INSERT COIN was:
   - ARCADE: the arcade game, one player, free play.
   - 2 PLAYERS: both seats, head to head (the game's own two player race), the second screen shown.
   - DELUXE, the modes of our own:
     - TIME ATTACK: a course or a route against the clock, best times kept.
     - FULL: every course, level by level: the pre-stage, then the 2 of the first level, the 4 of the
       second, the 6, the 8 and the 10 goals, 31 in all (stage 0 to 1E, the map's order), in one race:
       whichever branch the player takes at a fork leads to the next course, and the goals fork to the
       next one as well, as REVERSE's do, but the last.
     - REVERSE: the route from the hardest stages to the easiest. The goal it starts at is picked as
       SegaSonic's STAGE SELECT picks a level: on the route map (the game's own, its world map and its
       routes), each goal a marker, and the route shown from the top down, from the goal to the
       pre-stage; each fork leads back a level (first_stage for the start). The courses run as they
       are, not mirrored.
   - OPTIONS (Sebas):
     - CONTROLS: the buttons: each action's keys and game controller buttons, with Kenney's icons, kept
       in a file.
     - DIFFICULTY: the test mode's 1-8 (the traffic).
     - TIME: the test mode's seconds by stage (-5..+5: PRE-STAGE, STAGE 1-4, FINAL STAGE).
     - SOUND TEST: the game's sounds and songs.
     - VIDEO: WINDOW, FULL SCREEN, PIXELS, WIDE, the 2 PLAYERS layout.
     - VOLUME, as SegaSonic's.
     - BACK.
   - EXIT.
   OPTIONS over the title's opening: the palm beach in the dusk, dark, with the cars going by. DELUXE's
   pages over the ranking's course panoramas or the route map's world map.
7. **Video.** PIXELS as in SegaSonic: sharp, smooth (sharp bilinear), plain bilinear, and CRT (scanlines,
   a phosphor mask, a little vignette). WIDE: a 16:9 picture as SegaSonic's: the board's 416 pixel mode,
   the game drawing 48 more pixels at each side (the road, the tilemaps, the sprites the game leaves out
   past the edges).
8. **Kenney** (CC0): the letters of the window's own messages (Kenney Mini, as SegaSonic's
   ports/sdl2/font) and the controls' icons (Input Prompts Pixel, ports/sdl2/prompts) in CONTROLS.
9. **The ports**: the same as SegaSonic's: Windows, Linux and macOS from the CI with a package each; the
   Nintendo Switch (an .nsp made on the player's computer by OutRunners NSP), Android (an APK made by
   OutRunners APK, touch controls) and Anbernic's H700 handhelds. Of the devices, only the Switch has
   2 PLAYERS, side by side only; WIDE on the Switch and Android, not on Anbernic's handhelds (4:3).
   Android's touch controls, as few as the race needs (Sebas): the wheel, gas and brake; the gear
   buttons only with the manual transmission chosen in the car select (none with the automatic); the
   music (the radio) a small oval button at the bottom.
10. **Network**: up to 8 players, each on a device of their own: the game's own link of four cabinets.

## Done (29-10-2026)

- The build: CONSOLE_ORIGINAL in the core (arcade-decomp, its console-original branch), console/, `OutRunners`
  and `OutRunners Arcade`, orunners_console_headless with `--actions` (tests/console/). The arcade build is
  the same as the reference in the scenarios.
- One screen (the left seat's; both in 2 PLAYERS), one sound (sound_chips_join_sides; the right seat's
  requests dropped while it is not in a game), free play (load_settings), no credits line, no coin or
  operator's switches.
- The SEGA presentation (presentation.c): the title's SEGA four times its size on black, dark under a
  shadow that the three cars racing across it take away, the engine (sound 21), the fades; START skips it.
  The boot goes straight to it: no NETWORK CHECK on the screens (network_check_stand_alone).
- PRESS START in the attract mode (the prompt's message and ignition key end at once); the title's menu
  under the logo; OPTIONS over the title's opening, darkened: CONTROLS (two columns, Kenney's Input
  Prompts Pixel drawn by the window, controls.ini), DIFFICULTY by name, TIME by stage, VIDEO, VOLUME, SOUND
  TEST. DIFFICULTY and TIME reach the races at once (console_settings_to_race) and the EEPROM.
- 2 PLAYERS: both seats in one race, the window's two screens side by side or one over the other.
- PIXELS: sharp, smooth, bilinear, CRT. WIDE.
- & KNUCKLES (DELUXE's name, Sebas): TIME ATTACK with the fastest run's ghost, see-through, while it is on
  the player's route (ghost.c); REVERSE from the goal picked, its start line in the pre-stage's colours
  (the video's extra palette) and its finish where the pre-stage's fork was; FULL, the 31 courses in one
  race, its total time at the goal (routes.c); REVERSE's goal picked on the route map of the race's end,
  whole, its ten goals, a car driving to the one picked (route_select.c); CLASSIC, the arcade's game as it
  is (WAITING FOR CHALLENGERS, the right seat's START joins, its screen shown).
- The menu's song (Magical Sound Shower) and sounds (the name entry's letters); one player with no
  WAITING FOR CHALLENGERS (waiting_banner_show); WIDE on every screen; the pause in a race (Escape, Enter,
  a game controller's Start): the frame in grey, PAUSE, CONTINUE and QUIT GAME in the game's letters (the
  video's overlay of sprites).

## Done (30-10-2026)

- The devices' menus (console/menu.c): no VIDEO on a game device, its picture fixed (WIDE on the Switch,
  4:3 on Anbernic's handhelds, smooth pixels); CONTROLS on Anbernic's handhelds only; 2 PLAYERS on the
  Switch only, side by side; no EXIT where the system leaves games (the Switch).
- The Switch (ports/switch): the game as an .nro with the pack in its romfs and as the .nsp's pieces,
  devkitPro's toolchain; the game's two tasks on stacks of their own without ucontext (runtime/src/tasks.c,
  aarch64: the Switch and Android); OutRunners NSP, SegaSonic NSP's with the pack made from the ROM set
  (ports/switch/nsp/rom_pack.c); tools/check_nsp.py. Runs on Eden (an emulator), the .nro and the .nsp.
- The video draws the screens the window shows alone (s32_video_show_screens): the left one in a one player
  game, its layers, sprites and mixing; the headless runs draw both.
- OutRunners Anbernic (ports/anbernic/maker), as OutRunners NSP (Sebas): PORTS for the handheld's card from
  the game's Anbernic build (build/bin/anbernic, no assets in it) and the player's ROM set, written beside
  the tool or straight into the card's Roms.
- Android (ports/android): libmain.so and the APKs with the NDK and the SDK's tools, without Gradle; the touch
  controls (ports/sdl2/touch.c: the wheel, GAS, BRAKE, START, the gears with the manual transmission alone,
  MUSIC a small oval at the bottom); OutRunners APK, SegaSonic APK's with the pack made from the ROM set.
- The project's icon (tools/make_icon.py): a steering wheel on a sunset, for the builds without a picture of
  the player's own; since 1.0 Sebas's car on that sunset (ports/sdl2/icon/car.png, a picture he took from a
  Steam page that offers it free to use). Sebas's own (local/, never committed): the car for the computer, the flyer for the
  Switch, the title's oval for Android.
- REVERSE's map: no pause over it (it is the race's step 5), so START confirms the goal.
- WIDE 16:9 and 20:9 (Sebas): OPTIONS > VIDEO's WIDE OFF, 16:9 (48 columns more at each side, 416) or 20:9
  (88, 496); Android takes 20:9 where its screen is twice as wide as high or more, the Switch 16:9. 16:9's
  frames the same as before (but VIDEO's line); REVERSE's map kept within its right edge at 20:9.
- REVERSE's time turned round (Sebas: its first course, a goal's, the longest, ran out at 40 seconds): the
  start 99 seconds, the clock's most beside FINAL STAGE (the setting the car adds to its 40, for that one
  start), and the 59 more taken back from the last checkpoints (the pre-stage's gives nothing, the first
  level's 13): the race's whole time the game's own, its margin at the start.

- The computers' executables' icon: the project's wheel (ports/sdl2/icon: a resource on Windows, which SDL
  gives the window, the .apps' on macOS, a picture beside them on Linux); on Windows a picture of the
  player's own where the computer has one (local/icon_pc.png or ORUNNERS_PC_ICON, made an .ico as CMake
  configures): Sebas's, the car.
- The CI (.github/workflows/build.yml, as SegaSonic's): the Switch's .nsp pieces, Android's base APK and
  Anbernic's game built each in its own job, in every computer's package beside OutRunners NSP, OutRunners APK
  and OutRunners Anbernic, with docs/package/README.txt; a tag v* makes a release of the packages.

- Android on a tall screen (Sebas's Fold open): the picture at the top, the touch controls in the room under it,
  as large as it lets them, and between the wheel and the pedals the game's own route map (Sebas: the game's
  pictures, nothing made up): the right seat, unseen with one player, shows the close-up map the game drives
  after a race, its car as far along the route as the race's (console/side_map.c), and the port draws that
  screen there; a phone's screen as before. ORUNNERS_TOUCH_TEST: the touch controls in a computer's window,
  the mouse a finger; the headless build's --side-map.

- The route map fills the room under the picture, behind the controls (Sebas: none of it black), the part
  around the car. 2 PLAYERS on a tablet or an open Fold (Sebas): offered in the title's menu where the screen's
  short side is 4 inches or more, the two screens facing each other, the first player's on the bottom half
  and the second's on the top one turned half round, each with its own touch controls, turned with it.

- Android (Sebas's Fold 7): WIDE chosen again when the screen changes shape (unfolded or folded mid-game),
  the log saying each new size; GAS and BRAKE a quarter smaller in both layouts (Sebas: too big).

- NETWORK (Sebas: four devices as the four cabinets, HOST and JOIN, ARCADE only; every device against every
  other, computers, phones and the Switch alike): console/network.c and its sockets (console/net_udp.c:
  Winsock, libnx's BSD sockets, POSIX). A room hosted sends a beacon to the local network every half second;
  JOIN lists the rooms found and joins one; the room's page shows its devices with their players' names
  (NETWORK > NAME, kept in outrunners.ini) and the host's START, which links them in the list's order (the
  host machine 1, the others 2-4; NETWORK > PLAYERS 2 where 2 PLAYERS is offered: both seats) and presses
  each one's START: the game's own entries, car select and race, as linked cabinets. Every device sends its
  two race records to every other each frame, as the link board would (link_receive_records and
  link_send_records in place of the board's); every datagram written byte by byte. A device unheard for a
  third of a second holds the race (WAIT, the frame in grey); for 3 seconds it is gone (Sebas: as out of
  time): its players taken from the race's order, their cars out of sight, their records sent on by the first
  device still heard, and a race whose host is gone hosted on by its first player still heard from the host's
  last record (its time, the time left, the passes, the order); a room whose host is gone, by the next
  device of its list. Checked with two headless games on one computer (--real-time, tests/console/network_*):
  a race of both to its end and back to the room, the joiner gone mid-race, the host gone mid-race. Its pages
  in the menus' font, in its colour 0E0, white to pink, the line chosen 0C0 (Sebas: not white, yellow and
  orange). Only the room's devices are heard, each at the address it was first heard at; the room's list
  from its host alone; a record's numbers that index the game (players, checkpoints, stages, cars, their
  animations, the course records) kept within its tables before the game reads them.

## Done (31-10-2026)

- WIDE's background through a stage's change (Sebas, and a player on Reddit: the clouds cut after the
  pre-stage): the game swaps its four pages a column at a time while the view's turn keeps that column out
  of its 320, and WIDE's sides showed the half-swapped pages. WIDE keeps the background whole: the change's
  first set through the left column's swap, its last from the right one's on (routes.c); the turn reaches
  only about 0x2A8, so waiting for more turn could not hide it.
- FULL's and REVERSE's background changes (a player's report: glitches between the stages): the game picks
  the order its columns are swapped in by the fork's branch, which a route gives for its stage's
  background, so 13 of FULL's 33 changes swapped the column in sight at once (the clouds turned into the
  city in a frame, 4:3 too). The order now the view's turn's (turn_background_pages, routes.c): all 33 and
  REVERSE's 5 out of sight, as the game's own.
- The route map beside the race (a tall screen, the Fold opened) in CLASSIC too: its right seat joins while
  the race takes entries, before the race and its map.
- CLASSIC 5 seconds more than ARCADE in every part (Sebas: friendlier, and apart from ARCADE): at the start
  and at each checkpoint (console_classic_time).
- OPTIONS: STEERING (the sticks' DEAD ZONE and SENSITIVITY, the keys' and pads' SPEED; issue 1), FPS (the
  frames a second at the picture's top right and in a computer's window's title, its letters the port's
  own 3 x 5), the version at the bottom right (CMake's project version, the devices' packages' too); the
  signature only on the title.

## Done (01-11-2026)

- CONTROLS' triggers (issue 2): a trigger is one more button of the bindings (CONTROL_LEFT_TRIGGER and
  CONTROL_RIGHT_TRIGGER, past SDL's own; "lefttrigger" and "righttrigger" in controls.ini), bound pressed
  halfway; by default the right one the gas and the left one the brake (Sebas), which they press as far as
  they go, from any travel; bound to any other control, it holds it from any travel past its rest's wobble.
  Before, they pressed the pedals always and CONTROLS could not take them. A control keeps three buttons; a
  key or button bound leaves the player's other controls. controls.ini's version 2: a file without it
  (1.1.0's) gets the triggers on the pedals after its own buttons, as they were.
- CONTROLS' pictures of L3, R3, the Guide button, the triggers and Share (Kenney's sheet): those bound
  showed an empty place, and the pedals' a trigger whatever they were.
- The pause's RESTART (issue 4; Sebas: through the car select): the game ends as QUIT GAME's and starts
  again as the menus started it (ARCADE, 2 PLAYERS, TIME ATTACK, FULL, CLASSIC, REVERSE from the same goal,
  its route map skipped), once the left seat is at the title; on the car select (car_select_run, the
  console's) the turntable is at the car chosen before from its start and the console confirms it and the
  transmission chosen before as START would, the wheel held where it picks them: about 7.5 seconds from
  RESTART to the start line, the game's own scenes. Not in NETWORK (the other devices' races); CLASSIC's
  challenger joins again with START. A game a snapshot could not restart: the game's two tasks run on
  native stacks. Checked with the console headless (--restart N): ARCADE with another car and the manual
  transmission, REVERSE, 2 PLAYERS.
- The reference's build on a newer GCC (issue 6): emu.h of arcade-decomp's stand-ins takes <cmath> itself
  (the V60's float conversions), and a text with nothing to put in it no longer goes to snprintf as its
  format.

## Done (04-11-2026)

- Android's WIDE by the screen (issue 15: a 4:3 handheld got 16:9, the picture at the top and the route map
  under it): none on a screen narrower than 3:2, the game's 4:3 picture filling it, but a tablet's, whose
  16:9 keeps the controls and the route map under the picture (an open Fold's, Sebas). A tablet's screen is
  known before the window too (the first display's), for WIDE's first choice.
- OPTIONS > STEERING's WHEEL ROTATION (issue 16: a G29 at 900 turned 450 each way to the end): a racing
  wheel's turn from end to end as its driver has it, 270 (the arcade's: a stick's whole travel, as before)
  to 1080; the game's wheel all the way at 135 each way, the rest past it, the dead zone a part of those
  135. Saved in outrunners.ini (wheel_rotation).
- STEERING's INPUT TEST (Sebas: as the arcade's test mode has it): player 1's wheel, gas and brake as the game
  sees them (the screen's, through the calibration), each a mark on a bar of 16 cells as the test mode's
  input test draws them, and how far in percent (Sebas: not its hexadecimal), to see a racing wheel's ends and
  the pedals' floor; the wheel's mark the way it turns (the game's left is FF), its bar and its -100% to 100%
  the port's travel (EF-0F as the game sees it). A page of its own: the wheel's axis is the menus' arrows
  too, which there do nothing.
- The sprite graphics' lookup between the video's threads (issue 17: a segmentation fault in sprite_word on
  a build without optimization, the README's on Linux, whose CMake has no build type): the block the last
  lookup found is a hint the threads share, and a build that read it again after checking it could index
  another thread's block. Read once now (runtime/src/assets_media.c). It took the first run's assets: they
  leave the sprite chips' empty runs out (all ones, as the board reads them), five blocks, where
  extract_assets.py's are one and never change the hint; both draw the same.

## Done (05-11-2026)

- OPTIONS > STEERING's SET UP WHEEL (a T300RS on Windows, reported on Reddit: the game took no input from
  it): the game reads controllers as SDL's game controllers alone, and SDL has no layout for many wheels
  (it knows a T300RS for a wheel, not where its pedals are). The page asks for the wheel turned left, the gas
  and the brake pressed and let go, and the buttons for shift up, shift down and start
  (ports/sdl2/wheel_setup.c watches every device's axes and buttons meanwhile, each kept open as a
  joystick); a pedal's rest says its axis (an end: the whole axis, inverted at the positive one; the middle:
  combined pedals, half each). The layout is SDL's line for the device: added to SDL, the wheel player 1's
  game controller (the one before it player 2's), INPUT TEST next; written to gamecontrollerdb.txt in the
  saves folder in place of the device's line before, the file's other lines kept. Both games read that file
  as SDL starts (SDL_HINT_GAMECONTROLLERCONFIG_FILE), so a layout made elsewhere works too. Escape cancels.
  A computer's only (ORUNNERS_HANDHELD has none). Checked with SDL's virtual joysticks: a wheel with no
  layout through the six steps, separate and combined pedals, set up again (one line kept), a new start
  reading the file, and the window's game with a virtual pad and wheel through the menus (Escape on the way,
  the wheel player 1's, its turn and pedals on the seat), on Windows and on Linux (SDL 2.30). Built on Windows
  (MSVC) and Linux (gcc) without warnings; macOS's files compiled with clang (arm64, x86_64), its link
  left to the CI. Not checked on a real wheel.

## Done (06-11-2026)

- OPTIONS > STEERING's CENTRING and VIBRATION (the T300RS's player on Reddit: with no force the wheel never
  came back to the centre): the cabinet's wheel had a spring of its own, and the board's one force is the
  wheel motor, on or off (output port D bit 4 of each seat, written by the VBLANK while the test mode's STEERING
  WHEEL REACTION is on and the seat is not in the attract mode: a crash, off the road). ports/sdl2/forces.c,
  each frame for each player's game controller: SDL's spring at CENTRING (the wheel's autocentre where it has
  no spring), a sine of 40 ms at VIBRATION while the seat's motor runs, a game controller's rumble where the
  device has no sine; held in the pause and a network wait; nothing opened with both at 0 (SDL opening a
  haptic turns the device's own autocentre off). Both at 50% at first, kept in outrunners.ini; CENTRING a
  computer's line, VIBRATION every build's; OutRunners Arcade at 50% each, its test mode deciding the shake.
  The board's outputs were already kept (s32_board.outputs): nothing in src/ or runtime/ changed. Checked on
  Linux (SDL 2.30) with a virtual wheel of uinput logging every effect: the arcade game off the road (the
  spring at 50%, the sine on and off with the motor, all erased at the end), the console's with outrunners.ini
  at 30/70, 0/0 (no haptic opened) and 0/70, a wheel with an autocentre and no spring, a pad with rumble only,
  and forces.c alone through changes while it runs (the spring updated, erased at 0 and made again, the sine
  updated while playing, the pause's hold, the haptic let go at 0/0 and opened again). Not checked on a real
  wheel nor on Windows' DirectInput.
- Merge outrunners-decomp up to ba27c10 (the core at arcade-decomp dfc18d9, console-original f9a4594): each
  value at its own width on a big-endian processor, the board's memories in the host's order, the program's
  data laid out as the code reads it. src/ written again with console/replaced.txt; console/routes.c reads
  and writes the background's page word as its two halves (the union gone from struct tilemap_pages). The
  first run's layout written again (extract_layout.py: the tables the decomp split, background_tile_page_lists
  and its packed pages, the backgrounds of four scenes...), and with it the assets' version: a computer's
  first-run folder is taken out again, and a device's pack made before is refused (another version: its
  package is made again with this version's tools). The pack keeps the data as a little-endian host holds it,
  with no units (only a big-endian host's chip_byte reads them). Checked: the 16 scenarios identical; the
  first run's folder against extract_assets.py's, no difference; OutRunners Arcade's window (three video
  threads) on coin_start and two_players, its state, picture and sound the reference's; a pack written and
  two_players run from it with three video threads, the same; a pack made before, refused.
- NETWORK over arcade-lan (lan/, a submodule: github.com/cheviman/arcade-lan, Sebas: one network for the
  ports), its linked game (LAN_LINK, made for this one from these rooms beside TMNT's lockstep): arcade-lan
  keeps the rooms (the beacons, JOIN, the list with where the host hears each device), each device's block to
  every other each frame (the newest kept), a device unheard (heard false) and gone after 3 seconds for the
  rest of the game, a room taking devices while its game goes on, the next device hosting a room whose host is
  gone, and what a device says of itself (lan_set_status: BUSY, a seat in a race, which START waits on).
  console/network.c keeps the game's part: the link's set-up at START (the game's devices in the room's order,
  the host machine 1), the block (the records a machine carries: its own two and a gone one's), each
  machine's taken from the one that carries it (publisher) through check_record (now into a whole struct
  race), a machine gone carried by another, the race hosted on. console/net_udp.c and net.h gone; the
  datagrams' version 2 (a device of 1.2.0 sees no room of one before). Checked with two headless games
  (--real-time, tests/console/network_*): a race of both to its end and back to the room; the joiner gone
  mid-race (160 frames of WAIT, then its car out of sight, the race on to its end alone); the room's host
  gone (the other hosts the room); the race's host gone (the other hosts the race on from its last record,
  its time and time left, and the room). arcade-lan's tests (lan_test, lan_link_test: four devices, a tenth
  of the datagrams lost) on Windows and Linux; built on Windows (MSVC) and Linux (gcc) without warnings.
- The README for the player (959 lines to 122, Sebas: too specific): what the game is, playing it, the
  console version, the controls, NETWORK, building it; the decompilation's own README stays
  outrunners-decomp's, linked (a merge of the decomp keeps the port's README as it is). arcade-lan cited with
  its license, in the packages' licenses/ too.

## Done (07-11-2026)

- OPTIONS > VIDEO's VSYNC (issue 19: tearing on macOS; the port never waited for the screen, its frames paced
  by a timer alone): on at first, kept in outrunners.ini; OutRunners Arcade's always on; a device's screen as
  its SDL shows it (Anbernic's 2.0.12 has no SDL_RenderSetVSync), and off for --fast. The renderer shows each
  frame at the screen's refresh (SDL_RenderSetVSync) and the game keeps its own pace, 60 a second
  (wait_for_frame): a faster screen shows each frame twice or so. A frame a whole frame's time behind that pace
  (a screen a little slower than 60, 59.94 Hz, or the machine busy) is not shown, so as not to wait: the game
  and its sound keep their pace, a frame left out now and then. The pause and a network race's WAIT pace
  themselves the same way (wait_for_frame, not SDL_Delay(16)), as the network counts its time in frames. The
  log says the screen's refresh, the frames left out and the pace. Checked on Windows on a 144 Hz screen:
  1800 frames of a race, none left out, 59.935 frames a second (the start's first frames); the VIDEO page
  drawn. Not checked on macOS, nor on a 60 Hz screen. Interpolated frames for 120 Hz (issue 18) wait for the
  video on the GPU (the Switch's 4 PLAYERS branch).
- The phone vibrates with the cabinet's wheel motor (Sebas: the phone in a crash), for the players on its touch
  controls (no game controller of theirs): forces_device opens the device's own vibrator, which SDL lists on
  Android as a haptic named VIBRATOR_SERVICE (rumble only), and vibrates at VIBRATION's strength while a touch
  player's seat's motor runs: 250 ms asked at once and again every 10 frames (Android starts a vibration over
  at each call), stopped with the motor, in the pause and at the end. STEERING > PHONE VIBRATION (Android's,
  on at first, phone_vibration in outrunners.ini) turns it off; the manifest asks for VIBRATE (SDL's Vibrator
  throws without it). Built for Android (arm64, the base APK, with VIBRATE in it) and Windows without
  warnings; checked by Sebas on a Galaxy Z Fold 7 (an APK made with OutRunners APK): "anda perfecto".
- VIDEO's WIDE 16:10 (Sebas: a Steam Deck's 1280 x 800), as TMNT's: 32 columns more at each side
  (S32_VIDEO_WIDE_SIDE_16_10, frames of 384), the window showing 358 (16:10 at 224 rows); WIDE_16_10 after the
  others in the enum (outrunners.ini keeps the number), VIDEO going through them narrowest first (OFF, 16:10,
  16:9, 20:9). Android's WIDE by the screen: 16:10 from 3:2 up to 16:9 (a tablet's still 16:9, its controls
  under the picture). The headless's --wide-16-10. Checked: the same frames of a FULL race (the start, the
  pre-stage's fork, stage 1) in 16:10 and 16:9 as the window crops them, the sides drawn whole; VIDEO from OFF
  one step right to 16:10; built for Windows, Linux and Android without warnings. Not checked on a Steam Deck.
- The route map beside the race (Sebas, on the Fold 7 opened: the sea and the landmarks with no land under
  them, since 1.2.0): the decompilation's merge made PAGE_SELECT a view of halfwords, and side_map.c (and
  route_select.c, REVERSE's goal) wrote a route map's page select as one word, its upper pages left as they
  were (route_select's by chance the right ones). console_page_select writes both halves, as route_map_update
  does. Checked: the headless's --side-map frames of a race show the close-up map whole again, REVERSE's map
  the same as before; by Sebas on the Fold 7 opened (an APK made with OutRunners APK): "perfecto".
- CLASSIC with one player has no WAITING FOR CHALLENGERS (Sebas: a wait for no one, the right seat's screen
  unseen): the entries close once the choices are made, as in ARCADE; a challenger joins while the left seat
  chooses (the right seat's START), and only then does the game wait, for the challenger's choices.
  tests/console/classic.txt: the race starts at once (its 45 seconds, CLASSIC's 5 more); with the right
  seat's START in the car select, a race of two.

## Done (09-11-2026)

- The devices in packages of their own (Sebas: not inside the computers' packages): a release has six,
  the three computers' with their two games alone, and outrunners-switch.zip, outrunners-android.zip and
  outrunners-anbernic.zip, each the device's game without assets beside its tool for Windows, macOS and
  Linux (OutRunners APK.exe, "OutRunners APK (macOS)", "OutRunners APK (Linux)": the three find the device's
  folder and font.bmp beside them), the licenses and a README of its own (docs/package/README-*.txt). The
  CI's build job leaves the tools in a kit (outrunners-OS-kit.tar.gz, which keeps their permissions) and the
  job devices packs the zips (ports/pack_devices.py), the programs as Unix entries with their permissions.
  Checked: the script on this computer's builds, the Windows tools started from the unpacked packages (the
  APK and PORTS made, the device's folder found beside them); the CI's run before a release.

## Done (11-11-2026)

- Merge outrunners-decomp up to 0a03f7f (the core at arcade-decomp 5714a73, console-original 2c54367): the
  scenarios that cover the code identical on PowerPC, each value at the width the C reads it (toward a port
  to a big-endian system). src/ written again with console/replaced.txt, the same as the merge left it. The
  race's master_time_difficulty is one array of six bytes (its three fields before): console.c and routes.c
  take it so, from the first race copy as the car does. The first run's layout written again (the decomp
  split turntable_scenery_items, ending_actors, goal_actor_scripts and game_assignments_value_texts off
  their neighbours), and with it the assets' version: a computer's first-run folder is taken out again, a
  device's pack made before is refused. FULL's and REVERSE's control scripts poke the race's time as a byte
  of its halfword (poke ADDRESS VALUE HALFWORD), as the decomp's routes do. Checked: the 16 scenarios
  identical, picture and sound; the first run's folder against extract_assets.py's, no difference; the
  console's headless game before and after the merge on tests/console (menu, controls, classic, reverse,
  two_players, time_attack, full: 204,204 frames), its state, picture and sound the same in every frame.
- WIDE's background through a change, the game's own again (a player's video: the canyon stayed behind the
  road to Niagara, then turned into the falls' forest in a frame). Keeping the background whole (31-10) took
  the page that joins the two backgrounds out of the change, and that page is the change: the view comes
  back over it, the old background at one end and the new one at the other. So WIDE showed the old
  background where the game shows the joining page, in its middle 320 columns too, until the last set
  replaced it all at once. Now the game's four page words as it writes them, the last two (the other
  column's swap at a turn of 160, the joining page's at 60) waiting until the turn is back the side's
  columns more, where WIDE's sides are past their column too (wide_background_pages, routes.c): no half
  swapped pages at the sides, which is what keeping it whole was for. The first swap (at 2A0, the turn's
  most about 2A8) cannot wait: the strip a side shows of that column changes to the joining page's as the
  game swaps it. Where a route's change takes the other set (turn_background_pages) the last swap does not
  wait: the joining page is then in the column it was not drawn for, and its swap shows in the 320 columns
  whenever it comes, 4:3 too (FULL's stage 1: San Francisco's skyline gone in a frame; to do). Checked on the
  console's headless game: ARCADE to Niagara by the canyon (tests/console/arcade_niagara.txt and its controls;
  before, 9,932 pixels of the middle 320 columns were not 4:3's in a frame of that change), its two changes
  every 6th frame, and FULL's first 14,000 frames, their seven stretches the same way: the middle 320 columns
  the same as 4:3's in every one, at 16:10, 16:9 and 20:9 (FULL's at the last two), the sides whole to the
  eye at 20:9; 4:3 itself the same as before in 12,000 frames, picture and state. Not looked at in the
  window, nor on a device.
- The spray of Niagara's falls see-through (the same video: the falls blinked; Sebas). The game draws that
  sprite on the background every other frame of its 30 a second so that it looks faint
  (backdrop_translucent_update: the spray, the falls far off in the forest, the pyramids of stages 13 and
  20), two of the screen's frames on and two off; the reference does the same, and so does a longplay of the
  arcade. The console draws it every frame, half its colour and half the colour under it, as the ghost's
  car (S32_SPRITE_GHOST): backdrop_translucent_update in console/replaced.txt (src/ written again: its two
  lines), the console's keeps the handler's count odd and marks the sprites it puts in the list (console.c).
  The arcade build blinks as the cabinet does. Checked on the console's headless game, ARCADE to Niagara: 64
  frames in a row over the falls, the sky's brightness went between 237.7 and 234.6 two frames each (242.1
  and 237.7 on the spray's other picture) and now stays at 235.2 (239.4) through the 8 frames of each of the
  spray's pictures; the picture the same as before until the spray is first in sight (frame 11743). The
  arcade build: the 16 scenarios identical, picture and sound. Not looked at in the window; the pyramids
  not run.
- REVERSE's backgrounds where the game shows them (issue 25: the sky black over a hill, a player's
  screenshots; only the top of each stage's background in sight). The route map where its goal is picked
  (route_select.c) left the background's tilemap zoomed about A0, 70, so the race drew its background 112
  lines lower, its upper pages' empty rows in the sky: the center back at 0 as the map is left, as the game's
  route map leaves it.
- FULL's and REVERSE's palettes as the game's own way to a stage leaves them (issue 25: the lanes eaten by
  orange zigzags, the road red, black blocks in the sky through a change, a hill another colour). A change of
  the game's puts only the colours that are not already the stage before's: the rest stay from the changes on
  the way there (Spain's sky is the pre-stage's to stage 1's). A route comes to a stage from another, or
  starts at a goal: at a fork's change's first step, and at REVERSE's start, the palette is put as the way
  there leaves it (forward_palettes, routes.c: from the pre-stage's start, each change to the next stage of
  the way by stage_backgrounds and each change within a stage of it, their colours and their fades' ends,
  the backgrounds', the road's and the leaving ones'), with the road's fades that way's (road_palette_fades),
  under the new background's own. Checked on the console's headless game: REVERSE from AUSTRALIA (17 frames
  through the race) and FULL (46, one every 2,500) against the same with the old palettes, the frames that
  differed better each; at stage 7 the palette's background half (1000-17FF) the same as ARCADE's on the way
  0-1-3-7 over the same hill; menu, controls, classic, time_attack, two_players and arcade_niagara identical
  to the build before, state and picture in every frame. Not looked at in the window, nor REVERSE from the
  other goals.
- REVERSE's finish's kangaroos (issue 25: grey figures in the parade, a player's screenshot). The finish is
  AUSTRALIA's goal at the pre-stage, and its parade's kangaroos take their colours from AUSTRALIA's scene
  palette (scene_palettes, at 4C00; the pre-stage's is at 4A00, apart): put after the pre-stage's as the
  route comes to it (fork_background_update). Checked on the console's headless game: the parade's
  kangaroos as at ARCADE's AUSTRALIA (0-1-3-7-D-15, the sprites' palette there the same); menu, classic,
  time_attack, two_players and arcade_niagara identical to the build before.
- No pause in a race of the network's (issue 34; Sebas: it breaks the race's run): it held this device's car
  alone while the others raced on. console_can_pause is false while the link is on, so START and Escape go
  on as the cabinet's (the radio's >>), as in the race; a network race is left as before, at its end.
  Checked on two console headless games on one computer (--real-time, tests/console/network_*): both
  racing, the pause never offered; CLASSIC's race offers it as before.
- A network race whose machine 1 is gone before it took the entries goes on (issue 34): the others' requests
  waited for good, as machine 1's left seat takes them. The machine that sends its records on takes them on
  in player 0's record as race_entries_update does (take_lost_entries, network.c): once every player still
  heard has seen the last entry, a request is accepted, the first one opens the entries
  (entry_frames_by_machines), all of them in cut them to ENTRIES_CUT, the countdown closes them. Checked on
  two console headless games (--real-time, tests/console/network_*): the host gone 3 frames after its START,
  the other device was in car select for good, now it races alone; gone 20 frames after, as before (the
  entries already taken, counted down); both on, the race as before.
- NETWORK on Android: Wi-Fi's multicast lock while the network is open (console_network_open), without
  which some phones drop the rooms' beacons (issue 34): android_multicast_lock (ports/sdl2/main.c, JNI
  through SDL's activity), CHANGE_WIFI_MULTICAST_STATE in the manifest. The APK built here; not run on a
  phone.
- SET UP WHEEL with the pedals and a shifter on devices of their own (issue 28: a wheel, pedals and a shifter on
  USB each; the pedals never taken, no force feedback). The wheel's game controller layout stays as it was,
  made of what is on the wheel's device; the steps watch every device, and what they take on another one
  goes to wheel_extras (ports/sdl2/wheel_extras.c, wheel_devices.txt beside gamecontrollerdb.txt), read by
  the wheel's player beside its game controller as its triggers and buttons are. That wheel is player 1's
  as it comes, whatever device came first (its forces with it), and those devices are no player's game
  controller. A wheel with every control its own writes no line: as before. Checked with SDL's virtual
  joysticks (a wheel, pedals, a shifter; and one device with everything): the steps take each control where
  it is, the layouts as before but for what is elsewhere, the pedals' travel and the buttons read back from
  the file. Not tried with a real wheel, pedals and shifter.
- An update no longer stops at "the assets pack is of another version of the game" (issue 37: macOS, after
  1.3.0). The computers' console game writes its assets as a pack (outrunners.assets) beside the folder of
  what it keeps the first time, for the devices' tools, and the search took a pack before the assets'
  folder: after an update, the old build's pack for good. That pack is now tried first, and one that is not
  taken is removed and written again from the folder (or from the first run's again, where the folder is
  the old build's too). Checked on Windows with the pack's tables' version changed: the pack made again and
  the game on; with the folder's version.txt changed too and the ROM set beside, the assets taken out again;
  a good pack loaded once. Not checked on a Mac.
- SET UP WHEEL takes an axis for SHIFT UP, SHIFT DOWN or START (issue 28: a handbrake): of the axes no step
  took (the wheel's, the pedals'), the one pulled and let go, as a pedal is taken; held past the middle of
  its travel, it is the button held. On the wheel's device, SDL's button on an axis in its layout
  ("leftshoulder:a3"); on another, wheel_devices.txt's "start GUID a3 half inverted", the buttons' lines as
  before. Checked with SDL's virtual joysticks: pedals with a handbrake as START (the gas pressed meanwhile
  never taken), a wheel's own handbrake as SHIFT DOWN, held at a third and past the middle; the two cases
  before, the same files.
- outrunners-decomp merged (13-11-2026): the state in the V70's order on any processor (the bookkeeping by
  halfwords, two lists of halfwords at odd addresses read as they are, the output test's bits by byte), the
  EEPROM and the bookkeeping saved low byte first (the same files as before on the PC); the core at
  arcade-decomp 20996ed, console-original 8cf9d7e, src/ decompiled again with console/replaced.txt. The 16
  scenarios identical, picture and sound; the console's headless game the same before and after on
  tests/console (menu, controls, classic, time_attack, two_players, arcade_niagara, full, reverse: state,
  picture and sound) but OPTIONS' version; the assets pack's version the same.

## Done (15-11-2026)

- The pause's QUIT GAME leaves the race's sound off (issue 39: the engine went on at the title, and the
  race's song till the menu's): game mode 1 goes to the title without turning it off, which the arcade never
  needed, as it comes there from the ranking, whose first step does it; check_game_start's QUIT GAME now does
  what that step does, the wheel motor off and reset_screen_sound (RESTART, which goes through it, too).
  Checked with the console headless's new --quit N: ARCADE to Niagara, QUIT GAME at frame 6000 with the gas
  down, the sound the same up to it and then silent from frame 6100 (it went on before); tests/console the
  same before and after (state, picture and sound).
- OPTIONS > VIDEO > WIDE AUTO, the computers' default (issue 40, as the Simpsons port's): WIDE as the shape of
  the screen the window is on asks (wide_for_screen, Android's: 20:9, 16:9, 16:10 or off), chosen again where
  it changes, every frame (a Steam Deck's 16:10 in its hands, 16:9 docked to a TV; the window on another
  monitor), the window sized again for it; what draws and sizes the picture reads console_wide_in_effect.
  outrunners.ini keeps 4 for it; a file that says another keeps it. Checked on Windows (a 3072 x 1728
  monitor): a first start in AUTO, "screen 3072x1728: WIDE AUTO -> 16:9" and the window 796 x 448; with
  wide=0 the window 640 x 448; VIDEO shows AUTO, then OFF; tests/console the same but menu's VIDEO page, WIDE's
  line AUTO. Not tried on a Steam Deck, a 16:10 screen or with two monitors.
- The radio's hidden song, Jingle Bell (Sebas): the game plays it for << and >> pressed together, which a game
  controller's pad (both on it) cannot; console_radio_buttons presses the other one with >> past Last Wave and
  with << before Magical Sound Shower and leaves the car's song at 10, which the game's next << or >> steps
  from (to Last Wave or Magical Sound Shower). Checked on the console headless (the window port's buttons
  through it too): TIME ATTACK, >> twelve times and << ten times, the titles going round with Jingle Bell
  between the two.
- RecompLauncher (Sebas, its guide recomplauncher.com/sdk-files/recomplauncher-agent.md): recomplauncher.json
  at the top of the computers' packages (build.yml), levels 1-3: the player's orunners.zip (game), the console
  game started with --game {game} (start), --prepare --game {game} on its setup screen (prepare), and
  outrunners.ini's video settings, volume and FPS on the game's page (config, settings), the saves' files and
  folder; the README's badge (the port is in its catalog as outrunners-port). In the window ports, only with
  the new arguments: --game SET, the first run's set first (else the window as before, with what was wrong);
  --prepare --game SET, the assets out of it with no window and before SDL starts, "progress 0.1 ..." lines,
  0, 2 without --game, 3 when they did not come out; outrunners.ini's lines read with spaces around '='
  too ("wide = 4"), as a launcher may write them. Checked on Windows: the file valid against the guide's
  schema; --prepare 0 in 0.6 s, 3 with another file, 2 alone; then the game starts with no window between;
  --game with no assets takes them out and plays; with no arguments and no set, the first run's window as
  before; "wide = 0" and "  window_scale =  3" read (960 x 672). Not tried in RecompLauncher itself, on
  Linux or on macOS.
- The Recomp Standards the port did not meet (RecompLauncher's guide), but the achievements (Sebas): the
  computers' games write their log, FILE_NAME.log in the folder of what they keep, the two runs before as
  .1.log and .2.log (stderr, left on a terminal where one shows it); portable.txt beside the game (beside the
  .app) keeps what they write there on Linux and macOS too, as on Windows; RecompLauncher's Big Picture
  (RECOMPLAUNCHER_MODE=bigpicture) starts the console game full screen, not written to outrunners.ini; a
  DualShock's or a DualSense's buttons on CONTROLS are its own (SDL's type of it: cross, circle, square,
  triangle, L1, R1, L2, R2, from Kenney's sheet the game has, each symbol in two tiles), and with no
  controller RECOMPLAUNCHER_PAD=playstation; the release's packages carry its version in their names
  (outrunners-v1.3.2-windows-x64.zip; the CI's "dev" else), the folders in them as before. Checked on
  Windows: four starts, outrunners.log, .1.log and .2.log, the newest first ("OutRunners 1.3.1, the date");
  Big Picture's window 3072 x 1728, the screen's; CONTROLS (keys posted to the window, PrintWindow) with RT,
  LT, RB, LB and Y, and with R2, L2, R1, L1 and a triangle; the devices' packer's names with and without
  --version. portable.txt and the log on Linux and macOS not tried (the CI builds them); a real DualSense
  not tried. The ROM set picked with a dialog waits for SDL3 (its branch).
- The R36S on ArkOS (pull request 41, pauloedsfer's: ports/r36s, ORUNNERS_R36S with ORUNNERS_ANBERNIC): the
  Anbernic build against glibc 2.30, PortMaster's launcher, SELECT + START leaving the game, and a frame past
  its time not drawn (ORUNNERS_FRAMESKIP) so that the game and its sound keep their 60 a second on the RK3326.
  Its package, outrunners-VERSION-r36s.zip, the release's seventh (Sebas: each device its own): the CI's
  anbernic job builds both handhelds (a matrix), and OutRunners R36S, OutRunners Anbernic's sources with
  ORUNNERS_R36S_MAKER, makes the folder ports for the card's EASYROMS of the folder r36s beside it (the same
  files and assets pack as PORTS, the launcher PortMaster's); docs/package/README-r36s.txt. The release job
  takes the computers' packages and the devices' zips alone (the Anbernic's and the R36S's games have files
  of the same names). Checked on Windows: both builds compile with Zig (glibc 2.29 at most and 2.34), OutRunners
  Anbernic's texts the same as before, OutRunners R36S's ports with the R36S's launcher and game and the same
  assets pack as OutRunners Anbernic's, the packer's seven zips. Tried on an R36S by its author; the package on
  one not tried.

## Done (19-11-2026)

- A racing wheel's spring gone a couple of bends into a race, the shake still there (issue 46, a Logitech
  G920): the spring was run once, for a length that is not for ever on Windows nor on macOS (SDL gives it in
  microseconds in 32 bits: SDL_HAPTIC_INFINITY * 1000, 71 minutes, of which a wheel's driver may keep less),
  while the shake is run again each time the motor starts. The effects are run over and over
  (SDL_HAPTIC_INFINITY times) and the spring is looked after (forces.c, keep_spring): run again within a
  second where the device says it is not playing, every 10 seconds whatever it says, and on the first frame
  after the game was held. Checked on Windows with a fake haptic (the spring's runs counted over the frames:
  made once, run again at 600 and 1200 frames, within 60 of a status of not playing, on the frame after a
  hold; a wheel with no status and an autocentre one). On a wheel not tried: the G920's cause is what SDL's
  source says of the length, not seen on one.
- OPTIONS > STEERING > SPEED's INSTANT, past 9 (issue 45): the keys' and the pads' wheel all the way at once
  while one holds it and at the centre at once when let go (STEERING_SPEED_INSTANT, outrunners.ini's
  steering_speed=10; 9 takes 11 frames to the end). Checked: the page shows < INSTANT > after 9 and 1 after
  it (the headless's frames); the wheel's jump in a race not tried.
- 2 PLAYERS with WIDE on: each screen's place in the window was as wide as WIDE's (398 or 496) and its 320
  sat between black bars, both smaller than the screen had room for (issue 44: a small box each on a 21:9
  monitor). The places are the board's 320 in 2 PLAYERS (screen_width). Checked on Windows, WIDE 16:9: the
  window 796 x 448 at the title, 1280 x 448 in 2 PLAYERS, both screens edge to edge (keys posted to the
  window, PrintWindow). The touch layouts and the devices not tried.
- WIDE 21:9 (issue 44: 20:9 left a bar of 4% at each side of a 3440 x 1440 monitor): 108 columns more at
  each side, frames of 536 (S32_VIDEO_WIDE_SIDE_21_9, S32_VIDEO_MAX_WIDTH; the video's WIDE is the port's, the
  decompilation's runtime has none), shown whole (3440 x 1440 is 535 columns, 2560 x 1080 is 531). WIDE_21_9
  after AUTO (outrunners.ini's wide=5; wide_width_kind where the kinds were tested as under AUTO); VIDEO's
  WIDE goes AUTO, OFF, 16:10, 16:9, 20:9, 21:9; a computer's AUTO takes it where the screen is 2.3 times as
  wide as high or more (a 32:9 one has it between bars), Android's stays at 20:9; the headless's --wide-21-9;
  recomplauncher.json's choice. A layer is 512 columns a page: where the game's scenery is one page with
  nothing beside it, its end is in sight sooner than at 20:9 (FULL's frame 3500: the mesa's left edge 14
  columns in, 6 outside 20:9's view), as 20:9 shows it when the view turns.
  Checked on Windows: the 31 courses of tests/console/full.txt at 21:9 and at 20:9, a frame every 2000 (64):
  the middle 496 columns of each the same as 20:9's, the sides looked at one by one; the title, OPTIONS and its
  pages, the car's choice and ARCADE's start at 21:9; the window 1072 x 448 with wide=5; 4000 frames of ARCADE
  at 4:3, 16:10, 16:9 and 20:9 with the same hashes as a build of 496; the arcade's 16 scenarios identical. Not
  tried: an ultrawide monitor (AUTO's choice is by the numbers: 3440 x 1440 and 2560 x 1080 21:9, 2400 x 1080
  20:9), the route's map and the ending at 21:9, Linux and macOS.
- 32:9, tried in the headless with the widest frame at 796 (not kept): the road and the ground go on, the
  roadside's sprites end where the game stops placing them and the layers' scenery is cut and comes round
  again. See To do.

## Done (24-11-2026)

- CONTROLS' key or button takes the control's place (issue 56: << given another button, the pad's down still
  went back a song): controls_bind_key and controls_bind_button leave the control that key alone, or that
  button alone (a key the keys, a button, trigger or stick's way the buttons); before, it went in front and
  the others stayed. The page shows all the control's buttons, three at most (CONSOLE_CONTROLS_BUTTON_STEP;
  it showed the first, and the pedals' A and B worked unseen). More than one is controls.ini's, by hand.
- The sticks' ways as buttons (issue 42: the right stick's up and down as the pedals on the Switch):
  CONTROL_LEFT_STICK_UP to CONTROL_RIGHT_STICK_RIGHT past the triggers, the axes' buttons (controls.c's
  axis_buttons: each its axis and way), "leftstickup", "leftstickdown", "rightstickup", "rightstickdown",
  "rightstickleft", "rightstickright" in controls.ini (the left stick's left and right are the wheel's). On
  the pedals they press as far as they go, as a trigger does, past STICK_REST (4000 of 32767); on any other
  control they hold it past its wobble (AXIS_PRESSED). CONTROLS takes one pushed halfway
  (controls_axis_button); its pictures are Kenney's sheet's (rows 12 and 14). The Switch has no CONTROLS
  page: its controls.ini, by hand (ports/switch/README.md).
- CONTROLS' pictures with WIDE on (since WIDE came, 30f8aff; every computer's since 1.3.2's AUTO): drawn the
  side's columns to the left of their names; console_controls_place counts the side.
  Checked on Windows: controls.c against an SDL virtual controller (40 checks: the bindings replaced and
  saved, the other controls letting go, the stick's travel to the pedals past its rest, the names read and
  written, which axis CONTROLS takes); the page in the window with WIDE 16:9 and OFF (keys posted to the
  window, PrintWindow) with the sticks' pictures and three buttons; every Windows target, the Switch's and the
  Anbernic's with no warnings. Not tried: a pad's stick on the page, a Switch.
- REVERSE's finish's kangaroos (issue 29): not wrong. The ones that cross in front of the crowd are
  AUSTRALIA's goal parade (goal_parade_leftward_update, goal_parade_rightward_update: back and forth for
  ever, from the view 96 segments before them); the comparison took REVERSE and ARCADE at the same frame,
  but REVERSE's race (tests/console/reverse.txt) comes to its goal 146 frames later. ARCADE through
  0-1-3-7-D-15 (route_branches and route_stages put after race_progress_update, a build of its own): every
  object of the finish the same (record, place from the view, side, x_offset), the parade starting and
  moving the same frame by frame, and at the same time after the goal (REVERSE's 22000-22500 against
  ARCADE's 146 before) every sprite's pixel the same; the background, the road's colours and the HUD the
  pre-stage's. The pair 02E1A6 is the one at 256 either side, the same in both.
- The decompilation merged up to 3dc97bf (the game's variables in the host's memory, its state as typed
  structures, the objects' handlers as functions' numbers, the tables and lists of code out of the ROM; the
  core console-original 2f6dba3 on arcade-decomp 3dc3c5a). What the console follows: the race record's fields
  in its state (race->state; the car's model and choice one halfword, RACE_MODEL and RACE_SET_MODEL), the
  settings in eeprom.settings, the route's progress in screen->progress, FUNCTION_ for the functions'
  addresses. The six variables struct world keeps that the console sets (player_count, link_off, link_failed,
  car_numbers, send_video_registers, wheel_motor) are set in both world and the work RAM's copy: the copy
  alone compiles and does nothing (the race crashed at its eighth frame, no players). The ghost's and the
  start line's own handlers are called by number; the start line's objects of the console's streams get the
  console's handler after the game's stream reads them (it now takes CONSOLE_CODE_START_LINE, no function's
  address, as NO_FUNCTION); call_function (runtime/src/game.c) hands a number past the table to
  call_code_elsewhere, the console's objects. Checked on Windows: every target with no warnings, the arcade's
  16 scenarios identical, tests/console's eight identical to the build before in state, picture and sound
  (TIME ATTACK with an empty ghost folder each: a ghost left by one run changes the next). A start line
  object whose record START_LINE_KINDS (16) leaves out does nothing in REVERSE, as before.
- WIDE's sides and the game's edges (as The Simpsons' and TMNT's ports had in players' reports): the game takes
  objects away, stops drawing them, turns them back or puts them in a little past the board's 320, which WIDE
  showed inside the picture (Niagara's spray and the landmarks cut at the sides in every WIDE, the title's palm
  trees short of the right edge at 20:9 and 21:9, the road's crossers taken away in sight at 20:9 and 21:9). The
  decompilation names those places (278e004: PAST_LEFT, PAST_RIGHT, FROM_MIDDLE, support.h) and the console
  moves them by WIDE's side: console_set_sides, each frame where the ports set the video's side, sets
  screen_more_left and screen_more_right too (0 with WIDE off and in 2 PLAYERS). backdrop_off_screen, whose A0
  the decompilation keeps (the middle and the edges' distance at once), is the console's (replaced.txt): off past
  the wider screen's side, but the game's own while the sprite waits to come in (state 0), or a backdrop born
  in a side would stay away from the middle. The headless's --middle-hashes (each screen's 320 columns) and
  --board-edges (WIDE's frames with the board's edges) check it. Checked on Windows: every target with no
  warnings; tests/console's eight at 4:3 identical to the build before the merges (state, picture, sound);
  ARCADE, FULL, REVERSE and the menus at 16:9, 20:9 and 21:9 against --board-edges: the player's screen's middle
  the same but the title (its palm trees put in past the wider side, its logo's cars taken out past it, so the
  logo shines a few frames later), the goal parade (turned back 16 columns past the wider side, as the board's
  past its own) and the landmarks (their height, which the game keeps only while it draws them, a pixel apart);
  Niagara's spray now at the sides, the middle the same. The right screen is the title all along. Not tried:
  the devices' builds, a monitor, the network.

## Done (25-11-2026)

- SDL 3 and the Raspberry Pi (issue 47; Sebas, 19-11-2026: "primero pasarlo a sdl3 o el mas reciente que sirva con
  todo, y agrega una version de raspberi 3 en adelante. y va como regla para todos los ports"; on 25-11-2026, what
  issue 47 is: "sdl3, ios, pi"). TMNT's port did it first (its 1.1.0); this follows it.
- The window port is written for SDL 3 now (ports/sdl, which was ports/sdl2), port_sdl.h what its files include.
  SDL 3.4.20 (03-11-2026), CMakeLists.txt's ORUNNERS_SDL_VERSION, its release's archive by its SHA-256 (the one in
  build/downloads/sdl3-source where it is there); ORUNNERS_STATIC_SDL was ORUNNERS_STATIC_SDL2.
- Each platform: Windows, Linux, macOS and the Raspberry Pi, SDL 3.4.20 from its source inside the games (the
  packages); Android, the same from its source, libSDL3.so beside libmain.so, its Java from its android-project; the
  Switch, devkitPro's own branch of SDL 3 (github.com/devkitPro/SDL, switch-sdl-3.4 at c329016: SDL 3.4.0, not
  released; devkitPro has no switch-sdl3 package yet) built into the game by switch.cmake, by its archive's SHA-256;
  Anbernic's H700 handhelds and the R36S keep their system's SDL 2 (the firmware's 2.0.12, ArkOS's) under the same
  source: ORUNNERS_SDL2_LAYER, ports/sdl/sdl2_layer.h (TMNT's, with what OutRunners adds: the haptic part forces.c
  uses, the right stick's axes, a texture turned, the display's mode, the joysticks' list, the processors' count),
  the stand-in library with SDL_malloc and SDL_RenderFillRect more, and the build's last step
  ports/anbernic/check_sdl.py (no SDL function newer than 2.0.12 asked for).
- The move: SDL's own rename scripts over the sources, then by hand what changed underneath: bool results; float
  rectangles (float_rect in main.c, whole pixels as before); the window and the renderer made with no place and no
  flags (Direct3D 11 is SDL 3's first on Windows); full screen as a bool (SDL 3's is the desktop's mode); the events'
  fields; joysticks, gamepads and haptics by their ids (SDL_GetJoysticks, SDL_GetHaptics); the sound as a stream on
  the default device (SDL_OpenAudioDeviceStream, 1024 sample frames asked of the device as before, the queue's bytes
  by SDL_GetAudioStreamQueued); SDL's base path SDL's own (base_copy); the tools' window handle from its
  properties; SDL_main.h included by main.c (the tools, with a main of their own, SDL_MAIN_HANDLED); Android's
  tablet by the display's content scale (SDL_GetDisplayDPI is gone); an iPad by SDL_IsTablet.
- What the compiler did not say, found and set right: SDL 3 gives an ARGB texture blending by its alpha, and the
  game's frames have none (the picture would be black): texture and scaled SDL_BLENDMODE_NONE. SDL 3's joystick id
  is unsigned and 0 is none: forces.c's -1 and id < 0, and wheel_setup.c's -1 (SET UP WHEEL's "nothing set up"),
  are 0. SDL_GetTouchDevices gives an empty list, not NULL, with no touch screen. The touch controls' shapes kept at
  whole pixels. SDL's rename script had made __APPLE__ SDL_PLATFORM_APPLE in two files that include no SDL
  (ports/android/apk/key.c, ports/switch/nsp/folder_dialog.c): put back.
- The Switch (TMNT's issue 27 on SDL 3): devkitPro's SDL 3 loads EGL only for a window made with SDL_WINDOW_OPENGL,
  and its Switch window is an EGL surface: without it the game stops at its start. And F11 or Alt+Enter on a USB
  keyboard leaves the Switch's window as it is (never full screen: the 1920 x 1080 desktop mode, the picture's top
  left alone; the checklist's point for OutRunners).
- The computers' log says SDL's version, the renderer and the drivers: "SDL 3.4.20, renderer direct3d11, video
  windows, audio wasapi".
- What a player sees changed, as TMNT's port measured it on the same SDL (not measured here): on Windows the
  window is DPI-aware (with the desktop at 125% WINDOW 3 is its real pixels, sharper and smaller); a Nintendo
  controller's buttons by their place (SDL 3's SOUTH, EAST...).
- Checked on Windows (MSVC, every target with no warnings): OutRunners Arcade's and OutRunners' windows run with
  control scripts (attract; ARCADE's race to Niagara in both games; 2 PLAYERS), SDL's dummy video and sound, --fast:
  the same frame by frame on SDL 2 (the same commit built beside it) and on SDL 3, state, picture and sound, 53,604
  frames. The package's build (ORUNNERS_STATIC_SDL=ON): no SDL3.dll and no C runtime asked for, Windows' own
  libraries alone. rom_in_executables: the build's games 0; the package's 4 stretches of "program", 1 of "sound" and
  4 of "pcm", which are SDL 3's own tables inside them (steps 0a 0a ... 0b 0b ... 0c; 00 ... 01 02 03 ... 14; the
  same games with SDL beside them: 0); tools/rom_in_executables.py's trivial() does not know those shapes (the
  decompilation's tool).
- Every port source compiled by Zig against SDL 3.4.20's headers with every warning an error, for Windows, Linux,
  macOS and Android's branches (the NDK's clang).
- The Switch: devkitPro's build, OutRunners.nro and the .nsp's pieces, "SDL-3.4.0" and its Switch video in it.
- Android (NDK 28.2, SDK 35): outrunners-base.apk and this computer's signed one, apksigner v2 and v3; aapt:
  io.github.cheviman.outrunners, SDK 24 to 35, arm64-v8a with libmain.so and libSDL3.so.
- Anbernic and R36S (Zig, the layer over SDL 2.0.12): 86 SDL functions each, the newest of SDL 2.0.12.
- The layer run: the Anbernic's game built natively on Linux (WSL's GCC, the same configuration; it links the
  stand-in library and loads the system's SDL 2.30 as it starts), with SDL's dummy video and sound and ARCADE's race
  to Niagara: 16,001 frames the same as on Windows with SDL 2, state, picture and sound; its log by the layer
  ("SDL 2.30.0, video dummy, audio dummy", the screen's mode, the joysticks).
- Not played by hand yet: the sound heard, game controllers and racing wheels (the haptic part), the pause, VSYNC.
  Not tried on a device: a Switch, a phone, a handheld, a Mac, a Raspberry Pi.
- The Raspberry Pi (3 and later, the 64-bit Raspberry Pi OS): ports/raspberrypi (TMNT's, with OutRunners' names):
  build.sh builds and packs outrunners-VERSION-raspberrypi-arm64.tar.gz (the two games, font.bmp, input_prompts.bmp,
  outrunners.png, recomplauncher.json, README.txt, LICENSE.txt and licenses/), on Debian 12 "Bookworm" for arm64
  (Raspberry Pi OS Legacy's base, glibc 2.36: the games run on it and on Trixie's), SDL 3 inside with Wayland, X11,
  KMS/DRM, ALSA, PulseAudio and PipeWire; the CI's raspberrypi job runs it in debian:bookworm on an arm64 runner;
  wsl.sh makes that system in a folder of WSL's and runs it under QEMU. 64-bit only: every Pi from the 3 on runs the
  64-bit OS.
- Checked under QEMU in that system (wsl.sh): the games ask for libc and libm alone (glibc 2.34 at most) and for no
  SDL function of a library; the package's OutRunners Arcade takes the assets out of orunners.zip (--prepare, the
  first run on arm64); with SDL's dummy video ("SDL 3.4.20, renderer software"), attract (18,001 frames) and
  2 PLAYERS (3,301) the same frame by frame as on Windows with SDL 2, state, picture and sound. Not tried on a
  Raspberry Pi: the screen (Wayland, X11, KMS/DRM), the sound, game controllers, the speed on a Pi 3.
- iOS and iPadOS (issue 47; TMNT's port's, on SDL 3, and outrunners-port's pull request 14, yoshisuga's, the first
  iOS version of the game, on SDL 2, played on an iPhone on iOS 27.2): OutRunners, the console game alone, as on
  Android, for an iPhone or an iPad (arm64, iOS 13 on), an unsigned .ipa in outrunners-VERSION-ios.zip
  (ports/pack_devices.py --ios, docs/package/README-ios.txt), sideloaded (AltStore, SideStore, Sideloadly), never
  the App Store. ports/ios: ios.cmake (CMake's Xcode generator, SDL 3.4.20 static with no HIDAPI, the target
  orunners_ios_games), Info.plist.in, LaunchScreen.storyboard (black), Assets.xcassets (the project's car,
  tools/make_icon.py, which writes it now), README.md. The app takes its assets out of the player's orunners.zip
  in its Documents (main.c's ios_assets, the extractor linked: ORUNNERS_IOS), which the Files app shows; with none,
  its message on the screen and in outrunners.log there. main.c's SDL_PLATFORM_IOS: the log, the settings and the
  records in Documents, the whole screen at its pixels, landscape, the home indicator dimmed, the sound as
  AVAudioSession's playback, the screen kept on, WIDE and the picture between the sides the notch or the Dynamic
  Island takes (ios_side_inset), nothing drawn in the background (an event watch) and a race paused there, an
  iPad as a tablet (SDL_IsTablet).
- iOS's scenes: iOS 27's SDK asks every app for UIKit's scene life cycle, and one without it does not start (pull
  request 14 found it; SDL 2 has none, and it brought its own ios.m). SDL 3.4 has it itself (its app delegate gives
  the scene SDLUIKitSceneDelegate): the Info.plist declares UIApplicationSceneManifest, with
  UIApplicationSupportsMultipleScenes false and no configuration of the port's own. TMNT's port's iOS has no such
  key yet.
- iOS's tasks: runtime/src/tasks.c (the port's own; the decompilation's has fibers and ucontext alone) switches the
  game's two tasks by their stacks on iOS as on the Switch and Android, Apple's ucontext being "No longer
  supported" (pull request 14's way), with Mach-O's names (_host_switch_stacks, no symbol types nor sizes); macOS
  keeps ucontext.
- NETWORK on iOS: arcade-lan at ad5dfe7 (was bd84786; its protocol still 2, so the devices of 1.3.2 play with it),
  built with ARCADE_LAN_NO_BROADCAST on iOS (the rooms found by datagrams to each address of the local network:
  iOS gives an app no broadcasts without Apple's entitlement); its two tests pass.
- The CI's jobs ios (macos-15: the app and its .ipa, read back: the Info.plist's keys, arm64 for iOS, the launch
  screen and the icon in the bundle, no assets in the .ipa) and ios-simulator (the app started with no ROM set: its
  log must say where orunners.zip goes, a screenshot of it); the devices' job packs outrunners-VERSION-ios.zip; the
  release's packages are nine.
- Checked here, with no Mac: ports/sdl's main.c, letters.c, touch.c, forces.c and controls.c compiled by Zig for
  aarch64-ios and aarch64-ios-simulator with its Darwin headers and SDL 3.4.20's, every warning an error, iOS's
  branches in them (the message, the hints, SDL_main); the whole game for both (runtime/src, src/, console/,
  arcade-lan: 81 sources, no warning; runtime/src/byte_order.c is the byte order check's, GCC's alone, not iOS's);
  tasks.c: _host_switch_stacks on iOS and its Simulator, ucontext on macOS, host_switch_stacks on Android; the
  workflow's YAML parsed (ten jobs); pack_devices.py --ios with stand-in folders. Not built, not run: Xcode's build
  (the storyboard, the asset catalog, the link), the Simulator, an iPhone or an iPad: the CI's ios jobs, at the
  next push.
- The windows seen (PrintWindow, a DPI-aware capture: the desktop at 125%): the first run's window 640 x 448 in
  real pixels, its letters whole and sharp; OutRunners' title in WIDE 16:9 (796 x 448) and OutRunners Arcade's two
  screens (1280 x 448), the pictures right (the blend mode above).

- The northern lights of stage 26 and the close-up route map's half-rate picture see-through (issue 32): the game
  draws them every other frame, two of the screen's frames on and two off, which blinks as Niagara's spray did.
  aurora_update and route_map_landmark_update in console/replaced.txt (src/ written again: their four lines; a
  line of src/race/race_cars.c the decompiler wrote otherwise on this tree, perspective_scales[depth] read as the
  crash text's fields, kept as the decompilation has it: to look at there), the console's in console.c: their
  counts left as they are, as they also time the animations (animation_advance counts its frame down each call),
  the frames the game leaves out draw the same picture again (draw_object), and every frame's sprites are a
  ghost's (sprites_see_through, which backdrop_translucent_update takes too). Checked on the console's headless
  game, FULL's race (116,000 frames): the sound the same, the picture different from frame 92,073 to 108,324
  alone (stage 26), the state from 92,069 on (the sprite list's words); frames 95,001 and 95,002, two of the
  game's frames off: the lights there now, faint. The arcade build blinks as the cabinet does. Not seen: the
  route map's picture (in ARCADE's close-up route map), a screen.
- OPTIONS > SOUND (issue 57, a player's: the DJ over the radio's songs, the engine and the tyres): VOLUME's line is
  SOUND's page, VOLUME (the whole sound), MUSIC (the YM3438 and the songs' samples, the drums and the
  instruments), EFFECTS (the engines, the tyres, the crashes, the scenery) and VOICE (the DJ and the other
  voices), 0 to 10 each, 5 the game's own, kept in outrunners.ini (music_volume, effects_volume, voice_volume).
  The MultiPCM's samples by what they are: console/pcm_categories.c, made by tools/pcm_categories.py from
  pcm_samples.txt (241 samples: 142 MUSIC, 51 EFFECTS, 48 VOICE, by the folder of their names); a voice takes its
  sample's category as its key goes on (multipcm's category_of, through the banks' pages as they are then:
  s32_pcm_region_address; console_sample_category), each category its gain (multipcm's category_gain, 256 as it
  is) and the YM3438 its own in the mix (sound_chips_set_gains, console_sound_gains: the setting over 5). The
  console headless's --sound-mix MUSIC,EFFECTS,VOICE. Checked: ARCADE's race to Niagara (16,001 frames) at
  5,5,5 the same as before, state and sound; with VOICE 0 the sound different in 1,220 frames alone, the
  voices' (SEGA's at 2 s, the menus' and the car select's, Go! at 61 s, the checkpoint at 181 s), the state the
  same; with MUSIC 0 in 15,306; the windows' runs at 5 each the same as before, state, picture and sound. Not
  heard yet: the mix by ear.
- REVERSE's start at AUSTRALIA without the people over the water (Sebas's: the start line's stands and people at
  both sides, and its left is the harbour): the pre-stage's start line the console puts in where REVERSE starts has
  its rows drawn in pairs (scenery_pair, animated_scenery_pair: one item each side of the road, the second
  mirrored); at AUSTRALIA (WATER_LEFT_GOAL) presentation_start_line_update runs their one side's handlers instead
  (scenery, animated_scenery: the same record and steps, the pair's first item, at the right). The ten goals'
  starts looked at: only AUSTRALIA's has water at a side (the ATLANTIC OCEAN's is under the sea all round). Checked:
  REVERSE from AUSTRALIA (6,000 frames) different from frame 1,969 on, the sound the same, the left there the gate,
  its sign, the balloon, the flagman and the stage's planters before the harbour; REVERSE from JAPAN and CLASSIC
  the same as before, state, picture and sound. Looked at too, and the game's: the kangaroos of Uluru start small in
  the middle (where they stand, at the side of a road narrow in the distance, then jump across as the car comes
  near), the same at 4:3 and in WIDE's middle (20:9, --board-edges: 128 frames the same to the pixel).
- The radio's MUSIC goes round it all (Sebas's: the space bar, MUSIC, went from the car's song to the station and
  back, two of them; the songs were << and >> alone, [ and ]): console_radio_buttons takes MUSIC too, and a press
  of it on a song but the last is >> while it is held (decided as it is pressed: a song that turns the last one
  meanwhile does not make it the game's MUSIC halfway), on Last Wave the game's own (the station, its DJ), and on
  the station the game's own with the car's song set to the first, Magical Sound Shower (any of the station's
  buttons goes back to the car's song). The seat's radio is the object car_radio_read_buttons runs (seat_radio),
  on its songs in its states 0 and 1. The arcade build's MUSIC is the cabinet's. Checked on the console's
  headless game, CLASSIC with MUSIC pressed every 300 frames: the songs 7, 8, 9, the station (states 2 and 3),
  0, 1 and on to 9; the console's menu, CONTROLS, CLASSIC, 2 PLAYERS and ARCADE's race to Niagara the same as
  before, state, picture and sound (none presses the radio).
- The devices' frames' pace goes to the log only where it is asked for (Sebas's, from the main checkout's
  ports/sdl2/main.c, brought to ports/sdl/main.c): with --pace, or an empty pace.txt beside the log (the Switch's
  folder, Android's external storage, the game's folder elsewhere); a line every 300 frames is a write to the card
  while the game runs, which can hold a frame. The Anbernic's and the Switch's READMEs say so. Compiled for
  Windows, Android and iOS.
- 1.4.0: the version (CMakeLists.txt; the examples of the packages' names), docs/releases/v1.4.0.md.

## To do

- 32:9 (issue 44): for one player it would need each stage's scenery drawn further out than the game places
  it; for 2 PLAYERS side by side it is 16:9 a screen, which needs WIDE in 2 PLAYERS (today both screens are the
  board's 320).

- Issue 25's last report, a split second of something wrong from the results to the title: not seen
  pressing START on the route map or the ranking after ARCADE's race (the fades smooth, frame by frame);
  the player to say which screen.

- FULL's and REVERSE's background changes where the view turns the other way than the background's pages
  were drawn for (the fork's branch road the player takes): the turn's set keeps the swaps out of sight
  (31-10), but the page that joins the two backgrounds is drawn for the other column, so the view comes back
  over a cut and the game's last swap changes the picture in sight (FULL's stage 1 at frame 7087 of
  tests/console/full.txt: San Francisco's skyline gone in a frame, 4:3 and WIDE). Without the joining page
  there (the column out of sight straight to the new one's) one edge would cross the view instead. The cut
  at 7087 is also the change's first step: a route's change from a stage to one the game never comes to
  from it (FULL's 1 to 2) takes the background of the game's way there (0 to 2), whose first pages are
  another stage's, not the one left. No page in the ROM joins the two; keeping the old pages in sight
  instead would show them in the new background's colours.
- The network on a network shared with strangers: a room's code to join it (today a device on the same local
  network can fill a room with JOINs or speak for a member it overhears).
- The network: checked between devices (Android's multicast lock on a phone that dropped the beacons; the
  Switch, the Steam Deck) (issue 34).
- (done after 1.0) The decompilation's new version merged (2b4e7a0: the game's state as one structure, C
  pointers into it and into the program ROM's records): the core's console-original at arcade-decomp
  82aadc8 with CONSOLE_ORIGINAL; console/ reads the state through game-> and the pointers screen,
  current_object and player_car; data_elsewhere has its way back (address_elsewhere) and rom_pointer reads
  it, for FULL's and REVERSE's courses. The console's frames the same as before it.
- 2 PLAYERS on the Switch up to 4 players a console (Sebas, 30-10-2026), the screen in four, with the
  network's multiplayer (two cabinets in one device).

## Order

1. The build: CONSOLE_ORIGINAL in the core, console/, the two executables, the headless console build.
2. One screen, one sound, no coins.
3. The SEGA presentation and the title's menu (ARCADE, 2 PLAYERS, OPTIONS, EXIT; DELUXE's place).
4. OPTIONS: the test mode's settings, VIDEO with PIXELS, CONTROLS with Kenney's icons, VOLUME, SOUND TEST.
5. 2 PLAYERS with its two layouts.
6. DELUXE: TIME ATTACK, FULL, REVERSE.
7. WIDE.
8. The first run without Python: the game takes its assets out of the player's orunners.zip itself (a C
   extractor that writes the folder extract_assets.py writes), as SegaSonic's does; then the CI's packages,
   the Switch, Android and Anbernic.
9. The network.
