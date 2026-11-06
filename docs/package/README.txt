OutRunners
==========

Sega's OutRunners (1992, System Multi 32) as a native game, decompiled to C and checked frame by frame
against MAME: github.com/cheviman/outrunners-port. Nothing of the game comes in this package: it takes
the game's assets out of your own ROM set.

What you need
-------------
orunners.zip, MAME's ROM set of OutRunners (World). Keep it beside the game, or drop it on the game's
window when it asks for it.

The games
---------
OutRunners           the console version: one screen, a menu (ARCADE, 2 PLAYERS, NETWORK, & KNUCKLES with
                     CLASSIC, TIME ATTACK, FULL and REVERSE, OPTIONS), WIDE 16:9 and 20:9, the pause, game
                     controllers and keys set in OPTIONS > CONTROLS
OutRunners Arcade    the arcade game as it is, both cabinet screens side by side

The first time either starts it takes the assets out of orunners.zip (a few seconds) and keeps them:
beside the game on Windows, in your user folder for the game elsewhere. OutRunners also writes
outrunners.assets there, the assets in one file, which the devices' tools below can take instead.

Racing wheels
-------------
A wheel the game takes as a game controller (a Logitech G29, for one) works as it is: the wheel is the
left stick and the pedals the triggers; set OPTIONS > STEERING > WHEEL ROTATION to its own turn (900 on a
G29) and the DEAD ZONE to 0%. A wheel the game does not see (a Thrustmaster T300RS on Windows): OPTIONS >
STEERING > SET UP WHEEL asks for its wheel, pedals and buttons one by one, and keeps them in
gamecontrollerdb.txt in the same folder, where SDL's controller layouts made elsewhere can go too.
STEERING's INPUT TEST shows the wheel and the pedals as the game sees them.

NETWORK
-------
Up to four devices on the same local network (a house's Wi-Fi, a phone's hotspot) race together:
computers, phones and the Switch alike. NETWORK > NAME is your name; one device picks HOST and waits for
the others, which pick JOIN and the room; the host's START races them all. A device that stops being
heard is out of the race after 3 seconds, and the race goes on. The game uses UDP (the rooms on port
27720): a firewall that asks about the game should let it reach the private network.

The devices
-----------
Each device's game comes in this package without assets; a tool beside it makes yours, on this
computer, with your ROM set. What it makes is yours alone: never share it.

Nintendo Switch       OutRunners NSP (the folder switch beside it): OutRunners.nsp, with your console's
                      keys (prod.keys). Install it with an installer on a console with Atmosphere.
Android               OutRunners APK (the folder android beside it): OutRunners.apk, signed with a key of
                      yours kept in your user folder (keep it: only an APK with the same key installs over
                      the last one). Copy it to the phone and open it there.
Anbernic H700         OutRunners Anbernic (the folder anbernic beside it): the folder PORTS, beside the
handhelds             tool or straight into the Roms folder of your handheld's card (choose it as the
                      output folder): the other ports there stay as they are.

Start a tool with a double click and drop your files on its window (orunners.zip, prod.keys, a square
picture of yours for the icon), or run it from a command line: an option it does not know shows the ones
it takes.

Licenses
--------
LICENSE.txt is the project's (BSD-3-Clause). licenses/ holds MAME's and ymfm's (the board's video and
sound follow their behavior and tables), SDL2's (zlib) and Kenney's (CC0: the windows' letters, the
controls' pictures).

OutRunners is a trademark of SEGA. This project is not affiliated with or endorsed by SEGA, Nintendo,
Google or Anbernic.
