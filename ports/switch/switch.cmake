# Name:        switch.cmake
# Description: the Nintendo Switch build (devkitPro's toolchain): the console game alone, as the pieces of its
#              .nsp (the NSO, main.npdm, the NACP and the icon) and, where the assets pack is, as OutRunners.nro
# Version:     0.1.1
# Created:     30-10-2026
# Modified:    06-11-2026
# Author:      Sebastian Toyos
#
# Included by CMakeLists.txt when the toolchain is the Switch's (aarch64-none-elf-cmake), as SegaSonic's port
# does. The game is the same console build as on a computer, with SDL2 for the Switch (switch-sdl2, devkitPro's
# portlibs), its picture fixed and WIDE (ORUNNERS_HANDHELD, ORUNNERS_SCREEN_WIDE), its controls fixed, 2 PLAYERS
# side by side with a second controller (ORUNNERS_TWO_PLAYERS) and no EXIT in its menu (ORUNNERS_SYSTEM_EXIT:
# HOME leaves it). The Switch makes nothing from the ROM set: the game reads its assets pack from its own romfs,
# which a computer fills with the player's (the .nsp, made there with the player's keys; or, where the build has
# the pack beside the executables, build/bin/outrunners.assets, the .nro for the Homebrew Menu, the player's own
# and never shared).

target_compile_definitions(orunners_game_console PUBLIC ORUNNERS_HANDHELD ORUNNERS_SCREEN_WIDE ORUNNERS_TWO_PLAYERS
	ORUNNERS_SYSTEM_EXIT)

find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
	set(ORUNNERS_SWITCH_SDL2 SDL2::SDL2)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(ORUNNERS_SWITCH_SDL2_PKG REQUIRED IMPORTED_TARGET sdl2)
	set(ORUNNERS_SWITCH_SDL2 PkgConfig::ORUNNERS_SWITCH_SDL2_PKG)
endif()

add_executable(OutRunners ports/sdl2/main.c ports/sdl2/forces.c ports/sdl2/controls.c)
target_link_libraries(OutRunners PRIVATE orunners_game_console orunners_sound ${ORUNNERS_SWITCH_SDL2})
set_target_properties(OutRunners PROPERTIES SUFFIX ".elf")

# devkitPro's tools: the NACP (the title's name, author and version), the NSO, main.npdm and the .nro
set(ORUNNERS_SWITCH_TOOLS $ENV{DEVKITPRO}/tools/bin)
find_program(ORUNNERS_NACPTOOL nacptool HINTS ${ORUNNERS_SWITCH_TOOLS} REQUIRED)
find_program(ORUNNERS_ELF2NSO elf2nso HINTS ${ORUNNERS_SWITCH_TOOLS} REQUIRED)
find_program(ORUNNERS_NPDMTOOL npdmtool HINTS ${ORUNNERS_SWITCH_TOOLS} REQUIRED)
find_program(ORUNNERS_ELF2NRO elf2nro HINTS ${ORUNNERS_SWITCH_TOOLS} REQUIRED)

set(ORUNNERS_SWITCH_TITLE_ID 05005e6a4f520000)    # (homebrew's range; the .nsp's title; nacptool takes it with no 0x)
set(ORUNNERS_SWITCH_VERSION ${PROJECT_VERSION})
set(ORUNNERS_SWITCH_OUT ${ORUNNERS_BIN_DIR}/switch)
# the HOME menu's icon, 256 x 256: the player's own where this computer has one (local/icon.jpg: local/ holds
# the player's own pictures, never in the repository), else the project's (ports/switch/icon.jpg,
# tools/make_icon.py), else libnx's; the .nsp's maker takes the player's own instead
if(EXISTS ${CMAKE_SOURCE_DIR}/local/icon.jpg)
	set(ORUNNERS_SWITCH_ICON ${CMAKE_SOURCE_DIR}/local/icon.jpg)
elseif(EXISTS ${CMAKE_SOURCE_DIR}/ports/switch/icon.jpg)
	set(ORUNNERS_SWITCH_ICON ${CMAKE_SOURCE_DIR}/ports/switch/icon.jpg)
else()
	set(ORUNNERS_SWITCH_ICON ${NX_DEFAULT_ICON})
endif()

# the .nsp's pieces, which the computer's tool puts together with the player's assets and keys
add_custom_command(OUTPUT ${ORUNNERS_SWITCH_OUT}/main ${ORUNNERS_SWITCH_OUT}/main.npdm ${ORUNNERS_SWITCH_OUT}/control.nacp
		${ORUNNERS_SWITCH_OUT}/icon.jpg
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORUNNERS_SWITCH_OUT}
	COMMAND ${ORUNNERS_NACPTOOL} --create "OutRunners" "Sebastian Toyos" ${ORUNNERS_SWITCH_VERSION}
		${ORUNNERS_SWITCH_OUT}/control.nacp --titleid=${ORUNNERS_SWITCH_TITLE_ID}
	COMMAND ${ORUNNERS_ELF2NSO} $<TARGET_FILE:OutRunners> ${ORUNNERS_SWITCH_OUT}/main
	COMMAND ${ORUNNERS_NPDMTOOL} ${CMAKE_SOURCE_DIR}/ports/switch/outrunners.json ${ORUNNERS_SWITCH_OUT}/main.npdm
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORUNNERS_SWITCH_ICON} ${ORUNNERS_SWITCH_OUT}/icon.jpg
	DEPENDS OutRunners ${ORUNNERS_SWITCH_ICON} ${CMAKE_SOURCE_DIR}/ports/switch/outrunners.json
	COMMENT "The .nsp's pieces: main (NSO), main.npdm, control.nacp, icon.jpg"
	VERBATIM)
set(ORUNNERS_SWITCH_OUTPUTS ${ORUNNERS_SWITCH_OUT}/main ${ORUNNERS_SWITCH_OUT}/main.npdm)

# the .nro with the player's assets in its romfs, where this build has them
set(ORUNNERS_SWITCH_PACK ${ORUNNERS_BIN_DIR}/outrunners.assets)
if(EXISTS ${ORUNNERS_SWITCH_PACK})
	set(ORUNNERS_SWITCH_ROMFS ${CMAKE_BINARY_DIR}/switch_romfs)
	add_custom_command(OUTPUT ${ORUNNERS_SWITCH_OUT}/OutRunners.nro
		COMMAND ${CMAKE_COMMAND} -E make_directory ${ORUNNERS_SWITCH_ROMFS}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORUNNERS_SWITCH_PACK} ${ORUNNERS_SWITCH_ROMFS}
		COMMAND ${ORUNNERS_ELF2NRO} $<TARGET_FILE:OutRunners> ${ORUNNERS_SWITCH_OUT}/OutRunners.nro
			--icon=${ORUNNERS_SWITCH_ICON} --nacp=${ORUNNERS_SWITCH_OUT}/control.nacp --romfsdir=${ORUNNERS_SWITCH_ROMFS}
		DEPENDS OutRunners ${ORUNNERS_SWITCH_PACK} ${ORUNNERS_SWITCH_OUT}/control.nacp ${ORUNNERS_SWITCH_ICON}
		COMMENT "OutRunners.nro with the assets pack in its romfs (the player's own: never shared)"
		VERBATIM)
	list(APPEND ORUNNERS_SWITCH_OUTPUTS ${ORUNNERS_SWITCH_OUT}/OutRunners.nro)
else()
	message(STATUS "No assets pack at ${ORUNNERS_SWITCH_PACK}: no OutRunners.nro, the .nsp's pieces alone")
endif()
add_custom_target(OutRunners_switch ALL DEPENDS ${ORUNNERS_SWITCH_OUTPUTS})
