#!/bin/bash
# render.sh OUT: every state of the two flows (Designs and Badges: install, removal, refusals, errors) as pictures
# of the application in the tree this script stands in, drawn on the Mac by the flow renderer, into OUT (a new
# folder): OUT/designs/*.png, OUT/badges/*.png (90 pictures of 800 x 960), OUT/list.json, OUT/list.txt. Then the two
# rule checks over the walks' draw lists (order.py: texts are drawn from the atlas; signs.py: no sign on both
# screens). The scratch (drivers, walks, logs) goes into OUT.work, also new. Stock, colour scheme Weiss, German,
# demonstration build; stand-in content only (theme zips from tools/theme-pack/fixtures.py, badge sets from
# test_badgeflow's fixture). Builds the host library and the art in the tree's own build/host first.
# Exit 0 only if everything ran, there are 90 pictures and both checks pass.
set -u
[ $# -eq 1 ] && [ -n "$1" ] || { echo "usage: tools/flows/render.sh OUT" >&2; exit 2; }
OUT=$1
while [ "$OUT" != / ] && [ "${OUT%/}" != "$OUT" ]; do OUT=${OUT%/}; done
case "$OUT" in /*) ABS=$OUT ;; *) ABS=$PWD/$OUT ;; esac
# test_badgeflow's fixture command removes its folder with a shell command and holds the path in 300 bytes: only
# plain path characters, and at most 200 of them (the fixture's subfolders need the rest)
case "$ABS" in
  / | *[!A-Za-z0-9._/-]*) echo "render.sh: OUT may hold only letters, digits, '.', '_', '-' and '/': $ABS" >&2; exit 2 ;;
esac
[ ${#ABS} -le 200 ] || { echo "render.sh: OUT is longer than 200 characters as an absolute path" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(git -C "$HERE" rev-parse --show-toplevel) || exit 2
TREE=$ROOT
[ -f "$TREE/uikit/Makefile" ] || { echo "render.sh: the kit's submodule uikit is not checked out: git -c protocol.file.allow=always submodule update --init" >&2; exit 2; }
OUT=$ABS; W=$OUT.work
for d in "$OUT" "$W"; do
  [ -e "$d" ] && { echo "$d exists: give a new folder" >&2; exit 2; }
done
mkdir -p "$OUT" "$W/tmp"
OUT=$(cd "$OUT" && pwd); W=$(cd "$W" && pwd)
fail=0
say() { echo "$*" | tee -a "$W/summary.txt"; }

say "== render.sh $(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet --ignore-submodules=untracked HEAD -- app core host uikit Makefile || echo ' with changes in the working tree') ($(date '+%H:%M'))"
make -C "$TREE" build/host/libtpcore.a build/host/art/ui_art.h build/host/test_badgeflow > "$W/host.log" 2>&1
rc=$?; say "host library, art, test_badgeflow: exit $rc"
[ $rc -eq 0 ] || exit 1
for walk in designs badges; do
  sh "$HERE/build.sh" "$TREE" "$W/$walk-drv" "$HERE/$walk.c"
  rc=$?; say "driver $walk: exit $rc warning lines $(grep -c 'warning:' "$W/$walk-drv/driver-build.log")"
  [ $rc -eq 0 ] || exit 1
done
python3 -I -B "$HERE/make_zips.py" "$ROOT/tools/theme-pack" "$W/zips" || exit 1
"$TREE/build/host/test_badgeflow" fixture "$W/fixture" > "$W/fixture.log" 2>&1 || { say "badge fixture failed"; exit 1; }

export TMPDIR=$W/tmp REC_HASH=1 REC_LOOK=0
Z=$W/zips
mkdir -p "$W/wd" "$W/wb"
(cd "$W/designs-drv" && REC_TEX=-dA-,-dB-,-dC-,-dD-,-dX- perl -e 'alarm 150; exec @ARGV' ./driver "$W/wd" \
  "$Z/a.zip" "$Z/b.zip" "$Z/c.zip" "$Z/d.zip" "$Z/e.zip" 0 1 1 weiss < /dev/null > "$W/wd.log" 2>&1)
rc=$?; say "walk designs: exit $rc captures $(grep -c '^captured' "$W/wd.log")"; [ $rc -eq 0 ] || fail=1
(cd "$W/badges-drv" && REC_TEX=-bA-,-bB-,-bC-,-bD-,-bX- perl -e 'alarm 150; exec @ARGV' ./driver "$W/wb" \
  "$W/fixture" 0 1 weiss < /dev/null > "$W/wb.log" 2>&1)
rc=$?; say "walk badges: exit $rc captures $(grep -c '^captured' "$W/wb.log")"; [ $rc -eq 0 ] || fail=1

for walk in wd wb; do
  python3 -I -B "$TREE/uikit/tools/flows/order.py" "$W/$walk" > "$W/$walk.order.txt" 2>&1 || fail=1
  say "order $walk: $(tail -1 "$W/$walk.order.txt")"
  python3 -I -B "$HERE/signs.py" "$W/$walk" "$TREE/build/host/art" stock > "$W/$walk.signs.txt" 2>&1 || fail=1
  say "signs $walk: $(tail -1 "$W/$walk.signs.txt")"
done
python3 -I -B "$TREE/uikit/tools/flows/pics.py" "$W/wd" "$W/pic-d" > /dev/null || fail=1
python3 -I -B "$TREE/uikit/tools/flows/pics.py" "$W/wb" "$W/pic-b" > /dev/null || fail=1
# the textures (several GB over both walks) are needed only for the pictures
[ -n "${KEEP_TEX:-}" ] || find "$W/wd" "$W/wb" -type f -name '*.tex[0-9]*' -delete
SOURCE="3DStockTheme at $(git -C "$ROOT" rev-parse --short HEAD)"
git -C "$ROOT" diff --quiet --ignore-submodules=untracked HEAD -- app core host uikit Makefile || SOURCE="$SOURCE with changes in the working tree"
python3 -I -B "$HERE/make_out.py" "$W/pic-d" "$W/pic-b" "$OUT" "$SOURCE" > /dev/null || fail=1
n=$(find "$OUT" -name '*.png' | wc -l | tr -d ' ')
say "pictures: $n in $OUT"
[ "$n" -eq 90 ] || fail=1
say "== render.sh end ($(date '+%H:%M')): $([ $fail -eq 0 ] && echo passed || echo FAILED)"
exit $fail
