OutRunners for Dreamcast - version 0.1
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

Controls (Dreamcast controller)
-------------------------------
    Analogue stick        steer
    R trigger             accelerate (analogue)
    L trigger             brake (analogue)
    A / B                 gear shift
    X                     insert coin
    Start                 start
    Y                     music select
    D-pad up / down       previous / next music
    D-pad left / right    full lock

Saving
------
Course records, the game's settings and its bookkeeping are saved to the first VMU found, in a file
called ORUNNERS (about 2 blocks), and loaded when the game starts. Records and settings are saved within
ten seconds of changing; the rest at most every five minutes.

Sound
-----
The arcade board serves two cabinets and sends each seat's sound to one speaker. Here both sides
go to both speakers, so the game's music and effects are centred.

Status
------
Version 0.1. Tested on a Dreamcast with GDEMU and in the Flycast emulator. Known gaps: in the
busiest stretches the picture can drop a frame to keep the game and its music at full speed;
distant scenery can appear a moment late; FM sound and sound LFOs are not reproduced; one player
only (no link play).
