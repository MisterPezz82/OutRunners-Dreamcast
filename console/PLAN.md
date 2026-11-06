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

## To do

- The network on a network shared with strangers: a room's code to join it (today a device on the same local
  network can fill a room with JOINs or speak for a member it overhears).
- The network: checked on two windows and between devices (Android's broadcasts:
  some phones drop them without a multicast lock, then a room's address typed in); the pause in a network
  race without holding the game (today the pause holds this device's car, the others race on); the entries
  of a race whose machine 1 is gone mid-entries (counted down only).
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
