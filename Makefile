# host:    the core, the harness tpack, the unit tests of the core and of the application's plain C (build/host/);
#          test_audio: the body's wallpapers, a preview's media and the settings file; and the kit's own
#          (uikit/Makefile: test_kit, test_sound, test_probe, in uikit/build/host/)
# asan:    the same with AddressSanitizer and UBSan (build/asan/, uikit/build/asan/)
# test:    both, then the whole test run once per build, the kit's make test with it (needs tools/theme-pack and
#          makerom + ctrtool, see README)
# core3ds: the core as a static library for the 3DS (build/3ds/libtpcore.a); run inside the devkitPro image
# app:     the application, app/build/<name>.3dsx; run inside the devkitPro image (same as make -C app)
# example: the kit's example, uikit/example/build/3ds-uikit-example.3dsx (make -C uikit example); run inside the
#          devkitPro image
# docs-pictures: the pictures of the kit's docs, uikit/docs/img/, drawn from the example's host walk (on the Mac;
#          make -C uikit docs-pictures)
# cia:     the installable title, app/build/<name>.cia, with makerom and ctrtool on this machine (README, 3DStockTheme)
# clean:   removes build/ and app/build/, and the kit's build/ (uikit/build/, uikit/example/build/)

CORE_SRC := core/sha256.c core/zip.c core/theme.c core/lz11.c core/ncch.c core/pack.c core/installed.c core/cia.c \
            core/body.c core/media.c
CORE_HDR := core/tp.h core/internal.h core/sha256.h core/body.h core/media.h
# badges (phase 3a of the badge spec): the badge core is part of the core
CORE_SRC += core/badge.c core/badge_png.c core/badge_set.c
CORE_HDR += core/badge.h core/badge_internal.h
# The application's code that needs no console: built for the host tests as well
APP_SRC  := app/source/files.c app/source/store.c app/source/plan.c app/source/install.c app/source/deletion.c \
            app/source/designs.c \
            app/source/scan.c \
            app/source/text.c app/source/download.c uikit/source/wrap.c \
            uikit/source/scroll.c uikit/source/touch.c uikit/source/popup.c app/source/progress.c \
            uikit/source/wav.c uikit/source/bcstm.c app/source/settings.c uikit/source/tracks.c uikit/source/work.c
# badges (phase 3b): the Badges part's plain C
APP_SRC  += app/source/badge_flow.c app/source/badge_text.c app/source/badge_download.c app/source/badge_dir_store.c
# R9: the installing screen's picture (balls, drops, the liquid's level), the model without drawing
APP_SRC  += app/ui/pour.c
APP_HDR  := $(subst uikit/source/,uikit/include/,$(APP_SRC:.c=.h)) app/ui/layout.h uikit/include/popup_band.h uikit/include/ease.h uikit/include/order.h app/ui/turn.h uikit/include/swarm.h \
            uikit/include/ui_layout.h uikit/include/drift.h app/source/motion.h app/source/paths.h app/ui/pour_ui.h app/ui/pour_layout.h
# the lines the application's tests share with the kit's own (uikit/host)
APP_TEST_HDR := uikit/host/check.h uikit/host/ui_fakes.h
HOST_CC  ?= cc
WARN     := -std=c11 -Wall -Wextra -Werror -Wshadow -Wvla
HOST_LIBS := -lz

.PHONY: host asan test core3ds app cia clean

host: build/host/tpack build/host/test_core build/host/test_app build/host/test_main build/host/test_audio

asan: build/asan/tpack build/asan/test_core build/asan/test_app build/asan/test_main build/asan/test_audio

build/host/libtpcore.a: $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/host
	cd build/host && $(HOST_CC) $(WARN) -O2 -c $(addprefix ../../,$(CORE_SRC))
	ar rcs $@ $(addprefix build/host/,$(notdir $(CORE_SRC:.c=.o)))

build/host/%: host/%.c build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(WARN) -O2 -Icore -o $@ $< build/host/libtpcore.a $(HOST_LIBS)

build/host/test_app: host/test_app.c $(APP_SRC) $(APP_HDR) $(APP_TEST_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(WARN) -O2 -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_app.c $(APP_SRC) build/host/libtpcore.a $(HOST_LIBS) -lcurl

# test_audio: the core's body and media and the application's settings, with the kit's BCSTM reader and track table
AUDIO_SRC := uikit/source/bcstm.c app/source/settings.c uikit/source/tracks.c app/source/files.c
AUDIO_HDR := uikit/include/audio.h uikit/include/bcstm.h app/source/settings.h uikit/include/tracks.h app/source/files.h \
             app/ui/layout.h uikit/include/ui_layout.h uikit/include/popup_band.h uikit/host/check.h uikit/host/mutate.h \
             uikit/host/synth_bcstm.h

build/host/test_audio: host/test_audio.c host/synth.h $(AUDIO_SRC) $(AUDIO_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(WARN) -O2 -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_audio.c $(AUDIO_SRC) build/host/libtpcore.a $(HOST_LIBS) -lm

# test_main: app/source/main.c itself with a simulated touch screen, keys and clock (host/test_main.c); libctru is
# host/fake, the drawing and AM are no-ops (host/main_stubs.c), the images come from art.py as in the app (the atlas
# for the tabs' shape against their hit test)
# The looks: every uikit/source/look_<name>.c there (G1, as make app's LOOKS when not said), their art joined, and
# looks_built.h, the list look.c and main.c read (look.h). The tests of main.c (test_main, test_main_badges,
# test_design) see a build of Stock alone (build/host/one, first on MAIN_FLAGS' path): their flows and
# settings rows are the one-look application's whatever looks the tree holds; a build of two is test_look_rows'.
LOOKS    := $(foreach n,stock frost night prism,$(if $(wildcard uikit/source/look_$(n).c),$(n)))
ART_C    := build/host/art/ui_art.c $(patsubst %,build/host/art/art_%.c,$(LOOKS)) build/host/art/part_art.c \
            $(patsubst %,build/host/art/part_art_%.c,$(LOOKS))
LOOKS_C  := uikit/source/look.c $(patsubst %,uikit/source/look_%.c,$(LOOKS))
LOOKS_X  := $(foreach n,$(LOOKS),X($(n), $(shell echo $(n) | tr a-z A-Z)))
LOOKS_H  := /* make: the looks this build holds (look.h) */\n\#define LOOKS_HELD $(words $(LOOKS))
LOOKS_H  += \n\#define LOOKS_BUILT(X) $(LOOKS_X)\n
MAIN_SRC := host/test_main.c host/main_stubs.c uikit/source/hits.c $(ART_C) app/source/preview.c
MAIN_SRC += host/badge_stubs.c
MAIN_DEP := $(MAIN_SRC) app/source/main.c app/source/console.h app/source/camera.h uikit/include/ui.h app/ui/layout.h uikit/include/ui_layout.h \
            app/ui/app_ui.h host/fake/3ds.h host/fake/citro2d.h build/host/art/ui_art.h build/host/one/looks_built.h uikit/include/look.h \
            app/source/preview.h uikit/include/audio.h app/source/content.h host/synth.h uikit/host/synth_bcstm.h uikit/include/sound_data.h
MAIN_DEP += app/source/badges_ui.h app/source/badge_console.h
MAIN_FLAGS = -std=gnu11 -Ihost/fake -Ibuild/host/one -Ibuild/host/art -Icore -Iapp/source -Iapp/ui -Iuikit/include \
             -DAPP_NAME='"$(APP_TITLE)"' -DAPP_VERSION_MAJOR=$(APP_VERSION_MAJOR) \
             -DAPP_VERSION_MINOR=$(APP_VERSION_MINOR) -DAPP_VERSION_MICRO=$(APP_VERSION_MICRO) \
             -DAPP_VERSION_PRE='"$(APP_VERSION_PRE)"'

# the application's half of the generator (art.py --app): the hook, the images as 0.9.3 draws them, the installing
# screen's; each look's own drawing of them is app/ui/app_look_<look>.py
APP_ART    := app/ui/app_art.py
APP_ART_PY := $(APP_ART) app/ui/app_images.py app/ui/pour_art.py
# the Tabler icons the parts stroke (art.py, tabler())
ART_ICONS  := $(wildcard uikit/icons/*.svg)
build/host/art/stock.part: uikit/art/art.py uikit/art/look_stock.py app/ui/app_look_stock.py $(APP_ART_PY) uikit/art/schemes.py \
                           uikit/art/liquid.py app/ui/layout.h uikit/include/ui_layout.h uikit/include/popup_band.h app/ui/pour_layout.h \
                           $(ART_ICONS)
	@mkdir -p build/host/art
	python3 -I uikit/art/art.py --app $(APP_ART) --look stock --part $@
build/host/art/%.part: uikit/art/art.py uikit/art/look_%.py app/ui/app_look_%.py $(wildcard uikit/include/look_*.h) $(APP_ART_PY) \
                       uikit/art/schemes.py uikit/art/liquid.py app/ui/layout.h uikit/include/ui_layout.h uikit/include/popup_band.h \
                       app/ui/pour_layout.h $(ART_ICONS)
	@mkdir -p build/host/art
	python3 -I uikit/art/art.py --app $(APP_ART) --look $* --part $@
build/host/art/ui_art.h: $(patsubst %,build/host/art/%.part,$(LOOKS)) build/host/art/looks_built.h uikit/art/art.py \
                         $(APP_ART) uikit/art/schemes.py uikit/art/liquid.py app/ui/layout.h uikit/include/ui_layout.h uikit/include/popup_band.h
	python3 -I uikit/art/art.py --app $(APP_ART) --join build/host/art $(LOOKS)
$(ART_C) build/host/art/part_art.h: build/host/art/ui_art.h
# written anew only when it would change
.PHONY: FORCE
build/host/art/looks_built.h: FORCE
	@mkdir -p build/host/art
	@printf '$(LOOKS_H)' | cmp -s - $@ || printf '$(LOOKS_H)' > $@

build/host/one/looks_built.h:
	@mkdir -p build/host/one
	printf '/* host tests only: the application as a build of Stock alone (test_main, test_design) */\n#define LOOKS_HELD 1\n#define LOOKS_BUILT(X) X(stock, STOCK)\n' > $@

build/host/test_main: $(MAIN_DEP) $(APP_SRC) $(APP_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) $(MAIN_FLAGS) -O2 -o $@ $(MAIN_SRC) $(APP_SRC) build/host/libtpcore.a \
		$(HOST_LIBS) -lcurl

SAN := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all

build/asan/test_main: $(MAIN_DEP) $(APP_SRC) $(APP_HDR) $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/asan
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) $(MAIN_FLAGS) $(SAN) -o $@ $(MAIN_SRC) $(APP_SRC) $(CORE_SRC) $(HOST_LIBS) -lcurl

build/asan/test_app: host/test_app.c $(APP_SRC) $(APP_HDR) $(APP_TEST_HDR) $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/asan
	$(HOST_CC) $(WARN) $(SAN) -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_app.c $(APP_SRC) $(CORE_SRC) $(HOST_LIBS) -lcurl

build/asan/test_audio: host/test_audio.c host/synth.h $(AUDIO_SRC) $(AUDIO_HDR) $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/asan
	$(HOST_CC) $(WARN) $(SAN) -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_audio.c $(AUDIO_SRC) $(CORE_SRC) $(HOST_LIBS) -lm

# the kit's own build (uikit/Makefile): its unit tests test_kit, test_sound and test_probe, built from the kit alone
# in both builds (uikit/build/host/, uikit/build/asan/), its art made alone (art.py without --app, every look held,
# and looks_built.h; KIT_ART), and its make test: those, and its Python tests (the kit's halves of test_kit_bounds.py
# and test_art_alone.py, test_art_part.py, test_example.py, test_kit_docs.py); the application's tests of the same
# code with its texts and shapes stay in test_app and test_audio
KIT_ART := uikit/build/art
.PHONY: kit-host kit-asan kit-art kit-test
host: kit-host
asan: kit-asan
kit-host kit-asan kit-art kit-test:
	$(MAKE) --no-print-directory -C uikit $(@:kit-%=%)

build/asan/%: host/%.c $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/asan
	$(HOST_CC) $(WARN) $(SAN) -Icore -o $@ $< $(CORE_SRC) $(HOST_LIBS)

test: host asan
	build/host/test_audio
	build/asan/test_audio
	python3 -I host/test_host.py --build build/host
	python3 -I host/test_host.py --build build/asan

# badges: test_badge in both builds; make test also compares the badge core with tools/badges (host/test_badges.py)
host: build/host/test_badge
asan: build/asan/test_badge
test: test-badges
.PHONY: test-badges
test-badges: host asan
	python3 -I host/test_badges.py --build build/host
	python3 -I host/test_badges.py --build build/asan

# badges (phase 3b): test_badgeflow runs the Badges part's writes against a fake of HOME's two files, in both builds
host: build/host/test_badgeflow
asan: build/asan/test_badgeflow
test: test-badgeflow
.PHONY: test-badgeflow
BFLOW_DEP := host/test_badgeflow.c host/test_badgeflow_more.h host/test_badge.c $(APP_SRC) $(APP_HDR) $(CORE_HDR)

build/host/test_badgeflow: $(BFLOW_DEP) build/host/libtpcore.a
	$(HOST_CC) $(WARN) -O2 -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_badgeflow.c $(APP_SRC) build/host/libtpcore.a \
		$(HOST_LIBS) -lcurl

build/asan/test_badgeflow: $(BFLOW_DEP) $(CORE_SRC)
	@mkdir -p build/asan
	$(HOST_CC) $(WARN) $(SAN) -Icore -Iapp/source -Iapp/ui -Iuikit/include -o $@ host/test_badgeflow.c $(APP_SRC) $(CORE_SRC) \
		$(HOST_LIBS) -lcurl

# test_main_badges: the Badges part's screens in main.c, with test_main.c's simulated keys, touch screen and clock
host: build/host/test_main_badges
asan: build/asan/test_main_badges
build/host/test_main_badges: host/test_main_badges.c host/badges_shim/zlib.h $(MAIN_DEP) $(APP_SRC) $(APP_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -Ihost/badges_shim $(MAIN_FLAGS) -O2 -o $@ host/test_main_badges.c \
		$(filter-out host/test_main.c,$(MAIN_SRC)) $(APP_SRC) build/host/libtpcore.a $(HOST_LIBS) -lcurl

build/asan/test_main_badges: host/test_main_badges.c host/badges_shim/zlib.h $(MAIN_DEP) $(APP_SRC) $(APP_HDR) $(CORE_SRC) $(CORE_HDR)
	@mkdir -p build/asan
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -Ihost/badges_shim $(MAIN_FLAGS) $(SAN) -o $@ host/test_main_badges.c \
		$(filter-out host/test_main.c,$(MAIN_SRC)) $(APP_SRC) $(CORE_SRC) $(HOST_LIBS) -lcurl

test-badgeflow: host asan
	build/host/test_main_badges
	build/asan/test_main_badges
	build/host/test_badgeflow unit
	build/asan/test_badgeflow unit
	python3 -I host/test_badgeflow.py --build build/host
	python3 -I host/test_badgeflow.py --build build/asan

# test_ui: uikit/source/ui.c and app/ui/app_ui.c themselves with citro2d as a recorder (uikit/host/gpu/), Stock's images
# (R7: the order of pictures and texts on tiles and buttons)
UI_SRC := uikit/source/hits.c uikit/source/wrap.c uikit/source/popup.c app/source/progress.c app/ui/pour.c $(LOOKS_C)
host: build/host/test_ui
test: test-ui
.PHONY: test-ui
build/host/test_ui: host/test_ui.c $(wildcard host/expect*.h) uikit/host/gpu/3ds.h uikit/host/gpu/citro2d.h uikit/tools/flows/fake/3ds.h $(ART_C) build/host/art/looks_built.h uikit/source/ui.c uikit/include/ui.h uikit/include/kit.h uikit/include/look.h uikit/include/widget.h app/ui/app_ui.c app/ui/app_ui.h $(APP_HDR) $(UI_SRC)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -std=gnu11 -Iuikit/host/gpu -Ibuild/host/art -Icore -Iapp/source -Iapp/ui -Iuikit/include -Iuikit/source -DAPP_NAME='"3DStockTheme"' -O1 -o $@ \
		host/test_ui.c $(ART_C) $(UI_SRC) $(HOST_LIBS) -lm
test-ui: host
	build/host/test_ui

# test_texts_fit (review of the join): every failure text in the information box and every note text, German and
# English, in each look, stands whole (ui.c with test_ui.c's recorder and the application's texts)
host: build/host/test_texts_fit
test: test-texts-fit
.PHONY: test-texts-fit
TEXTS_SRC := app/source/text.c app/source/badge_text.c
build/host/test_texts_fit: host/test_texts_fit.c host/test_ui.c $(wildcard host/expect*.h) uikit/host/gpu/3ds.h uikit/host/gpu/citro2d.h uikit/tools/flows/fake/3ds.h $(ART_C) build/host/art/looks_built.h uikit/source/ui.c uikit/include/ui.h uikit/include/kit.h uikit/include/look.h uikit/include/widget.h app/ui/app_ui.c app/ui/app_ui.h $(APP_HDR) $(UI_SRC) $(TEXTS_SRC)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -std=gnu11 -Ihost -Iuikit/host/gpu -Ibuild/host/art -Icore -Iapp/source -Iapp/ui -Iuikit/include -Iuikit/source -DAPP_NAME='"3DStockTheme"' -O1 -o $@ \
		host/test_texts_fit.c $(ART_C) $(UI_SRC) $(TEXTS_SRC) $(HOST_LIBS) -lm
test-texts-fit: host
	build/host/test_texts_fit

# the kit's make test (uikit/Makefile): test_kit, test_sound uikit/sound and test_probe in their builds, and the kit's
# Python tests
test: kit-test

# the Stock design in main.c (host/test_design.c): no glass row, the Shard slot, the settings rows' heights, R10, R12
host: build/host/test_design
test: test-design
.PHONY: test-design
DESIGN_SRC := $(filter-out host/test_main.c $(ART_C),$(MAIN_SRC))

build/host/test_design: host/test_design.c host/badges_shim/zlib.h $(MAIN_DEP) $(APP_SRC) $(APP_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -Ihost/badges_shim $(MAIN_FLAGS) -O2 -o $@ host/test_design.c $(DESIGN_SRC) \
		$(ART_C) $(APP_SRC) build/host/libtpcore.a $(HOST_LIBS) -lcurl

test-design: host
	build/host/test_design

# G1, the boundary between the drawing layer and the looks (host/test_look.c, with host/test_ui.c's recorder and the
# test-only look host/look_fake.c): every look held fills its table and its art inflates; a number not held draws
# Stock; a look told is drawn from the next frame's start, its images unpacked anew; the popup's tone
host: build/host/test_look
test: test-look
.PHONY: test-look
build/host/test_look: host/test_look.c host/look_fake.c host/test_ui.c $(wildcard host/expect*.h) uikit/host/gpu/3ds.h uikit/host/gpu/citro2d.h uikit/tools/flows/fake/3ds.h $(ART_C) build/host/art/looks_built.h uikit/source/ui.c uikit/include/ui.h uikit/include/kit.h uikit/include/look.h uikit/include/widget.h app/ui/app_ui.c app/ui/app_ui.h $(APP_HDR) $(UI_SRC)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -std=gnu11 -Iuikit/host/gpu -Ibuild/host/art -Icore -Iapp/source -Iapp/ui -Iuikit/include -Iuikit/source -DAPP_NAME='"3DStockTheme"' -O1 -o $@ \
		host/test_look.c host/look_fake.c $(ART_C) $(UI_SRC) $(HOST_LIBS) -lm
test-look: host
	build/host/test_look

# G1, the settings row "Stil" in a build of two looks (host/test_look_rows.c): main.c with a looks_built.h of Stock
# and a second look under the number of Prisma, which main.c needs only the number of
host: build/host/test_look_rows
test: test-look-rows
.PHONY: test-look-rows
build/host/two/looks_built.h:
	@mkdir -p build/host/two
	printf '/* host tests only: Stock and a second look (host/test_look_rows.c) */\n#define LOOKS_HELD 2\n#define LOOKS_BUILT(X) X(stock, STOCK) X(fake, PRISM)\n' > $@

build/host/test_look_rows: host/test_look_rows.c build/host/two/looks_built.h host/badges_shim/zlib.h $(MAIN_DEP) $(APP_SRC) $(APP_HDR) build/host/libtpcore.a $(CORE_HDR)
	$(HOST_CC) $(filter-out -std=c11,$(WARN)) -Ihost/badges_shim -Ibuild/host/two $(MAIN_FLAGS) -O2 -o $@ host/test_look_rows.c \
		$(DESIGN_SRC) $(ART_C) $(APP_SRC) build/host/libtpcore.a $(HOST_LIBS) -lcurl

test-look-rows: host
	build/host/test_look_rows

# R9: the installing screen's picture (app/ui/pour.c) against kinds of work, in both builds
host: build/host/test_pour
asan: build/asan/test_pour
test: test-pour
.PHONY: test-pour
build/host/test_pour: host/test_pour.c app/ui/pour.c app/ui/pour.h uikit/include/ease.h
	@mkdir -p build/host
	$(HOST_CC) $(WARN) -O2 -Iapp/ui -Iuikit/include -o $@ host/test_pour.c app/ui/pour.c -lm
build/asan/test_pour: host/test_pour.c app/ui/pour.c app/ui/pour.h uikit/include/ease.h
	@mkdir -p build/asan
	$(HOST_CC) $(WARN) $(SAN) -Iapp/ui -Iuikit/include -o $@ host/test_pour.c app/ui/pour.c -lm
test-pour: host asan
	build/host/test_pour
	build/asan/test_pour

# the colour schemes' table (uikit/art/schemes.py): per scheme a loader tone and a pressed tone of its own
test: test-schemes
.PHONY: test-schemes
test-schemes:
	python3 -I host/test_schemes.py

# each look's own art tests (host/test_look_<name>.py, there with the look's port; G1 B4, Nachtglas R3)
test: test-look-py
.PHONY: test-look-py
test-look-py:
	@for t in $(wildcard host/test_look_*.py); do echo "python3 -I $$t"; python3 -I $$t || exit 1; done

# the Stock design's start screen word: both "e"s of "Theme" read open in every scheme (R3)
test: test-word
.PHONY: test-word
test-word:
	python3 -I host/test_word.py

# the Stock design's frame round a design's picture: its line flush on the picture, no light gap (R19)
test: test-frame
.PHONY: test-frame
test-frame:
	python3 -I host/test_frame.py

# every sprite of every look built (Stock, Frost, Nachtglas, Prisma) lies inside its texture (SR-TD6: POUR_MELT once passed the atlas's right edge)
test: test-atlas
.PHONY: test-atlas
test-atlas: host
	python3 -I host/test_atlas.py build/host/art

# the application's halves of the kit's split tests (the kit's run in its make test, on its own art KIT_ART): no
# APP_PROBE, no installing screen's cut in the kit; the kit's art the same bytes as the host build's
test: test-kit-bounds test-art-alone
.PHONY: test-kit-bounds test-art-alone
test-kit-bounds: host
	python3 -I host/test_kit_bounds.py
test-art-alone: host kit-art
	python3 -I host/test_art_alone.py build/host/art $(KIT_ART)

# the image generator's two halves: the kit's names nothing of the application's but what its hook offers
test: test-art-bounds
.PHONY: test-art-bounds
test-art-bounds: host
	python3 -I host/test_art_bounds.py build/host/art

# the kit's example and the pictures of its docs, by the kit's Makefile: make example in the devkitPro image (its
# .3dsx in uikit/example/build/), make docs-pictures on the Mac (uikit/docs/img/, committed; not part of make test,
# which checks the files only)
.PHONY: example docs-pictures
example docs-pictures:
	$(MAKE) --no-print-directory -C uikit $@

# the CIA's start logo, the finished signed file app/meta/logo.bin: its size and sha256 as app/meta/logo.txt states,
# checklogo.py accepts it, its DARC is makelogo.py's "word", one byte changed is refused (make cia checks the CIA)
test: test-logo
.PHONY: test-logo
test-logo:
	python3 -I app/meta/logo/test_logo.py

# the CIA's banner model, the finished file app/meta/banner.cgfx: its size and sha256 as app/meta/banner.txt states,
# one byte changed is refused (make cia checks the CIA)
test: test-banner
.PHONY: test-banner
test-banner:
	python3 -I app/meta/test_banner.py

# the HOME Menu's icon that app/meta/icon.py draws: both pictures' pixels as approved (their sha256 in
# app/meta/test_icon.py), one byte changed is refused
test: test-icon
.PHONY: test-icon
test-icon:
	python3 -I app/meta/test_icon.py

# devkitARM's compiler with the 3DS code generation flags of libctru's examples; zlib.h from the 3ds-zlib
# portlib. No libctru include path: the core must not need one.
ARM_CC   = $(DEVKITARM)/bin/arm-none-eabi-gcc
ARM_AR   = $(DEVKITARM)/bin/arm-none-eabi-ar
ARM_ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

core3ds: $(CORE_SRC) $(CORE_HDR)
	@test -n "$(DEVKITARM)" || { echo "DEVKITARM is not set: run in the devkitPro image (README)"; exit 1; }
	@mkdir -p build/3ds
	cd build/3ds && $(ARM_CC) $(WARN) -O2 $(ARM_ARCH) -ffunction-sections -I$(DEVKITPRO)/portlibs/3ds/include \
		-c $(addprefix ../../,$(CORE_SRC))
	$(ARM_AR) rcs build/3ds/libtpcore.a $(addprefix build/3ds/,$(notdir $(CORE_SRC:.c=.o)))

include app/identity.mk

app:
	$(MAKE) -C app

# makerom runs on this machine (the macOS release in the data dir, README); the ELF, the SMDH and the banner come
# from make app and make -C app banner in the devkitPro image. The title version stays 0: makerom 0.19.0 ignored
# -ver and -minor in a test (only -major counted), and FBI replaces an installed copy whatever its version.
DATA_BIN = $(or $(THEME_PACK_BIN),$(if $(THEME_PACK_DATA),$(THEME_PACK_DATA)/bin))
MAKEROM ?= $(DATA_BIN)/makerom
CTRTOOL ?= $(dir $(MAKEROM))ctrtool
APP_OUT := app/build/$(APP_TITLE)

# The start logo is the finished signed file app/meta/logo.bin (app/meta/logo.txt): makerom takes a -logo file
# before the rsf's Logo name and, with -exefslogo, puts it into the ExeFS as "logo". The CIA keeps its name only
# once ctrtool has read that logo back equal to the file (app/meta/logo/test_logo.py) and its banner's model back
# equal to app/meta/banner.cgfx (app/meta/test_banner.py); an older CIA goes first, so a run that fails leaves none
# under that name.
cia: $(APP_OUT).elf $(APP_OUT).icn app/build/banner.bnr app/meta/app.rsf app/identity.mk app/meta/logo.bin \
     app/meta/logo.txt app/meta/logo/test_logo.py app/meta/banner.cgfx app/meta/banner.txt app/meta/test_banner.py
	rm -f $(APP_OUT).cia $(APP_OUT).cia.new
	@test -x "$(MAKEROM)" || { echo "makerom not found: set MAKEROM, THEME_PACK_BIN or THEME_PACK_DATA (README)"; exit 1; }
	@test -x "$(CTRTOOL)" || { echo "ctrtool not found: set CTRTOOL or put it beside makerom (README)"; exit 1; }
	"$(MAKEROM)" -f cia -o $(APP_OUT).cia.new -target t -exefslogo -logo app/meta/logo.bin -elf $(APP_OUT).elf \
		-rsf app/meta/app.rsf -icon $(APP_OUT).icn -banner app/build/banner.bnr -DAPP_TITLE="$(APP_TITLE)" \
		-DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(APP_UNIQUE_ID)"
	python3 -I app/meta/logo/test_logo.py --cia $(APP_OUT).cia.new --ctrtool "$(CTRTOOL)"
	python3 -I app/meta/test_banner.py --cia $(APP_OUT).cia.new --ctrtool "$(CTRTOOL)"
	mv $(APP_OUT).cia.new $(APP_OUT).cia

clean:
	rm -rf build app/build
	$(MAKE) --no-print-directory -C uikit clean
