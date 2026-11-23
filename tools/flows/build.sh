#!/bin/sh
# build.sh TREE OUT EPILOGUE: the flow renderer's driver for one walk (EPILOGUE: designs.c, badges.c or sheet.c) from
# TREE, a tree of this repository whose build/host/libtpcore.a and build/host/art are made, into OUT/driver; the
# compiler's output in OUT/driver-build.log. The application's sources, its looks and its generated art are TREE's own;
# the name and version come from TREE's app/identity.mk. COVERAGE=1: built with clang's source-based coverage (each run
# writes LLVM_PROFILE_FILE; record.sh reads it). The recorder (rec.c, fake/) and its generators are the kit's, TREE's
# uikit/tools/flows/.
set -eu
TREE=$(cd "$1" && pwd); OUT=$2; EPI=$3; HERE=$(cd "$(dirname "$0")" && pwd)
KF=$TREE/uikit/tools/flows
mkdir -p "$OUT/src"
python3 -I -B "$HERE/gen_driver.py" "$TREE" "$OUT/src" "$EPI"
python3 -I -B "$KF/fontgen.py" "$TREE/uikit/include/ui_layout.h" "$OUT/src/font_adv.h"
python3 -I -B "$KF/art_names.py" "$TREE/build/host/art/ui_art.h" "$OUT/src/art_names.h"
id() { sed -n "s/^$1 *:= *//p" "$TREE/app/identity.mk"; }
ART=$TREE/build/host/art
APP_SRC="app/source/files.c app/source/store.c app/source/plan.c app/source/install.c app/source/deletion.c
 app/source/designs.c app/source/scan.c app/source/text.c app/source/download.c uikit/source/wrap.c
 uikit/source/scroll.c uikit/source/touch.c uikit/source/popup.c app/source/progress.c uikit/source/wav.c
 uikit/source/bcstm.c app/source/settings.c uikit/source/tracks.c uikit/source/work.c app/source/preview.c
 app/source/badge_flow.c app/source/badge_text.c app/source/badge_download.c app/source/badge_dir_store.c
 host/badge_stubs.c app/ui/pour.c"
LOOK_C=$(cd "$TREE" && ls uikit/source/look*.c)
ART_C="$(ls "$ART"/art_*.c) $(ls "$ART"/part_art_*.c)"
COV=
[ -z "${COVERAGE:-}" ] || COV="-fprofile-instr-generate -fcoverage-mapping"
# shellcheck disable=SC2086
( cd "$TREE" && cc -std=gnu11 -Wall -Wextra -Wshadow -Wvla -O1 $COV -I"$KF/fake" -I"$OUT/src" -I"$ART" -Icore \
    -Iapp/source -Iapp/ui -Iuikit/include -DAPP_NAME="\"$(id APP_TITLE)\"" -DAPP_VERSION_MAJOR="$(id APP_VERSION_MAJOR)" \
    -DAPP_VERSION_MINOR="$(id APP_VERSION_MINOR)" -DAPP_VERSION_MICRO="$(id APP_VERSION_MICRO)" \
    -DAPP_VERSION_PRE="\"$(id APP_VERSION_PRE)\"" \
    -DAPP_DEMO -o "$OUT/driver" "$OUT/src/driver.c" "$OUT/src/stubs.c" "$KF/rec.c" "$HERE/names.c" uikit/source/ui.c uikit/source/hits.c \
    app/ui/app_ui.c "$ART/ui_art.c" $ART_C "$ART/part_art.c" $LOOK_C $APP_SRC build/host/libtpcore.a -lz -lcurl -lm ) > "$OUT/driver-build.log" 2>&1
