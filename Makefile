#---------------------------------------------------------------------------------
# Lone Lookout -- Wii homebrew build (devkitPPC + libogc)
#
#   make            generate assets (Python) and build lonelookout.dol
#   make SHOTS=1    build the screenshot-tour variant (scripted camera, no input)
#   make WALKTEST=1 build the scripted-input variant (walks, answers the radio, pages the notebook)
#   make SOAK=1     build the audio soak variant (Day 11, music, calls and Listen on a loop;
#                   presents one frame every 10 s with the mixer stats -- tools/capture/soak.sh)
#   make test       host tests: the world, the story playthroughs, the music
#   make package    dist/apps/lonelookout/{boot.dol,meta.xml,icon.png}
#   make run        send to a Wii running the Homebrew Channel (wiiload)
#   make clean
#---------------------------------------------------------------------------------
.SUFFIXES:
.SECONDARY:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
TARGET		:=	lonelookout
BUILD		:=	build
GEN			:=	gen
SOURCES		:=	src src/core src/gfx src/input src/world src/game src/audio
DATA		:=	$(GEN)
INCLUDES	:=	src $(GEN)
PYTHON		?=	python3

#---------------------------------------------------------------------------------
# Code generation options
#---------------------------------------------------------------------------------
CFLAGS		=	-g -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type \
				-ffast-math -fno-strict-aliasing $(MACHDEP) $(INCLUDE) $(DEFINES)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifeq ($(SHOTS),1)
DEFINES		+=	-DLL_SHOT_TOUR=1
TARGET		:=	lonelookout_shots
BUILD		:=	build_shots
endif
ifeq ($(WALKTEST),1)
DEFINES		+=	-DLL_INPUT_SCRIPT=1
TARGET		:=	lonelookout_walktest
BUILD		:=	build_walktest
endif
ifeq ($(SOAK),1)
DEFINES		+=	-DLL_INPUT_SCRIPT=1 -DLL_SOAK=1
TARGET		:=	lonelookout_soak
BUILD		:=	build_soak
endif

LIBS	:=	-lfat -laesnd -lwiiuse -lbte -logc -lm
LIBDIRS	:=

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
# Outer make: generate assets first. assets.mk is a generated makefile; GNU make
# rebuilds it (running the Python generators) and then re-reads this Makefile,
# so the wildcard below always sees the freshly generated .bin files.
#---------------------------------------------------------------------------------
.DEFAULT_GOAL	:=	all
ASSET_DEPS	:=	$(wildcard tools/assets/*.py tools/font/*.py tools/worldgen/*.py tools/dialogue/*.py tools/music/*.py \
				data/palette/*.json data/dialogue/*.lls data/music/*.txt)

$(GEN)/assets.mk: $(ASSET_DEPS)
	@echo assets ...
	@$(PYTHON) tools/assets/build_assets.py $(GEN)
	@touch $@

ifneq ($(MAKECMDGOALS),clean)
include $(GEN)/assets.mk
endif

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(GEN_BINS)

export LD	:=	$(CC)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES	:=	$(OFILES_BIN) $(CFILES:.c=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(LIBOGC_LIB)
export DEFINES

.PHONY: all $(BUILD) clean run package test

# Host-side tests of pure game logic (system compiler, no Wii toolchain needed).
HOST_CC		?=	cc
HOST_TEST_SRC	:=	tests/host/test_world.c tests/host/blobs_host.c src/world/ashen.c src/world/walk.c \
					src/world/places.c src/core/noise.c src/audio/voice.c

HOST_STORY_SRC	:=	tests/host/test_story.c src/game/dialogue.c src/audio/voice.c src/core/noise.c
HOST_AUDIO_SRC	:=	tests/host/test_audio.c src/audio/music.c src/audio/ambient.c

test: $(GEN)/assets.mk
	@$(HOST_CC) -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Itests/host/stub -Isrc -I$(GEN) \
		-o tests/host/run_tests $(HOST_TEST_SRC) -lm
	@$(HOST_CC) -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -Itests/host/stub -Isrc -I$(GEN) \
		-o tests/host/run_story $(HOST_STORY_SRC) -lm
	@$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Itests/host/stub -Isrc -I$(GEN) \
		-o tests/host/run_audio $(HOST_AUDIO_SRC) -lm
	@./tests/host/run_tests
	@./tests/host/run_story
	@./tests/host/run_audio

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr build build_shots build_walktest build_soak $(GEN) dist tests/host/run_tests tests/host/run_story tests/host/run_audio *.elf *.dol *.map

run: $(BUILD)
	wiiload $(OUTPUT).dol

package: $(BUILD)
	@mkdir -p dist/apps/lonelookout
	@cp $(OUTPUT).dol dist/apps/lonelookout/boot.dol
	@cp hbc/meta.xml dist/apps/lonelookout/meta.xml
	@$(PYTHON) tools/assets/make_icon.py dist/apps/lonelookout/icon.png
	@echo packaged dist/apps/lonelookout

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
# Inner make (runs inside $(BUILD))
#---------------------------------------------------------------------------------
DEPENDS	:=	$(OFILES:.o=.d)

# elf2dol can leave the last section's size unaligned; loaders (and Dolphin)
# round section sizes up to 32 bytes and refuse a file that ends early, so
# zero-pad the DOL to a 32-byte boundary.
$(OUTPUT).dol: $(OUTPUT).elf
	$(SILENTMSG) output ... $(notdir $@)
	$(SILENTCMD)elf2dol $< $@
	$(SILENTCMD)$(PYTHON) -c "import os,sys; f=sys.argv[1]; n=os.path.getsize(f); open(f,'ab').write(bytes((-n) % 32))" $@

$(OUTPUT).elf: $(OFILES)

# C sources include the bin2o headers (via gen/assets.h), so embed data first.
$(filter-out $(OFILES_BIN),$(OFILES)): $(OFILES_BIN)

%.bin.o : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
