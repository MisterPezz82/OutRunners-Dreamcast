OutRunners for Dreamcast - version 0.2
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
    & KNUCKLES            CLASSIC (the arcade's game with a little more time), TIME ATTACK (no time
                          limit; no ghost car in this version), FULL (all the stages in one race),
                          REVERSE (from a goal back to the start)
    OPTIONS               DIFFICULTY, TIME, VOLUME, SOUND TEST, STEERING (the stick's dead zone and
                          sensitivity, the D-pad's steering speed, vibration, an input test), FPS
In a race, Start pauses: CONTINUE, RESTART or QUIT GAME.

Controls (Dreamcast controller)
-------------------------------
    Analogue stick        steer; moves through the menus
    D-pad left / right    steer (the wheel turns while held); moves through the menus
    R trigger             accelerate (analogue)
    L trigger             brake (analogue)
    A / B                 gear shift; in the menus A picks a line and B goes back
    Start                 start; picks a line in the menus; pauses a race
    Y                     music select
    D-pad up / down       previous / next music
A vibration pack shakes when the cabinet's wheel motor would (a crash, off the road).

Saving
------
Course records, the game's settings, the OPTIONS menu's settings and the bookkeeping are saved to the first VMU found, in a file
called ORUNNERS (about 2 blocks), and loaded when the game starts. Records and settings are saved within
ten seconds of changing; the rest at most every five minutes.

Sound
-----
The arcade board serves two cabinets and sends each seat's sound to one speaker. Here both sides
go to both speakers, so the game's music and effects are centred.

Getting it onto a Dreamcast
---------------------------
The PC port runs the arcade game's logic and draws every frame in software, which a modern computer
does without effort. A Dreamcast has a 200 MHz processor, 16 MB of memory and 8 MB of video memory,
and the game's graphics and sound alone are several times that. Most of this port is the work of
fitting it in and keeping it at 60 frames a second:
- The picture is not drawn in software. The arcade board's tile planes and sprites are turned into
  PowerVR textures and quads each frame, with sprite textures built, cached and thrown out as the
  scenery goes by, and 16-colour textures sharing palettes so one drawing serves every colour.
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
Version 0.2: the menus above are new and have been checked in the Flycast emulator; on a console
they, the pause and the vibration are not yet tested. Version 0.1 was tested on a Dreamcast with
GDEMU. Known gaps: in the busiest stretches the picture can drop a frame to keep the game and its
music at full speed; distant scenery can appear a moment late, and large distant scenery can flicker
on some stages (the pyramids, the snow); FM sound and sound LFOs are not reproduced; one player only
(no 2 PLAYERS, no NETWORK); TIME ATTACK has no ghost car.
