/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * console_nonet.c: the console build (console/, the PC version's menus and games) without its network
 * game, in place of console/network.c: the Dreamcast has no NETWORK in its menu (ORUNNERS_NO_NETWORK
 * leaves the line out, console/menu.c), and what the rest of the console asks of the network answers
 * "none". The game's own link functions run as the arcade's.
 */
#include "console.h"
#include "console_internal.h"
#include "game.h"

void console_network_frame(void) {}
bool console_network_waiting(void) { return false; }
void console_network_host(void) {}
void console_network_search(void) {}
void console_network_join(int room) { (void)room; }
void console_network_leave(void) {}
int console_network_players(void) { return 1; }
void console_network_set_players(int players) { (void)players; }
bool console_network_in_room(void) { return false; }
bool console_network_joining(void) { return false; }
bool console_network_searching(void) { return false; }
bool console_network_hosting(void) { return false; }
bool console_network_linked(void) { return false; }
int console_network_room_count(void) { return 0; }
void console_network_room(int room, char *text, size_t size) { (void)room; if (size) text[0] = 0; }
bool console_network_room_full(int room) { (void)room; return true; }
int console_network_member_count(void) { return 0; }
void console_network_member(int index, char *text, size_t size) { (void)index; if (size) text[0] = 0; }
bool console_network_can_start(void) { return false; }
void console_network_start(void) {}
bool console_network_start_pending(void) { return false; }
int console_network_take_start(void) { return 0; }
const char *console_network_problem(void) { return NULL; }
void console_network_problem_seen(void) {}
void console_network_describe(char *text, size_t size) { if (size) text[0] = 0; }

void link_receive_records(void) { original_link_receive_records(); }
void link_send_records(void) { original_link_send_records(); }
