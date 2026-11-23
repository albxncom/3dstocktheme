# 3DStockTheme for the 3DS. Runs in the devkitPro image (README, "3DStockTheme"):
#   make          build/<name>.3dsx (and the .elf and .smdh it is made of)
#   make DEMO=1   the demonstration build in build/demo/: it only pretends to install and delete (main.c, APP_DEMO),
#                 to show the screens after an install in an emulator; never delivered
#   make PROBE=1  the probe build in build/probe/: it compares both screens' frames while a screen stands still
#                 and logs every change (main.c, APP_PROBE, and the kit's UI_PROBE; the white flicker); never delivered
#   make LOOKS="stock night"   only these looks (every ../uikit/source/look_<name>.c there if not said; Stock always),
#                 in build/looks-stock-night/ (with DEMO=1 under build/demo/)
#   make sheet    build/ui-sprites.png, every image of the interface enlarged
#   make banner   build/banner.bnr and build/<name>.icn for the CIA; needs BANNERTOOL (bannertool 1.2.3, README)
#                 (the banner's model is the finished file meta/banner.cgfx; meta/icon.py draws the icon of both
#                 the CIA and the .3dsx into build/meta/)
#   make clean    removes build/
# The CIA itself is made by `make cia` in the folder above, on the Mac, where makerom is.
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set: run this in the devkitPro image, see README)
endif
include $(DEVKITARM)/3ds_rules
include identity.mk

# The looks a build holds (G1): a look registers by its files in the kit, ../uikit/source/look_<name>.c (include/look_<name>.h, art/look_<name>.py); the application's drawing of it is ui/app_look_<name>.py;
# LOOKS names some of those present. Every option that changes what is built has a build folder of its own, so builds never mix objects.
ifneq ($(DESIGN),)
$(error DESIGN is gone: Stock is built always, and the looks a build holds are named by LOOKS (G1))
endif
LOOKS_ALL     := stock frost night prism
LOOKS_PRESENT := $(foreach n,$(LOOKS_ALL),$(if $(wildcard ../uikit/source/look_$(n).c),$(n)))
LOOKS         ?= $(LOOKS_PRESENT)
ifneq ($(filter-out $(LOOKS_PRESENT),$(LOOKS)),)
$(error LOOKS: no ../uikit/source/look_<name>.c for $(filter-out $(LOOKS_PRESENT),$(LOOKS)) (there: $(LOOKS_PRESENT)))
endif
override LOOKS := $(filter stock $(LOOKS),$(LOOKS_PRESENT))
SPACE   := $(subst ,, )
SOME    := $(if $(filter-out $(LOOKS),$(LOOKS_PRESENT)),looks-$(subst $(SPACE),-,$(LOOKS)))
BUILD0  := $(if $(filter 1,$(DEMO)),build/demo,$(if $(filter 1,$(PROBE)),build/probe,build))
BUILD   := $(BUILD0)$(if $(SOME),/$(SOME))
OUT     := $(BUILD)/$(APP_TITLE)
# The Homebrew Launcher lists a .3dsx by its SMDH's title, so the two extra builds say there what they are
# (two entries of one name could not be told apart on the console).
LISTED  := $(APP_TITLE)$(if $(filter 1,$(DEMO)), Demo)$(if $(filter 1,$(PROBE)), Probe)
# ui/ and the kit (../uikit/source) in the order of their names, as when they were one folder: the same link order
UI_C    := $(filter-out $(patsubst %,../uikit/source/look_%.c,$(filter-out $(LOOKS),$(LOOKS_ALL))),\
           $(wildcard ui/*.c ../uikit/source/*.c))
SOURCES := $(wildcard source/*.c) $(sort $(notdir $(UI_C))) $(wildcard ../core/*.c)
VENDOR  := $(wildcard vendor/quirc/*.c)
# ../uikit/art/art.py draws the interface's images from ../uikit/include/ui_layout.h and ui/layout.h, per look (../uikit/art/look_<name>.py) and colour scheme
# (../uikit/art/schemes.py): each look's part, then their join as C (ui_art.h, ui_art.c, art_<look>.c; the application's part
# part_art.h, part_art.c, part_art_<look>.c) and the banner's sound.
# One art folder for the plain, demo and probe builds, so one export draws it once. With it looks_built.h, the list
# look.c and main.c read (look.h).
ARTDIR  := build$(if $(SOME),/$(SOME))/art
ART     := $(ARTDIR)/ui_art.c $(ARTDIR)/ui_art.h $(patsubst %,$(ARTDIR)/art_%.c,$(LOOKS)) $(ARTDIR)/part_art.c \
           $(patsubst %,$(ARTDIR)/part_art_%.c,$(LOOKS)) $(ARTDIR)/part_art.h $(ARTDIR)/banner.wav
LOOKS_X := $(foreach n,$(LOOKS),X($(n), $(shell echo $(n) | tr a-z A-Z)))
LOOKS_H := /* make: the looks this build holds (LOOKS, look.h) */\n\#define LOOKS_HELD $(words $(LOOKS))
LOOKS_H += \n\#define LOOKS_BUILT(X) $(LOOKS_X)\n
# The sounds: the files of ../uikit/sound/ as they are, linked in through ../uikit/source/sound_data.S (audio.c reads
# them at start); which of the music tracks go in is ../uikit/include/tracks.h's table
SOUNDS  := $(wildcard ../uikit/sound/*.wav) $(wildcard ../uikit/sound/*.bcstm)
OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.c=.o))) $(BUILD)/ui_art.o $(patsubst %,$(BUILD)/art_%.o,$(LOOKS)) \
           $(BUILD)/part_art.o $(patsubst %,$(BUILD)/part_art_%.o,$(LOOKS)) $(BUILD)/sound_data.o \
           $(addprefix $(BUILD)/vendor/,$(notdir $(VENDOR:.c=.o)))
vpath %.c source ui ../uikit/source ../core

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
DEFINES := -DAPP_NAME='"$(APP_TITLE)"' -DAPP_VERSION_MAJOR=$(APP_VERSION_MAJOR) \
           -DAPP_VERSION_MINOR=$(APP_VERSION_MINOR) -DAPP_VERSION_MICRO=$(APP_VERSION_MICRO) \
           -DAPP_VERSION_PRE='"$(APP_VERSION_PRE)"' \
           $(if $(filter 1,$(DEMO)),-DAPP_DEMO) $(if $(filter 1,$(PROBE)),-DAPP_PROBE -DUI_PROBE)
BASE    := -std=gnu11 -O2 -g -mword-relocations -ffunction-sections $(ARCH) -D__3DS__ -DCURL_STATICLIB \
           -isystem $(CTRULIB)/include -isystem $(PORTLIBS)/include
CFLAGS  := $(BASE) -Wall -Wextra -Werror -Wshadow -Wvla $(DEFINES) -Isource -Iui -I../uikit/include -I$(ARTDIR) -I../core -Ivendor/quirc
# quirc as released: it warns under -Wextra and -Wshadow (unused parameters, signedness), not under -Wall
VENDOR_CFLAGS := $(BASE) -Wall -Werror
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(OUT).map -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
# libcurl and its TLS (mbedtls) from the image's portlibs, as their pkg-config file lists them
LIBS    := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

.PHONY: all banner sheet clean FORCE
all: $(OUT).3dsx

$(BUILD) $(ARTDIR):
	@mkdir -p $@

# identity.mk: APP_NAME and the version are compiled in (DEFINES), so a new version rebuilds every object
$(BUILD)/%.o: %.c identity.mk | $(ARTDIR)/ui_art.h $(ARTDIR)/looks_built.h $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/ui_art.o $(patsubst %,$(BUILD)/art_%.o,$(LOOKS)) $(BUILD)/part_art.o \
$(patsubst %,$(BUILD)/part_art_%.o,$(LOOKS)): $(BUILD)/%.o: $(ARTDIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# written anew only when it would change (the objects that include it follow through their .d files)
$(ARTDIR)/looks_built.h: FORCE | $(ARTDIR)
	@printf '$(LOOKS_H)' | cmp -s - $@ || printf '$(LOOKS_H)' > $@

$(BUILD)/sound_data.o: ../uikit/source/sound_data.S ../uikit/include/tracks.h $(SOUNDS) | $(BUILD)
	$(CC) $(ARCH) -I../uikit/include -Wa,-I../uikit/sound -c $< -o $@

$(BUILD)/vendor/%.o: vendor/quirc/%.c
	@mkdir -p $(BUILD)/vendor
	$(CC) $(VENDOR_CFLAGS) -MMD -MP -c $< -o $@

$(OUT).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

# the application's half of the generator (art.py --app): the hook, the images as 0.9.3 draws them, the installing
# screen's; each look's own drawing of them is ui/app_look_<look>.py
APP_ART    := ui/app_art.py
APP_ART_PY := $(APP_ART) ui/app_images.py ui/pour_art.py
# the Tabler icons the parts stroke (art.py, tabler())
ART_ICONS  := $(wildcard ../uikit/icons/*.svg)
$(ARTDIR)/stock.part: ../uikit/art/art.py ../uikit/art/look_stock.py ui/app_look_stock.py $(APP_ART_PY) ../uikit/art/schemes.py ui/layout.h \
                      ../uikit/art/liquid.py ../uikit/include/popup_band.h ../uikit/include/ui_layout.h ui/pour_layout.h $(ART_ICONS) | $(ARTDIR)
	python3 -I ../uikit/art/art.py --app $(APP_ART) --look stock --part $@

# another look's part: its .py and its own numbers (../uikit/include/look_<name>.h, when it has them)
.SECONDEXPANSION:
$(ARTDIR)/%.part: ../uikit/art/art.py ../uikit/art/look_%.py ui/app_look_%.py $$(wildcard ../uikit/include/look_$$*.h) $(APP_ART_PY) ../uikit/art/schemes.py \
                  ../uikit/art/liquid.py ui/layout.h ../uikit/include/ui_layout.h ../uikit/include/popup_band.h ui/pour_layout.h $(ART_ICONS) | $(ARTDIR)
	python3 -I ../uikit/art/art.py --app $(APP_ART) --look $* --part $@

$(ART) &: $(patsubst %,$(ARTDIR)/%.part,$(LOOKS)) $(ARTDIR)/looks_built.h ../uikit/art/art.py $(APP_ART) ../uikit/art/schemes.py ui/layout.h \
          ../uikit/art/liquid.py ../uikit/include/ui_layout.h ../uikit/include/popup_band.h
	python3 -I ../uikit/art/art.py --app $(APP_ART) --join $(ARTDIR) $(LOOKS)

# all images at twice their size on the background colour, to look at: build/ui-sprites.png
sheet: | $(BUILD)
	python3 -I ../uikit/art/art.py --app $(APP_ART) --look stock --sheet $(BUILD)/ui-sprites.png

# The HOME Menu's icon at 48 and 24 px: the Stock house raised in teal on a light ground, the same in every scheme
META    := $(BUILD)/meta/icon.png $(BUILD)/meta/icon24.png
$(META) &: meta/icon.py
	python3 -I meta/icon.py $(BUILD)/meta

$(OUT).smdh: $(BUILD)/meta/icon.png $(BUILD)/meta/icon24.png identity.mk
	smdhtool --create "$(LISTED)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(BUILD)/meta/icon.png $@ \
		$(BUILD)/meta/icon24.png

$(OUT).3dsx: $(OUT).elf $(OUT).smdh
	3dsxtool $< $@ --smdh=$(OUT).smdh

# The CIA's banner and its HOME Menu icon, which also says "visible" (smdhtool's SMDH is for the Homebrew Launcher).
# The banner's 3D model is the finished file meta/banner.cgfx (meta/banner.txt), packed with the chime.
banner: $(BUILD)/banner.bnr $(OUT).icn

$(BUILD)/banner.bnr: meta/banner.cgfx $(ARTDIR)/banner.wav
	@test -x "$(BANNERTOOL)" || { echo "BANNERTOOL is not set to bannertool 1.2.3 (README)"; exit 1; }
	"$(BANNERTOOL)" makebanner -ci meta/banner.cgfx -a $(ARTDIR)/banner.wav -o $@

$(OUT).icn: $(BUILD)/meta/icon.png identity.mk
	@test -x "$(BANNERTOOL)" || { echo "BANNERTOOL is not set to bannertool 1.2.3 (README)"; exit 1; }
	"$(BANNERTOOL)" makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $< \
		-f visible,nosavebackups -o $@
	python3 -I meta/icon.py --small $@

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
