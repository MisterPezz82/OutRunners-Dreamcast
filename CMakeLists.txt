# Name:        CMakeLists.txt
# Description: build of the OutRunners runtime and its development tools
# Version:     0.1.1
# Created:     11-10-2026
# Modified:    04-11-2026
# Author:      Sebastian Toyos
cmake_minimum_required(VERSION 3.20)
project(outrunners VERSION 1.1.2 LANGUAGES C CXX)    # (the release's: OPTIONS shows it, the devices' packages carry it)

# A generator of one configuration (Makefiles, Ninja: Linux's and macOS's by default) given no build type
# builds the game optimized, as the packages are: without, it is slower (and issue 17's race showed)
get_property(ORUNNERS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ORUNNERS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "The build type: Release unless given" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Every executable lands in build/bin (ORUNNERS_BIN_DIR), whatever the generator or configuration
set(ORUNNERS_BIN_DIR ${CMAKE_SOURCE_DIR}/build/bin CACHE PATH "Where the executables go")
foreach(config IN ITEMS "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${config} ${ORUNNERS_BIN_DIR})
endforeach()

# The packages a player downloads (.github/workflows/build.yml) carry SDL2 inside the window port, built
# from its own source
option(ORUNNERS_STATIC_SDL2 "Build SDL2 from its source into the executables" OFF)

# A development aid: everything built with AddressSanitizer, which stops the game at a read past a piece of
# its assets (each piece is a block of its own, runtime/src/assets.c: the code reads a table only inside it)
option(ORUNNERS_ASAN "Build with AddressSanitizer" OFF)
if(ORUNNERS_ASAN)
	if(MSVC)
		add_compile_options(/fsanitize=address /Zi)
		add_link_options(/DEBUG /INCREMENTAL:NO)
	else()
		add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
		add_link_options(-fsanitize=address)
	endif()
endif()

if(MSVC)
	add_compile_options(/W3 /utf-8)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	# with ORUNNERS_STATIC_SDL2 the C runtime goes inside the executables too: they need no Visual C++
	# redistributable on the player's machine
	if(ORUNNERS_STATIC_SDL2)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)    # (SDL2's targets too)
	endif()
endif()

# Multi 32 board model (script.c drives the controls in test runs; video.c draws the two screens; sound.c
# is the Z80's side of the board and sound_chips.c its YM3438 and MultiPCM)
add_library(s32runtime STATIC
	runtime/src/machine.c
	runtime/src/io.c
	runtime/src/script.c
	runtime/src/sound.c
	runtime/src/ym3438.c
	runtime/src/memory_census.c
	runtime/src/tasks.c
	runtime/src/video.c
	runtime/src/workers.c
	runtime/src/multipcm.c
	runtime/src/sound_chips.c
)
target_include_directories(s32runtime PUBLIC runtime/include)
if(UNIX AND NOT APPLE)
	target_link_libraries(s32runtime PUBLIC m)    # (the C library's maths are apart there)
endif()
# (the video's workers, runtime/src/workers.c: POSIX threads but on Windows and the Switch, libnx's there)
set(ORUNNERS_SWITCH OFF)
if(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
	set(ORUNNERS_SWITCH ON)
endif()
if(UNIX AND NOT ORUNNERS_SWITCH)
	find_package(Threads REQUIRED)
	target_link_libraries(s32runtime PUBLIC Threads::Threads)
endif()

# The game's data: no executable carries the ROM or anything from it. The game loads its assets when it
# starts (runtime/src/assets.c): the files extract_assets.py takes out of the player's ROM set (rom/, its
# files or its .zip; ORUNNERS_ROM picks another) into assets/, in formats the usual tools open, where it
# looks for them (beside the executable, or the current folder's); or, where it finds none, the ones its
# first run takes out of the ROM set itself. The reference and the scaffold run the original code, so they
# read the ROM set themselves when they start (runtime/src/rom_set.c). The ROM never goes into the
# repository. Without a ROM set (a build for another machine) nothing of the game's is made, only the
# executables
find_package(Python3 COMPONENTS Interpreter)
set(ORUNNERS_ROM ${CMAKE_SOURCE_DIR}/rom CACHE PATH "The ROM set the assets are taken out of")
set(ORUNNERS_ROM_FILES "")
if(IS_DIRECTORY ${ORUNNERS_ROM})
	file(GLOB ORUNNERS_ROM_FILES ${ORUNNERS_ROM}/*)
elseif(EXISTS ${ORUNNERS_ROM})
	set(ORUNNERS_ROM_FILES ${ORUNNERS_ROM})
endif()
add_library(orunners_data STATIC runtime/src/game_data.c runtime/src/rom_set.c runtime/src/inflate.c)
target_include_directories(orunners_data PUBLIC runtime/include)
# The game's first run, in the window ports: the assets out of the player's ROM set, in C (extract.c), into
# files that load as extract_assets.py's do; the pieces of the programs' data as extract_layout.c lists them,
# in positions and names only (tools/extract_layout.py writes it from the layouts extract_assets.py writes)
add_library(orunners_extract STATIC runtime/src/extract.c runtime/src/extract_layout.c)
target_link_libraries(orunners_extract PUBLIC orunners_data)
if(ORUNNERS_ROM_FILES AND Python3_FOUND)
	set(ORUNNERS_ASSETS_STAMP ${CMAKE_SOURCE_DIR}/assets/.extracted)
	add_custom_command(OUTPUT ${ORUNNERS_ASSETS_STAMP}
		COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/extract_assets.py ${ORUNNERS_ROM}
		DEPENDS ${ORUNNERS_ROM_FILES} ${CMAKE_SOURCE_DIR}/assets.json ${CMAKE_SOURCE_DIR}/extract_assets.py
		COMMENT "Taking the assets out of the ROM set")
	add_custom_target(orunners_assets ALL DEPENDS ${ORUNNERS_ASSETS_STAMP})
else()
	message(STATUS "No ROM set at ${ORUNNERS_ROM} (or no Python): no assets; the game needs them beside it")
endif()

# The game: its runtime plus its C, src/ (tools/decompile.py writes it; ORUNNERS_DECOMPILED_DIR picks
# another folder). Its own video and sound come back into it when their libraries draw the two screens and
# play the MultiPCM
set(ORUNNERS_DECOMPILED_DIR ${CMAKE_SOURCE_DIR}/src CACHE PATH "The game's C (tools/decompile.py)")
file(GLOB_RECURSE ORUNNERS_DECOMPILED CONFIGURE_DEPENDS ${ORUNNERS_DECOMPILED_DIR}/*.c)
list(FILTER ORUNNERS_DECOMPILED EXCLUDE REGEX "/sound/[^/]*\\.c$")    # (the sound program's: a library of its own)

# The sound program: the Z80's C (src/sound/, tools/decompile_sound.py) on the board's time
# (runtime/src/sound_cpu.c). The decompiled game and the scaffold both play it
file(GLOB ORUNNERS_SOUND_DECOMPILED CONFIGURE_DEPENDS ${ORUNNERS_DECOMPILED_DIR}/sound/*.c)
if(ORUNNERS_SOUND_DECOMPILED)
	add_library(orunners_sound STATIC runtime/src/sound_cpu.c ${ORUNNERS_SOUND_DECOMPILED})
	target_include_directories(orunners_sound PUBLIC runtime/include PRIVATE ${ORUNNERS_DECOMPILED_DIR}/sound)
	target_link_libraries(orunners_sound PUBLIC s32runtime)
	if(MSVC)
		target_compile_options(orunners_sound PRIVATE /bigobj)
	else()
		target_compile_options(orunners_sound PRIVATE -fno-strict-aliasing)
	endif()
else()
	message(STATUS "No sound program C in ${ORUNNERS_DECOMPILED_DIR}/sound")
endif()

if(ORUNNERS_DECOMPILED AND ORUNNERS_SOUND_DECOMPILED)
	add_library(orunners_game STATIC runtime/src/game.c runtime/src/assets.c runtime/src/assets_media.c
		runtime/src/assets_midi.c runtime/src/assets_pack.c runtime/src/json.c runtime/src/png.c runtime/src/inflate.c
		${ORUNNERS_DECOMPILED})
	target_include_directories(orunners_game PUBLIC runtime/include ${ORUNNERS_DECOMPILED_DIR})
	target_link_libraries(orunners_game PUBLIC s32runtime)
	if(MSVC)
		target_compile_options(orunners_game PRIVATE /bigobj)
	else()
		# variables are typed lvalues over the work RAM bytes (game/support.h)
		target_compile_options(orunners_game PRIVATE -fno-strict-aliasing)
	endif()

	# a development aid: every mem_read and mem_write of the game's C counted by its line
	# (orunners_game_headless --memory-census FILE; runtime/src/memory_census.c)
	option(ORUNNERS_MEMORY_CENSUS "Count the game's memory accesses by line of C" OFF)
	if(ORUNNERS_MEMORY_CENSUS)
		target_compile_definitions(orunners_game PUBLIC S32_MEMORY_CENSUS)
	endif()

	# (GCC, the Switch's and Linux's: it takes the sprite list's second copy, 6144 words past an entry, for a
	# write past the sprite RAM's end, from an entry at any address its mask leaves (game/support.h, board());
	# the lists' entries are all in its first part)
	if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
		set(ORUNNERS_GAME_GCC_OPTIONS -Wno-stringop-overflow)
		target_compile_options(orunners_game PRIVATE ${ORUNNERS_GAME_GCC_OPTIONS})
	endif()

	add_executable(orunners_game_headless ports/headless/main.c)
	target_link_libraries(orunners_game_headless PRIVATE orunners_game orunners_sound)

	# The console build: the same game with the console's own versions of the functions console/replaced.txt
	# lists (console/*.c, ORUNNERS_CONSOLE), in OutRunners and a headless build for scripted checks
	file(GLOB ORUNNERS_CONSOLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/console/*.c)
	add_library(orunners_game_console STATIC runtime/src/game.c runtime/src/assets.c runtime/src/assets_media.c
		runtime/src/assets_midi.c runtime/src/assets_pack.c runtime/src/json.c runtime/src/png.c runtime/src/inflate.c
		${ORUNNERS_DECOMPILED} ${ORUNNERS_CONSOLE_SOURCES})
	target_include_directories(orunners_game_console PUBLIC runtime/include ${ORUNNERS_DECOMPILED_DIR} console)
	target_compile_definitions(orunners_game_console PUBLIC ORUNNERS_CONSOLE ORUNNERS_VERSION="${PROJECT_VERSION}")
	target_link_libraries(orunners_game_console PUBLIC s32runtime)
	if(WIN32)
		target_link_libraries(orunners_game_console PUBLIC ws2_32)    # (the network's sockets: console/net_udp.c)
	endif()
	if(MSVC)
		target_compile_options(orunners_game_console PRIVATE /bigobj)
	else()
		target_compile_options(orunners_game_console PRIVATE -fno-strict-aliasing ${ORUNNERS_GAME_GCC_OPTIONS})
	endif()
	add_executable(orunners_console_headless ports/headless/main.c)
	target_link_libraries(orunners_console_headless PRIVATE orunners_game_console orunners_sound)

	# The Switch's .nsp, made on the player's computer (ports/switch/nsp): the game's Switch build (main,
	# main.npdm, control.nacp, icon.jpg), the assets pack of the player's ROM set (the first run's steps, the
	# console game's loader) and the keys of the player's console. A command line; with SDL2 also a window
	# (below, with the game's window)
	if(NOT ORUNNERS_SWITCH AND NOT ORUNNERS_ANBERNIC AND NOT ANDROID)
		add_executable(orunners_nsp ports/switch/nsp/main.c ports/switch/nsp/make_nsp.c ports/switch/nsp/formats.c
			ports/switch/nsp/crypto.c ports/switch/nsp/rom_pack.c)
		target_link_libraries(orunners_nsp PRIVATE orunners_game_console orunners_sound orunners_extract)
		set_target_properties(orunners_nsp PROPERTIES OUTPUT_NAME "OutRunners NSP")

		# Anbernic's PORTS, made on the player's computer (ports/anbernic/maker): the game's Anbernic build
		# (build/bin/anbernic) and the assets pack of the player's ROM set, as OutRunners NSP makes it
		# (rom_pack.c). A command line; with SDL2 also a window (below)
		add_executable(orunners_anbernic_maker ports/anbernic/maker/main.c ports/anbernic/maker/make_ports.c
			ports/switch/nsp/rom_pack.c)
		target_include_directories(orunners_anbernic_maker PRIVATE ports/switch/nsp)
		target_link_libraries(orunners_anbernic_maker PRIVATE orunners_game_console orunners_sound orunners_extract)
		set_target_properties(orunners_anbernic_maker PROPERTIES OUTPUT_NAME "OutRunners Anbernic")

		# Android's APK, made on the player's computer (ports/android/apk): the game's Android build
		# (android/outrunners-base.apk) with the assets pack of the player's ROM set (rom_pack.c), signed with
		# the player's own key; its SHA-256 is OutRunners NSP's (crypto.c). A command line; with SDL2 also a
		# window (below)
		add_executable(orunners_apk_maker ports/android/apk/main.c ports/android/apk/make_apk.c ports/android/apk/zip.c
			ports/android/apk/sign.c ports/android/apk/key.c ports/android/apk/ecdsa.c ports/switch/nsp/crypto.c
			ports/switch/nsp/rom_pack.c)
		target_include_directories(orunners_apk_maker PRIVATE ports/switch/nsp)
		target_link_libraries(orunners_apk_maker PRIVATE orunners_game_console orunners_sound orunners_extract)
		if(WIN32)
			target_link_libraries(orunners_apk_maker PRIVATE bcrypt)    # (BCryptGenRandom: the key)
		endif()
		set_target_properties(orunners_apk_maker PROPERTIES OUTPUT_NAME "OutRunners APK")
	endif()

	# The devices' builds, each with its toolchain: the Nintendo Switch (devkitPro's: its .nro and its .nsp's
	# pieces, ports/switch), Android (the NDK's: libmain.so in its APKs, ports/android) and Anbernic's H700
	# handhelds (ports/anbernic)
	option(ORUNNERS_ANBERNIC "Build the game for Anbernic's H700 handhelds" OFF)
	if(ORUNNERS_SWITCH)
		include(${CMAKE_SOURCE_DIR}/ports/switch/switch.cmake)
	elseif(ANDROID)
		include(${CMAKE_SOURCE_DIR}/ports/android/android.cmake)
	elseif(ORUNNERS_ANBERNIC)
		include(${CMAKE_SOURCE_DIR}/ports/anbernic/anbernic.cmake)
	endif()

	# The game in a window (ports/sdl2): SDL2 from the system, or on Windows the development package
	# unpacked in build/downloads/sdl2 (SDL2_DIR picks another), a library beside the executable; or, with
	# ORUNNERS_STATIC_SDL2, SDL2 built from its own source into the executable
	if(ORUNNERS_ANBERNIC OR ORUNNERS_SWITCH OR ANDROID)
		set(SDL2_FOUND FALSE)    # (the device's own build above: no computer's window ports)
	elseif(ORUNNERS_STATIC_SDL2)
		include(FetchContent)
		FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.32.10
			GIT_SHALLOW TRUE)
		set(SDL_SHARED OFF CACHE BOOL "" FORCE)
		set(SDL_STATIC ON CACHE BOOL "" FORCE)
		set(SDL_TEST OFF CACHE BOOL "" FORCE)
		set(SDL_FORCE_STATIC_VCRT ON CACHE BOOL "" FORCE)    # (MSVC: the C runtime inside, as the game's)
		FetchContent_MakeAvailable(SDL2)
		set(ORUNNERS_SDL2 SDL2::SDL2-static)
		set(SDL2_FOUND TRUE)
	else()
		set(ORUNNERS_SDL2_UNPACKED "")
		if(WIN32)    # (the VC package: its libraries are Windows' only, a source tree shared with WSL has it)
			file(GLOB ORUNNERS_SDL2_UNPACKED ${CMAKE_SOURCE_DIR}/build/downloads/sdl2/SDL2-*/cmake)
		endif()
		find_package(SDL2 CONFIG QUIET HINTS ${ORUNNERS_SDL2_UNPACKED})
		set(ORUNNERS_SDL2 SDL2::SDL2)
	endif()
	if(TARGET SDL2::SDL2main)
		list(APPEND ORUNNERS_SDL2 SDL2::SDL2main)
	endif()
	if(SDL2_FOUND)
		# (an .app on macOS: SDL_GetBasePath is its Resources folder): the arcade game, "OutRunners Arcade" its
		# executable, .app and window, and the console one beside it, OutRunners. Both make their assets on
		# their first run from the player's ROM set (ports/sdl2/first_run.c), its window written with Kenney
		# Mini (CC0), a picture beside them, in the .app's Resources on macOS
		add_executable(orunners WIN32 MACOSX_BUNDLE ports/sdl2/main.c ports/sdl2/first_run.c ports/sdl2/letters.c
			ports/sdl2/font/font.bmp)
		target_link_libraries(orunners PRIVATE orunners_game orunners_sound orunners_extract ${ORUNNERS_SDL2})
		set_target_properties(orunners PROPERTIES OUTPUT_NAME "OutRunners Arcade"
			MACOSX_BUNDLE_BUNDLE_NAME "OutRunners Arcade" MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cheviman.outrunners-arcade)
		# (OutRunners draws its CONTROLS page's keys and buttons with Kenney's Input Prompts Pixel, a picture
		# beside it, in the .app's Resources on macOS)
		add_executable(outrunners WIN32 MACOSX_BUNDLE ports/sdl2/main.c ports/sdl2/controls.c ports/sdl2/first_run.c
			ports/sdl2/letters.c ports/sdl2/prompts/input_prompts.bmp ports/sdl2/font/font.bmp)
		target_link_libraries(outrunners PRIVATE orunners_game_console orunners_sound orunners_extract ${ORUNNERS_SDL2})
		# a development aid: OutRunners with Android's touch controls, the mouse a finger, to look at them in a
		# window of any shape
		option(ORUNNERS_TOUCH_TEST "Build OutRunners with Android's touch controls, the mouse as a finger" OFF)
		if(ORUNNERS_TOUCH_TEST)
			target_sources(outrunners PRIVATE ports/sdl2/touch.c)
			target_compile_definitions(outrunners PRIVATE ORUNNERS_TOUCH ORUNNERS_TOUCH_TEST)
		endif()
		set_target_properties(outrunners PROPERTIES OUTPUT_NAME "OutRunners"
			MACOSX_BUNDLE_BUNDLE_NAME "OutRunners" MACOSX_BUNDLE_GUI_IDENTIFIER io.github.cheviman.outrunners)
		set_source_files_properties(ports/sdl2/prompts/input_prompts.bmp ports/sdl2/font/font.bmp PROPERTIES
			MACOSX_PACKAGE_LOCATION Resources)
		if(NOT APPLE)
			add_custom_command(TARGET outrunners POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ports/sdl2/prompts/input_prompts.bmp
					$<TARGET_FILE_DIR:outrunners>
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ports/sdl2/font/font.bmp
					$<TARGET_FILE_DIR:outrunners>)
			add_custom_command(TARGET orunners POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ports/sdl2/font/font.bmp
					$<TARGET_FILE_DIR:orunners>)
		endif()
		if(WIN32 AND NOT ORUNNERS_STATIC_SDL2)
			add_custom_command(TARGET orunners POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:orunners>)
		endif()

		# the executables' icon (tools/make_icon.py): a resource on Windows (which SDL gives the window too),
		# the .app's on macOS, a picture beside them on Linux (for a launcher of the desktop's). On Windows a
		# picture of the player's own in its place where this computer has one (ORUNNERS_PC_ICON, else
		# local/icon_pc.png: local/ holds the player's own pictures, never committed), made an .ico here
		set(ORUNNERS_PC_ICON "" CACHE FILEPATH "The executables' icon on Windows: a picture of the player's own")
		set(ORUNNERS_PC_PICTURE ${ORUNNERS_PC_ICON})
		if(NOT ORUNNERS_PC_PICTURE AND EXISTS ${CMAKE_SOURCE_DIR}/local/icon_pc.png)
			set(ORUNNERS_PC_PICTURE ${CMAKE_SOURCE_DIR}/local/icon_pc.png)
		endif()
		set(ORUNNERS_ICON_RC ${CMAKE_SOURCE_DIR}/ports/sdl2/icon/outrunners.rc)
		if(WIN32 AND ORUNNERS_PC_PICTURE AND Python3_FOUND)
			# (made as CMake configures, once for both: again when the picture changes)
			set(ORUNNERS_OWN_ICON ${CMAKE_BINARY_DIR}/icon)
			execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/make_icon.py --ico
				${ORUNNERS_PC_PICTURE} ${ORUNNERS_OWN_ICON}/outrunners.ico RESULT_VARIABLE ORUNNERS_OWN_ICON_MADE)
			set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ORUNNERS_PC_PICTURE})
			if(ORUNNERS_OWN_ICON_MADE EQUAL 0)
				file(WRITE ${ORUNNERS_OWN_ICON}/outrunners.rc "// the player's own picture (CMakeLists.txt)\n1 ICON \"outrunners.ico\"\n")
				set(ORUNNERS_ICON_RC ${ORUNNERS_OWN_ICON}/outrunners.rc)
				message(STATUS "The executables' icon: ${ORUNNERS_PC_PICTURE}")
			endif()
		endif()
		foreach(game orunners outrunners)
			if(WIN32)
				target_sources(${game} PRIVATE ${ORUNNERS_ICON_RC})
			elseif(APPLE)
				target_sources(${game} PRIVATE ports/sdl2/icon/outrunners.icns)
				set_target_properties(${game} PROPERTIES MACOSX_BUNDLE_ICON_FILE outrunners.icns)
			else()
				add_custom_command(TARGET ${game} POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ports/sdl2/icon/outrunners.png
						$<TARGET_FILE_DIR:${game}>)
			endif()
		endforeach()
		if(APPLE)
			set_source_files_properties(ports/sdl2/icon/outrunners.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
		endif()

		# OutRunners NSP's window (ports/switch/nsp/window.c), when it is started with nothing: still a console
		# program, with its own main (SDL2 without SDL2main); the folder dialog is COM's on Windows. Its
		# letters (Kenney Mini, font.bmp) beside it, as SDL2's library is
		list(GET ORUNNERS_SDL2 0 ORUNNERS_SDL2_LIBRARY)
		target_sources(orunners_nsp PRIVATE ports/switch/nsp/window.c ports/switch/nsp/folder_dialog.c
			ports/sdl2/letters.c)
		target_include_directories(orunners_nsp PRIVATE ports/sdl2)
		target_compile_definitions(orunners_nsp PRIVATE ORUNNERS_NSP_WINDOW)
		target_link_libraries(orunners_nsp PRIVATE ${ORUNNERS_SDL2_LIBRARY})
		if(WIN32)
			target_link_libraries(orunners_nsp PRIVATE ole32 shell32 uuid)
		endif()
		if(NOT APPLE)
			add_custom_command(TARGET orunners_nsp POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ports/sdl2/font/font.bmp
					$<TARGET_FILE_DIR:orunners_nsp>)
		endif()

		# OutRunners Anbernic's window (ports/anbernic/maker/window.c), as OutRunners NSP's: its folder dialog
		# (with a title of its own) and its letters, font.bmp beside it
		target_sources(orunners_anbernic_maker PRIVATE ports/anbernic/maker/window.c ports/switch/nsp/folder_dialog.c
			ports/sdl2/letters.c)
		target_include_directories(orunners_anbernic_maker PRIVATE ports/sdl2)
		target_compile_definitions(orunners_anbernic_maker PRIVATE ORUNNERS_ANBERNIC_WINDOW
			[[FOLDER_TITLE="The folder for PORTS (the Roms of your card, or any)"]])
		target_link_libraries(orunners_anbernic_maker PRIVATE ${ORUNNERS_SDL2_LIBRARY})
		if(WIN32)
			target_link_libraries(orunners_anbernic_maker PRIVATE ole32 shell32 uuid)
		endif()

		# OutRunners APK's window (ports/android/apk/window.c), as OutRunners NSP's: its folder dialog (with a
		# title of its own) and its letters, font.bmp beside it
		target_sources(orunners_apk_maker PRIVATE ports/android/apk/window.c ports/switch/nsp/folder_dialog.c
			ports/sdl2/letters.c)
		target_include_directories(orunners_apk_maker PRIVATE ports/sdl2)
		target_compile_definitions(orunners_apk_maker PRIVATE ORUNNERS_APK_WINDOW
			[[FOLDER_TITLE="The folder for OutRunners.apk"]])
		target_link_libraries(orunners_apk_maker PRIVATE ${ORUNNERS_SDL2_LIBRARY})
		if(WIN32)
			target_link_libraries(orunners_apk_maker PRIVATE ole32 shell32 uuid)
		endif()
	else()
		message(STATUS "No SDL2: the window ports (OutRunners, OutRunners Arcade) are not built")
	endif()
else()
	message(STATUS "No game C in ${ORUNNERS_DECOMPILED_DIR}")
endif()

# The translation scaffold: the V60 runtime plus the C tools/recompile.py writes from the user's ROM into
# build/generated, instruction by instruction (to run the program natively before the decompiler exists)
option(ORUNNERS_SCAFFOLD "Build the V70 translation scaffold (orunners_headless)" OFF)
file(GLOB ORUNNERS_GENERATED CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/build/generated/*.c)
if(ORUNNERS_SCAFFOLD AND ORUNNERS_GENERATED AND ORUNNERS_SOUND_DECOMPILED)
	add_library(orunners_program STATIC runtime/src/v60.c ${ORUNNERS_GENERATED})
	target_include_directories(orunners_program PUBLIC runtime/include build/generated)
	target_link_libraries(orunners_program PUBLIC s32runtime)
	if(MSVC)
		target_compile_options(orunners_program PRIVATE /bigobj)
	endif()

	add_executable(orunners_headless ports/headless/main.c)
	target_compile_definitions(orunners_headless PRIVATE ORUNNERS_SCAFFOLD)
	target_link_libraries(orunners_headless PRIVATE orunners_program orunners_sound orunners_data)
elseif(ORUNNERS_SCAFFOLD)
	message(STATUS "No generated program in build/generated: no scaffold")
endif()

# Development tools built on MAME's code; never linked into the game
option(ORUNNERS_REFERENCE_TOOLS "Build the MAME-based reference tools" ON)
if(ORUNNERS_REFERENCE_TOOLS)
	# the reference on MAME's cores (arcade-decomp's system32/reference: s32ref, the disassemblers' oracles,
	# pcmcheck), with OutRunners' data library
	set(S32_GAME_DATA orunners_data)
	include(${CMAKE_SOURCE_DIR}/tools/core/system32/reference/reference.cmake)

	# the acceptance check of the first run (ports/headless/assets_check.c): two folders of assets compared
	# as the game loads them, and the assets out of a ROM set as the first run takes them. A development tool
	# too, so the players' packages (built without these) do not carry it
	if(TARGET orunners_game AND TARGET orunners_sound)
		add_executable(orunners_assets_check ports/headless/assets_check.c)
		target_link_libraries(orunners_assets_check PRIVATE orunners_game orunners_sound orunners_extract)
	endif()
endif()
