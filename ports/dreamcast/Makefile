# Cross-compilation for KallistiOS runtime components.
# Invoked after sourcing /opt/toolchains/dc/kos/environ.sh

.DEFAULT_GOAL := all

# Include KOS build rules
include $(KOS_BASE)/Makefile.rules

# Paths
ROOT := ../..
BUILD := build

# Source and Object directories
SRC_DIR := $(ROOT)/runtime/src
OBJ_DIR := $(BUILD)/runtime
WORKERS_OBJ := $(BUILD)/workers_serial.o
TASKS_KOS_OBJ := $(BUILD)/tasks_kos.o
DIAG_OBJ := $(BUILD)/diagnostic.o
DIAG_ELF := $(BUILD)/outrunners-diagnostic.elf
DIAG_MAP := $(BUILD)/outrunners-diagnostic.map

# Library output
LIB_NAME := libs32runtime-dc.a
LIB_PATH := $(BUILD)/$(LIB_NAME)

# Runtime components to compile
# Note: 'tasks' is excluded because runtime/src/tasks.c requires unavailable ucontext.h.
RUNTIME_NAMES := machine io script sound ym3438 memory_census video multipcm sound_chips

# Object files for runtime components
RUNTIME_OBJS := $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(RUNTIME_NAMES)))

# All objects to include in the archive
ALL_OBJS := $(RUNTIME_OBJS) $(WORKERS_OBJ) $(TASKS_KOS_OBJ)

# Compiler and Archiver
CC := kos-cc
AR := $(KOS_AR)

# Flags
CFLAGS := -std=gnu11 -Wall -Wextra -fno-strict-aliasing -ffunction-sections -fdata-sections
CFLAGS += -I$(ROOT)/runtime/include
CFLAGS += $(KOS_CFLAGS)
# Retain strict game arithmetic
CFLAGS += -fno-fast-math
# No sound output yet: the sound program, chip registers and timers run, samples are not made
# (runtime/src/sound_chips.c; host-verified identical game and sound board state). Remove when the
# AICA output exists.
CFLAGS += -DS32_SOUND_SILENT
# render_emit: a frame stops building sprite textures 4 ms into its work (the rest wait a frame)
CFLAGS += -DRT_BUILD_US=4000

# The console build (console/: the PC version's menus, & KNUCKLES and OPTIONS) without 2 PLAYERS, VIDEO
# and EXIT (a game device's menus), the network game (console_nonet.c in its place) and TIME ATTACK's
# ghost (no memory for a run's recording); the extra palette is drawlist.c's, not the software video's.
# (Changing these needs a clean build: the game's own sources read ORUNNERS_CONSOLE.)
CFLAGS += -DORUNNERS_CONSOLE -DORUNNERS_HANDHELD -DORUNNERS_SYSTEM_EXIT -DORUNNERS_NO_NETWORK -DORUNNERS_NO_GHOST
CFLAGS += -DS32_VIDEO_EXTRA_ELSEWHERE '-DORUNNERS_VERSION="0.2 DC"' -I$(ROOT)/console -I$(ROOT)/src

# Dependency generation flags
DEPFLAGS = -MMD -MP

# Phony targets
.PHONY: all runtime-check clean diagnostic game-check

# Default target: runtime-check only until diagnostic.c exists
all: runtime-check

# Check for KOS environment
runtime-check: $(LIB_PATH)
	@echo "Checking KOS environment..."
	@if [ -z "$(KOS_BASE)" ]; then \
		echo "Error: KOS_BASE is not set. Please source /opt/toolchains/dc/kos/environ.sh"; \
		exit 1; \
	fi
	@if [ ! -f "$(KOS_BASE)/Makefile.rules" ]; then \
		echo "Error: $(KOS_BASE)/Makefile.rules not found."; \
		exit 1; \
	fi
	@echo "KOS environment OK."

# Compile runtime sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile workers_serial.c
$(WORKERS_OBJ): workers_serial.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile local tasks_kos.c
$(TASKS_KOS_OBJ): tasks_kos.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile diagnostic.c
$(DIAG_OBJ): diagnostic.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Archive objects into static library
$(LIB_PATH): $(ALL_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(ALL_OBJS)

# Diagnostic target: links diagnostic.o and entire runtime archive
# Purpose: compile/link baseline and accurate linked static memory audit.
# No --gc-sections to retain runtime for size audit.
diagnostic: $(DIAG_ELF)

$(DIAG_ELF): $(DIAG_OBJ) $(LIB_PATH)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(DIAG_OBJ) \
		-Wl,--whole-archive $(LIB_PATH) -Wl,--no-whole-archive \
		-lm \
		-Wl,-Map,$(DIAG_MAP)

# --- Game Check Targets ---

# Game source directory
GAME_SRC_DIR := $(ROOT)/src

# Game object directories
GAME_OBJ_DIR := $(BUILD)/game
SOUND_OBJ_DIR := $(BUILD)/sound

# Runtime glue components for game
GAME_GLUE_NAMES := game assets assets_media assets_midi assets_pack json png inflate
GAME_GLUE_OBJS := $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(GAME_GLUE_NAMES)))

# Sound CPU component
SOUND_CPU_OBJ := $(OBJ_DIR)/sound_cpu.o

# List all game source files (excluding sound)
# Using wildcard for one level deep as per project structure
GAME_SRCS := $(filter-out $(GAME_SRC_DIR)/sound/%.c, $(wildcard $(GAME_SRC_DIR)/*.c $(GAME_SRC_DIR)/*/*.c))

# List all sound source files
SOUND_SRCS := $(wildcard $(GAME_SRC_DIR)/sound/*.c)

# Map game sources to objects preserving relative path
# e.g. src/foo.c -> build/game/foo.o
# e.g. src/bar/baz.c -> build/game/bar/baz.o
GAME_OBJS := $(patsubst $(GAME_SRC_DIR)/%.c,$(GAME_OBJ_DIR)/%.o,$(GAME_SRCS))

# Map sound sources to objects with basename only
# e.g. src/sound/foo.c -> build/sound/foo.o
SOUND_OBJS := $(patsubst $(GAME_SRC_DIR)/sound/%.c,$(SOUND_OBJ_DIR)/%.o,$(SOUND_SRCS))

# Game library
GAME_LIB_NAME := liborunners-game-dc.a
GAME_LIB_PATH := $(BUILD)/$(GAME_LIB_NAME)

# Sound library
SOUND_LIB_NAME := liborunners-sound-dc.a
SOUND_LIB_PATH := $(BUILD)/$(SOUND_LIB_NAME)

# The console's sources (all but the network game)
CONSOLE_SRCS := $(filter-out $(ROOT)/console/network.c, $(wildcard $(ROOT)/console/*.c))
CONSOLE_OBJS := $(patsubst $(ROOT)/console/%.c,$(BUILD)/console/%.o,$(CONSOLE_SRCS)) $(BUILD)/console_nonet.o

$(BUILD)/console/%.o: $(ROOT)/console/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Objects for game library: game code + glue
GAME_LIB_OBJS := $(GAME_OBJS) $(GAME_GLUE_OBJS) $(CONSOLE_OBJS)

# Objects for sound library: sound code + sound_cpu
SOUND_LIB_OBJS := $(SOUND_OBJS) $(SOUND_CPU_OBJ)

# Game check target
game-check: $(GAME_LIB_PATH) $(SOUND_LIB_PATH)
	@echo "Game check complete."

# Compile game sources
# Pattern rule for game objects
$(GAME_OBJ_DIR)/%.o: $(GAME_SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(ROOT)/src $(DEPFLAGS) -c $< -o $@

# Compile sound sources
# Pattern rule for sound objects
$(SOUND_OBJ_DIR)/%.o: $(GAME_SRC_DIR)/sound/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(ROOT)/src $(DEPFLAGS) -c $< -o $@

# Compile game glue components (already defined in RUNTIME_OBJS pattern, but ensure they are built)
# The existing rule $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c handles these if they exist in runtime/src
# However, the prompt says "Compile game runtime glue: game assets assets_media assets_midi assets_pack json png inflate (runtime/src/*.c)"
# So they are in runtime/src. The existing rule covers them.

# Compile sound_cpu
# The existing rule $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c covers this if sound_cpu.c is in runtime/src

# Archive game objects into static library
$(GAME_LIB_PATH): $(GAME_LIB_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(GAME_LIB_OBJS)

# Archive sound objects into static library
$(SOUND_LIB_PATH): $(SOUND_LIB_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $(SOUND_LIB_OBJS)

PROTO_ELF=$(BUILD)/outrunners-prototype.elf
PROTO_OBJS=$(BUILD)/main.o $(BUILD)/page_cache.o $(BUILD)/stream_media.o $(BUILD)/input.o $(BUILD)/present.o $(BUILD)/fault_record.o $(BUILD)/aica_pcm.o $(BUILD)/pvr/drawlist.o $(BUILD)/pvr/render_emit.o $(BUILD)/pvr/pvr_backend.o $(BUILD)/pvr/vram_alloc.o $(BUILD)/vmu_save.o $(BUILD)/startup.o $(BUILD)/diag_screen.o

.PHONY: prototype
prototype: $(PROTO_ELF)

$(BUILD)/fault_record.o: fault_record.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -fno-lto $(DEPFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(PROTO_ELF): $(PROTO_OBJS) $(GAME_LIB_PATH) $(SOUND_LIB_PATH) $(LIB_PATH)
	$(CC) $(CFLAGS) -o $@ $(PROTO_OBJS) -Wl,--start-group $(GAME_LIB_PATH) $(SOUND_LIB_PATH) $(LIB_PATH) -Wl,--end-group -lm -Wl,-Map,$(BUILD)/outrunners-prototype.map -Wl,--wrap=exit,--wrap=abort

# Clean target
clean:
	rm -rf $(BUILD)

# Include dependency files
-include $(ALL_OBJS:.o=.d)
-include $(DIAG_OBJ:.o=.d)
-include $(GAME_OBJS:.o=.d)
-include $(SOUND_OBJS:.o=.d)
-include $(GAME_GLUE_OBJS:.o=.d)
-include $(SOUND_CPU_OBJ:.o=.d)
-include $(PROTO_OBJS:.o=.d)
-include $(CONSOLE_OBJS:.o=.d)
