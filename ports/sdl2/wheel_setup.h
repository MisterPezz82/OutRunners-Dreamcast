/**
 *    Name:        wheel_setup.h
 *    Description: OPTIONS > STEERING's SET UP WHEEL: a racing wheel's axes and buttons, as the player shows
 *                 them, made a game controller layout for SDL, kept in gamecontrollerdb.txt
 *    Version:     0.1.0
 *    Created:     05-11-2026
 *    Modified:    05-11-2026
 *    Author:      Sebastian Toyos
 */
#ifndef ORUNNERS_PORT_WHEEL_SETUP_H
#define ORUNNERS_PORT_WHEEL_SETUP_H

#include <SDL.h>

/* The file of game controller layouts, in SDL's format (gamecontrollerdb.txt): where the layouts made go,
   beside the ones already there; SDL reads it as the game starts (SDL_HINT_GAMECONTROLLERCONFIG_FILE) */
void wheel_setup_open(const char *path);

/* SDL_JOYDEVICEADDED, SDL_JOYDEVICEREMOVED: every device kept open as a joystick, its axes and buttons
   watched while the page waits for one */
void wheel_setup_device_added(int device_index);
void wheel_setup_device_removed(SDL_JoystickID which);

/* Once a frame, after the events: the step the page waits for (console_wheel_setup_step), watched, and
   console_wheel_setup_took once it is taken. With the last, the device's layout made and added to SDL and to
   the file: its joystick's instance id (the port opens it as a game controller); -1 otherwise */
SDL_JoystickID wheel_setup_frame(void);

#endif
