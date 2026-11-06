# Name:        android.cmake
# Description: the Android build (the NDK's toolchain): the console game as libmain.so beside SDL2's, both from
#              their source, and the APKs around them: the game's, for OutRunners APK, and this computer's own
# Version:     0.1.1
# Created:     30-10-2026
# Modified:    06-11-2026
# Author:      Sebastian Toyos
#
# Included by CMakeLists.txt when the toolchain is the NDK's (build/cmake/android.toolchain.cmake, with
# ANDROID_ABI and ANDROID_PLATFORM), as SegaSonic's port does. The game is the console build with its picture
# fixed and WIDE (ORUNNERS_HANDHELD, ORUNNERS_SCREEN_WIDE), no EXIT in its menu (ORUNNERS_SYSTEM_EXIT: Android's
# home leaves it) and the touch screen's controls (ORUNNERS_TOUCH), in libmain.so, which SDL's activity starts
# (its Java, from SDL's source). The APKs are put together without Gradle, by the SDK's own tools: javac and
# d8 (the Java), aapt2 (the manifest, the icon, the assets), aapt (the libraries and the Java in), zipalign
# and apksigner. Android makes nothing from the ROM set: an APK carries the assets pack taken out of the
# player's ROM set on a computer, so it is its player's alone and never shared. Two of them:
#   build/bin/android/outrunners-base.apk   the game without a pack or a signature: OutRunners APK
#                                           (ports/android/apk) makes the player's APK of it, on their computer
#   build/bin/android/OutRunners-debug.apk   this computer's own, with its pack (build/bin/outrunners.assets),
#                                           signed with the debug key of its Android tools
#                                           (~/.android/debug.keystore, which keytool makes when there is none;
#                                           the same key each time, so a new APK installs over the last one and
#                                           keeps what the game wrote)

target_compile_definitions(orunners_game_console PUBLIC ORUNNERS_HANDHELD ORUNNERS_SYSTEM_EXIT ORUNNERS_SCREEN_WIDE
	ORUNNERS_TOUCH)

set(ORUNNERS_ANDROID_VERSION ${PROJECT_VERSION})
math(EXPR ORUNNERS_ANDROID_VERSION_CODE    # (major * 10000 + minor * 100 + patch)
	"${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
set(ORUNNERS_ANDROID_TARGET_SDK 35)
set(ORUNNERS_ANDROID_MIN_SDK 24)    # (Android 7: the APK's signature is v2's alone, as OutRunners APK signs it)
string(REGEX MATCH "[0-9]+" ORUNNERS_ANDROID_PLATFORM_LEVEL "${ANDROID_PLATFORM}")
if(ORUNNERS_ANDROID_PLATFORM_LEVEL GREATER ORUNNERS_ANDROID_MIN_SDK)
	set(ORUNNERS_ANDROID_MIN_SDK ${ORUNNERS_ANDROID_PLATFORM_LEVEL})
endif()
set(ORUNNERS_ANDROID_ICON "" CACHE FILEPATH
	"The launcher's icon: a square picture of the player's own (JPEG or PNG), instead of the wheel")
set(ORUNNERS_ANDROID_KEYSTORE "" CACHE FILEPATH "The key the APK is signed with (by default the debug one)")
if(NOT ORUNNERS_ANDROID_ICON AND EXISTS ${CMAKE_SOURCE_DIR}/local/icon_android.png)
	set(ORUNNERS_ANDROID_ICON ${CMAKE_SOURCE_DIR}/local/icon_android.png)    # (local/: the player's own pictures,
endif()                                                                      #  never in the repository)

# SDL2 from its source, as the releases' (FETCHCONTENT_SOURCE_DIR_SDL2 picks a copy already here): its
# library, libSDL2.so, and its Java
include(FetchContent)
FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.32.10
	GIT_SHALLOW TRUE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

add_library(main SHARED ports/sdl2/main.c ports/sdl2/forces.c ports/sdl2/controls.c ports/sdl2/letters.c
	ports/sdl2/touch.c)
target_link_libraries(main PRIVATE orunners_game_console orunners_sound SDL2::SDL2)

# The SDK around the NDK (sdk/ndk/<version>), else ANDROID_HOME: the platform's android.jar, the newest
# build-tools
if(DEFINED ENV{ANDROID_HOME})
	set(ORUNNERS_ANDROID_SDK $ENV{ANDROID_HOME})
else()
	get_filename_component(ORUNNERS_ANDROID_SDK ${ANDROID_NDK}/../.. ABSOLUTE)
endif()
set(ORUNNERS_ANDROID_JAR ${ORUNNERS_ANDROID_SDK}/platforms/android-${ORUNNERS_ANDROID_TARGET_SDK}/android.jar)
if(NOT EXISTS ${ORUNNERS_ANDROID_JAR})
	message(FATAL_ERROR "No android.jar of Android ${ORUNNERS_ANDROID_TARGET_SDK} in the SDK (${ORUNNERS_ANDROID_SDK})")
endif()
file(GLOB ORUNNERS_BUILD_TOOLS LIST_DIRECTORIES true ${ORUNNERS_ANDROID_SDK}/build-tools/*)
list(SORT ORUNNERS_BUILD_TOOLS COMPARE NATURAL)
list(POP_BACK ORUNNERS_BUILD_TOOLS ORUNNERS_BUILD_TOOLS_DIR)
foreach(tool IN ITEMS aapt2 aapt zipalign d8 apksigner)
	string(TOUPPER ${tool} name)
	find_program(ORUNNERS_${name} NAMES ${tool} ${tool}.bat HINTS ${ORUNNERS_BUILD_TOOLS_DIR} NO_DEFAULT_PATH REQUIRED)
endforeach()

# Java 17 (JAVA_HOME): javac and jar for SDL's Java, keytool for the debug key; d8 and apksigner run with it
find_package(Java 17 COMPONENTS Development REQUIRED)
get_filename_component(ORUNNERS_JAVA_BIN ${Java_JAVAC_EXECUTABLE} REALPATH)
get_filename_component(ORUNNERS_JAVA_BIN ${ORUNNERS_JAVA_BIN} DIRECTORY)
get_filename_component(ORUNNERS_JAVA_HOME ${ORUNNERS_JAVA_BIN} DIRECTORY)
find_program(ORUNNERS_KEYTOOL keytool HINTS ${ORUNNERS_JAVA_BIN} REQUIRED)
set(ORUNNERS_WITH_JAVA ${CMAKE_COMMAND} -E env JAVA_HOME=${ORUNNERS_JAVA_HOME})

set(ORUNNERS_APK_WORK ${CMAKE_BINARY_DIR}/apk)
set(ORUNNERS_APK ${ORUNNERS_BIN_DIR}/android/OutRunners-debug.apk)    # (not OutRunners APK's OutRunners.apk)

# SDL's Java, the activity that starts libmain.so: classes.dex
file(GLOB ORUNNERS_SDL_JAVA ${sdl2_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app/*.java)
add_custom_command(OUTPUT ${ORUNNERS_APK_WORK}/dex/classes.dex
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${ORUNNERS_APK_WORK}/classes ${ORUNNERS_APK_WORK}/dex
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORUNNERS_APK_WORK}/classes ${ORUNNERS_APK_WORK}/dex
	COMMAND ${Java_JAVAC_EXECUTABLE} -source 8 -target 8 -Xlint:-options -nowarn -encoding UTF-8
		-bootclasspath ${ORUNNERS_ANDROID_JAR} -classpath ${ORUNNERS_BUILD_TOOLS_DIR}/core-lambda-stubs.jar
		-d ${ORUNNERS_APK_WORK}/classes ${ORUNNERS_SDL_JAVA}    # (the stubs: the lambdas' LambdaMetafactory)
	COMMAND ${Java_JAR_EXECUTABLE} cf ${ORUNNERS_APK_WORK}/classes.jar -C ${ORUNNERS_APK_WORK}/classes .
	COMMAND ${ORUNNERS_WITH_JAVA} ${ORUNNERS_D8} --release --min-api ${ORUNNERS_ANDROID_MIN_SDK} --lib ${ORUNNERS_ANDROID_JAR}
		--output ${ORUNNERS_APK_WORK}/dex ${ORUNNERS_APK_WORK}/classes.jar
	DEPENDS ${ORUNNERS_SDL_JAVA}
	COMMENT "SDL's Java: classes.dex"
	VERBATIM)


# The game's files in every APK: the libraries (without their symbols) and the Java, named as the APK has them
set(ORUNNERS_APK_FILES ${ORUNNERS_APK_WORK}/files)
set(ORUNNERS_APK_LIB lib/${ANDROID_ABI})
add_custom_command(OUTPUT ${ORUNNERS_APK_FILES}/classes.dex ${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libmain.so
		${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libSDL2.so
	COMMAND ${CMAKE_COMMAND} -E rm -rf ${ORUNNERS_APK_FILES}
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}
	COMMAND ${CMAKE_STRIP} --strip-unneeded -o ${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libmain.so $<TARGET_FILE:main>
	COMMAND ${CMAKE_STRIP} --strip-unneeded -o ${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libSDL2.so
		$<TARGET_FILE:SDL2::SDL2>
	COMMAND ${CMAKE_COMMAND} -E copy ${ORUNNERS_APK_WORK}/dex/classes.dex ${ORUNNERS_APK_FILES}
	DEPENDS main SDL2 ${ORUNNERS_APK_WORK}/dex/classes.dex
	COMMENT "The APK's libraries and Java"
	VERBATIM)
set(ORUNNERS_APK_GAME_FILES ${ORUNNERS_APK_FILES}/classes.dex ${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libmain.so
	${ORUNNERS_APK_FILES}/${ORUNNERS_APK_LIB}/libSDL2.so)

# An APK's manifest, icon and assets, as aapt2 links them from the folder 'stage' (its res/ and assets/,
# which 'commands' fill), then the game's files in and aligned: 'apk'
function(orunners_apk apk stage commands depends)
	add_custom_command(OUTPUT ${apk}
		COMMAND ${CMAKE_COMMAND} -E rm -rf ${stage}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${stage}/assets
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/ports/android/res ${stage}/res
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/ports/sdl2/font/font.bmp ${stage}/assets
		${commands}
		COMMAND ${ORUNNERS_AAPT2} compile --dir ${stage}/res -o ${stage}/res.zip
		COMMAND ${ORUNNERS_AAPT2} link -o ${stage}/linked.apk -I ${ORUNNERS_ANDROID_JAR}
			--manifest ${CMAKE_SOURCE_DIR}/ports/android/AndroidManifest.xml
			--min-sdk-version ${ORUNNERS_ANDROID_MIN_SDK} --target-sdk-version ${ORUNNERS_ANDROID_TARGET_SDK}
			--version-code ${ORUNNERS_ANDROID_VERSION_CODE} --version-name ${ORUNNERS_ANDROID_VERSION}
			-A ${stage}/assets -0 assets ${stage}/res.zip    # (the pack stored: read in place, never unpacked)
		COMMAND ${CMAKE_COMMAND} -E chdir ${ORUNNERS_APK_FILES} ${ORUNNERS_AAPT} add ${stage}/linked.apk classes.dex
			${ORUNNERS_APK_LIB}/libmain.so ${ORUNNERS_APK_LIB}/libSDL2.so
		COMMAND ${ORUNNERS_ZIPALIGN} -p -f 4 ${stage}/linked.apk ${apk}
		DEPENDS ${CMAKE_SOURCE_DIR}/ports/android/AndroidManifest.xml ${ORUNNERS_ANDROID_RES}
			${CMAKE_SOURCE_DIR}/ports/sdl2/font/font.bmp ${ORUNNERS_APK_GAME_FILES} ${depends}
		COMMENT "The APK: ${apk}"
		VERBATIM)
endfunction()

file(GLOB_RECURSE ORUNNERS_ANDROID_RES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/ports/android/res/*)

# The game's Android build for OutRunners APK (ports/android/apk), which puts the player's assets pack in it
# (and their icon, ic_launcher_own.xml and the picture in ic_launcher_art's place), and signs it: no pack, no
# signature
set(ORUNNERS_APK_BASE ${ORUNNERS_BIN_DIR}/android/outrunners-base.apk)
orunners_apk(${ORUNNERS_APK_BASE} ${ORUNNERS_APK_WORK}/base "" "")

# This computer's own APK: its assets pack, its icon (ORUNNERS_ANDROID_ICON, as OutRunners APK puts it) and
# the debug key of its Android tools (made once)
set(ORUNNERS_ANDROID_PACK ${ORUNNERS_BIN_DIR}/outrunners.assets)
set(ORUNNERS_APK_STAGE ${ORUNNERS_APK_WORK}/player)
set(ORUNNERS_APK_EXTRA "")
set(ORUNNERS_APK_EXTRA_FILES "")
if(EXISTS ${ORUNNERS_ANDROID_PACK})
	list(APPEND ORUNNERS_APK_EXTRA COMMAND ${CMAKE_COMMAND} -E copy ${ORUNNERS_ANDROID_PACK} ${ORUNNERS_APK_STAGE}/assets)
	list(APPEND ORUNNERS_APK_EXTRA_FILES ${ORUNNERS_ANDROID_PACK})
else()
	message(STATUS "No assets pack at ${ORUNNERS_ANDROID_PACK}: this computer's APK gets the game alone")
endif()
if(ORUNNERS_ANDROID_ICON)
	get_filename_component(ORUNNERS_ICON_TYPE ${ORUNNERS_ANDROID_ICON} LAST_EXT)
	string(TOLOWER ${ORUNNERS_ICON_TYPE} ORUNNERS_ICON_TYPE)
	set(ORUNNERS_APK_MIPMAP ${ORUNNERS_APK_STAGE}/res/mipmap-xxxhdpi)
	list(APPEND ORUNNERS_APK_EXTRA
		COMMAND ${CMAKE_COMMAND} -E rm -f ${ORUNNERS_APK_MIPMAP}/ic_launcher.png ${ORUNNERS_APK_MIPMAP}/ic_launcher_art.png
		COMMAND ${CMAKE_COMMAND} -E copy ${ORUNNERS_ANDROID_ICON} ${ORUNNERS_APK_MIPMAP}/ic_launcher${ORUNNERS_ICON_TYPE}
		COMMAND ${CMAKE_COMMAND} -E copy ${ORUNNERS_ANDROID_ICON} ${ORUNNERS_APK_MIPMAP}/ic_launcher_art${ORUNNERS_ICON_TYPE}
		COMMAND ${CMAKE_COMMAND} -E copy ${ORUNNERS_APK_STAGE}/res/mipmap-anydpi-v26/ic_launcher_own.xml
			${ORUNNERS_APK_STAGE}/res/mipmap-anydpi-v26/ic_launcher.xml)
	list(APPEND ORUNNERS_APK_EXTRA_FILES ${ORUNNERS_ANDROID_ICON})
endif()
orunners_apk(${ORUNNERS_APK_WORK}/player.apk ${ORUNNERS_APK_STAGE} "${ORUNNERS_APK_EXTRA}" "${ORUNNERS_APK_EXTRA_FILES}")

if(ORUNNERS_ANDROID_KEYSTORE)
	set(ORUNNERS_KEYSTORE ${ORUNNERS_ANDROID_KEYSTORE})
elseif(CMAKE_HOST_WIN32)
	file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.android/debug.keystore" ORUNNERS_KEYSTORE)
else()
	set(ORUNNERS_KEYSTORE $ENV{HOME}/.android/debug.keystore)
endif()
get_filename_component(ORUNNERS_KEYSTORE_DIR ${ORUNNERS_KEYSTORE} DIRECTORY)
set(ORUNNERS_KEYSTORE_MADE "")
if(NOT EXISTS ${ORUNNERS_KEYSTORE})    # (one this computer's Android tools made already is kept as it is)
	add_custom_command(OUTPUT ${ORUNNERS_KEYSTORE}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${ORUNNERS_KEYSTORE_DIR}
		COMMAND ${ORUNNERS_KEYTOOL} -genkeypair -keystore ${ORUNNERS_KEYSTORE} -storepass android -alias androiddebugkey
			-keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
		COMMENT "The debug key: ${ORUNNERS_KEYSTORE}"
		VERBATIM)
	set(ORUNNERS_KEYSTORE_MADE ${ORUNNERS_KEYSTORE})
endif()
add_custom_command(OUTPUT ${ORUNNERS_APK}
	COMMAND ${ORUNNERS_WITH_JAVA} ${ORUNNERS_APKSIGNER} sign --ks ${ORUNNERS_KEYSTORE} --ks-pass pass:android
		--ks-key-alias androiddebugkey --key-pass pass:android --out ${ORUNNERS_APK} ${ORUNNERS_APK_WORK}/player.apk
	DEPENDS ${ORUNNERS_APK_WORK}/player.apk ${ORUNNERS_KEYSTORE_MADE}
	COMMENT "This computer's APK, signed: ${ORUNNERS_APK}"
	VERBATIM)
add_custom_target(orunners_apk ALL DEPENDS ${ORUNNERS_APK_BASE} ${ORUNNERS_APK})
