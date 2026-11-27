OutRunners for Dreamcast - version 0.5
=====================================

A native Dreamcast port of the OutRunners arcade game, built on the C reconstruction of the game
(https://github.com/cheviman/outrunners-port), with its own PowerVR renderer and AICA sound.

THIS PACKAGE HOLDS NO SEGA DATA. There are no graphics, sounds, music or program tables from the
arcade ROMs in it. You make the disc yourself from your own legally owned ROM set, and the disc
you make is for your own use only: do not share it.

What you need
-------------
- Your OutRunners ROM set as MAME names it: orunners.zip (the World set).
- Windows 10 or 11 (everything else is included), or Linux/WSL with python3, a C compiler and
  mkdcdisc 0.0.4 or later (https://gitlab.com/simulant/mkdcdisc).

Making the disc
---------------
Windows: run make_disc.bat and choose your orunners.zip in the window that opens (or drop the
zip onto make_disc.bat).
Linux/WSL: ./make_disc.sh /path/to/orunners.zip

It takes two or three minutes. It checks and extracts your ROM set, converts the data for the
Dreamcast, has the game draw the loading screen's picture from your set, and builds the disc image:
    output\OutRunners.cdi       GDEMU, MODE, emulators (Flycast, Redream), or a CD-R
For GDEMU, copy it to a new numbered folder on the SD card.

Picture output
--------------
    VGA box or cable      480p: the arcade's 320 x 224 picture at exactly twice its size, each pixel
                          and each line doubled by the video hardware: sharp, with no scaling
                          shimmer or blur
    RGB/SCART, S-Video,   240p at 60 Hz, progressive, as the arcade board's own picture, on PAL
    composite             consoles too (at 50 Hz the game would run slower). Your TV must accept a
                          60 Hz signal.

Menus
-----
The title screen has the PC version's menu (press Start):
    ARCADE                the arcade game, one player, free play
    2 PLAYERS             both players in one race, on a split screen (shown when a second
                          controller is plugged in)
    & KNUCKLES            CLASSIC (the arcade's game with a little more time; a second player can
                          join with Start), TIME ATTACK (no time limit, racing your fastest run as a
                          see-through ghost car), FULL (all the stages in one race),
                          REVERSE (from a goal back to the start)
    OPTIONS               DIFFICULTY, TIME, SOUND (VOLUME, and MUSIC, EFFECTS and VOICE each on its
                          own), SOUND TEST, STEERING (the stick's dead zone and sensitivity, the
                          D-pad's steering speed, vibration, an input test), FPS, 2P MUSIC (in
                          2 PLAYERS, player 1's radio alone or both)
In a race, Start pauses: CONTINUE, RESTART or QUIT GAME.

DIFFICULTY sets the traffic and rivals; TIME sets the seconds each stage gives you. If the arcade
game is too hard to finish, turn TIME down (towards -) as well.

2 PLAYERS
---------
The two races are shown side by side, each at the arcade's own 320 x 224, in a 640 x 480 picture
with black bars above and below (480p on VGA; 480i on RGB/SCART and the other cables, in this mode
only). The second player uses the controller in port B (or the next port with one), with the same
controls. Player 1's music plays from both speakers; each player's engine and effects come from
their own side (player 1 left, player 2 right). Large scenery is drawn at half resolution in this
mode, so that two races fit in each frame. The picture changes mode as 2 PLAYERS starts and ends
(a moment of black screen).

Time Attack ghosts
------------------
TIME ATTACK records each run and races your fastest one again as a see-through car. After a run
faster than any before, the game asks SAVE GHOST: SAVE TO VMU keeps it on the VMU (a file called
ORGHOST, about 23 blocks for a five-minute run), DO NOT SAVE keeps it only until the console is
switched off. A saved ghost is loaded when the game starts. To fit on a VMU the run is stored as the
car's place once a second, with the moments in between filled in on a smooth curve, so the ghost can
take a slightly rounder line than you did through a hard corner or a crash. Playing 2 PLAYERS clears
the ghost from memory (it needs the room); TIME ATTACK reads the saved one from the VMU again.

Controls (Dreamcast controller)
-------------------------------
    Analogue stick        steer; moves through the menus
    D-pad left / right    steer (the wheel turns while held); moves through the menus
    R trigger             accelerate (analogue)
    L trigger             brake (analogue)
    A / B                 gear shift; in the menus A picks a line and B goes back
    Start                 start; picks a line in the menus; pauses a race
    Y                     music: goes round the songs, then the radio station, then the first song
    D-pad up / down       previous / next music (every car can play all ten songs; the hidden
                          Jingle Bell is on the way round, between Last Wave and Magical Sound
                          Shower)
    A + B + X + Y + Start soft reset: ends the game and goes back to the title
A vibration pack shakes when the cabinet's wheel motor would (a crash, off the road). The second
controller has the same controls for the second player.

Saving
------
Course records, the game's settings, the OPTIONS menu's settings and the bookkeeping are saved to the first VMU found, in a file
called ORUNNERS (about 2 blocks), and loaded when the game starts. A Time Attack ghost you choose to
save goes in a second file, ORGHOST. Records and settings are saved within
ten seconds of changing; the rest at most every five minutes.

Sound
-----
The arcade board serves two cabinets and sends each seat's sound to one speaker. In one-player games
both sides go to both speakers, so the game's music and effects are centred. 2 PLAYERS: see above.

Niagara's mist
--------------
The arcade draws the mist at Niagara Falls on two frames out of four, which an arcade monitor
blended into a haze; on a modern screen it flickers (it does in the PC version too). Here such
sprites are drawn half see-through every frame instead.

Getting it onto a Dreamcast
---------------------------
The PC port runs the arcade game's logic and draws every frame in software, which a modern computer
does without effort. A Dreamcast has a 200 MHz processor, 16 MB of memory and 8 MB of video memory,
and the game's graphics and sound alone are several times that. Most of this port is the work of
fitting it in and keeping it at 60 frames a second:
- The picture is not drawn in software. Most of the arcade board's tile planes are drawn cell by cell
  from a store of the tiles' 16-colour graphics in video memory, the PowerVR colouring them from its
  palette memory, so a change of colours costs nothing; the sky and ground, whose lines scroll one
  by one, are kept as textures and only the cells that change are redone. Sprites are PowerVR
  textures, built, cached and thrown out as the scenery goes by, 16-colour ones sharing palettes so
  one drawing serves every colour.
- The graphics and samples stay compressed on the disc and are read in small pieces while you play,
  in the background, so the race never waits for the disc.
- The arcade's sound board (its own processor and sample chip) is still run, but its samples are
  played by the Dreamcast's sound chip directly, not mixed in software.
- A frame's work is budgeted in microseconds: what cannot be prepared in time waits a frame, far
  scenery first, and the player's car and the road never do.
The emulators were the other half of the trouble. A build that ran perfectly in Flycast crashed,
dropped sprites, lost its music or showed warped textures on a real console, each for a reason the
emulator hides: the real processor traps on unaligned memory access, the real video chip is still
drawing a frame while the next is being prepared, it interpolates texture coordinates less exactly,
and the real disc and sound hardware are far slower to feed. Each of those was found from videos of
a console and fixed one at a time. If something looks right in an emulator and wrong on your
Dreamcast, that is a bug worth reporting.

Status
------
Version 0.5: brought up to the PC version 1.4.0 (Sebastian Toyos, cheviman): OPTIONS > SOUND's MUSIC,
EFFECTS and VOICE apart (the DJ can be turned down under the songs; here on the Dreamcast's own sound
chip), the radio's MUSIC button going round the songs and the station, REVERSE's start at AUSTRALIA
without the people over the water, the northern lights see-through, and the decompiled game's latest
code (its variables, state and code tables reorganised).
Version 0.4: a large rework of the drawing for speed. Most background planes are drawn straight from
a tile store with the PowerVR's palettes, the planes look only at what changed each frame, the sound
board's program runs about a third faster, and sprites find their textures and palettes far more
cheaply: in one player a race now leaves roughly half of each frame spare. The garbage at the road's
edges where a stage's colours change is almost gone. From the PC version: Jingle Bell on the radio,
REVERSE's and FULL's backgrounds and colours, REVERSE's finish, QUIT GAME stopping the race's sound,
the route map's land under the sea and landmarks, 2P MUSIC.
Known gaps: an occasional frame can still be dropped to keep the game and its music at full speed,
more often in 2 PLAYERS, which draws large scenery at half resolution; no NETWORK; WIDE (a PC
feature) is not offered.
