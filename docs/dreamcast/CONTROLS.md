# OutRunners Dreamcast prototype: controls (standard controller, port A)

| Dreamcast | Arcade |
|---|---|
| X | Coin |
| Start | Start |
| Analogue stick left/right (analogue; small centre dead zone) or D-pad left/right (full lock) | Steering wheel |
| Right trigger (analogue) | Accelerator |
| Left trigger (analogue) | Brake |
| A | Shift up (high gear) |
| B | Shift down (low gear) |
| Y | Music select button |
| D-pad up / down | Music previous / next |
| A + B + X + Y + Start together | Leave the game |

Test builds only: a control script at build/autoplay.txt is copied to the disc as
assets/autoplay.txt and then drives the controls instead of the pad (see PVR-RENDERER-DESIGN.md
"Audio stage 1"). Keep scripts in build/test-scripts/ and copy one there only for scripted tests.
