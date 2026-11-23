#!/bin/bash
# record.sh OUT: the records of everything the application in this tree draws, into OUT (a new folder), for
# compare_records.py: the draw lists of both walks (designs.c, badges.c) and of the sheet (sheet.c: every drawing
# function with fixed arguments) in every look the tree builds, Stock in each colour scheme and every other look in
# the first scheme and the last (Weiss and Nacht); the sha256 of every generated art file for every set of looks
# the Makefile can build (Stock with any of the others); the summary lines of make test. Layout:
#   OUT/lists/<look>-s<scheme>/{designs,badges,sheet}/*.dl   every call with its numbers, colours, sprite and text
#   OUT/named/<look>-s<scheme>/{designs,badges,sheet}/*.dl   the same lists in the second form, free of the atlas's
#                    places (rec.c, named_dump), with depth and the textures' creates, deletes and flushes
#   OUT/art.txt      <looks joined by "-"> <file> <bytes> <sha256>
#   OUT/sprites/<looks joined by "-">.txt   every sprite of that join by name with its size and pixels (sprites.c)
# The first form (lists, art.txt, tests.txt) is compare_records.py's, the second (named, sprites, tests.txt)
# compare_named.py's.
#   OUT/tests.txt    each test's line of checks and failed (make test, THEME_PACK_DATA from the environment or
#                    from ENV_FILE, a file of VAR=value lines sourced for that step only; values never printed)
#   OUT/source.txt, OUT/summary.txt   where the records come from and how the run went (not compared)
# COVERAGE=1: the drivers are built with clang's coverage, and OUT/coverage.txt lists per file the functions and
# lines no walk executes, and those that stay unexecuted with the sheet (coverage.py). The scratch goes into
# OUT.work, also new. OUT as render.sh's. Exit 0 only if every step ran and every test reported 0 failed.
set -u
[ $# -eq 1 ] && [ -n "$1" ] || { echo "usage: tools/flows/record.sh OUT" >&2; exit 2; }
OUT=$1
while [ "$OUT" != / ] && [ "${OUT%/}" != "$OUT" ]; do OUT=${OUT%/}; done
case "$OUT" in /*) ABS=$OUT ;; *) ABS=$PWD/$OUT ;; esac
# the same rule as render.sh's: test_badgeflow's fixture command holds the path in 300 bytes
case "$ABS" in
  / | *[!A-Za-z0-9._/-]*) echo "record.sh: OUT may hold only letters, digits, '.', '_', '-' and '/': $ABS" >&2; exit 2 ;;
esac
[ ${#ABS} -le 200 ] || { echo "record.sh: OUT is longer than 200 characters as an absolute path" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(git -C "$HERE" rev-parse --show-toplevel) || exit 2
TREE=$ROOT
[ -f "$TREE/uikit/Makefile" ] || { echo "record.sh: the kit's submodule uikit is not checked out: git -c protocol.file.allow=always submodule update --init" >&2; exit 2; }
OUT=$ABS; W=$OUT.work
for d in "$OUT" "$W"; do
  [ -e "$d" ] && { echo "$d exists: give a new folder" >&2; exit 2; }
done
mkdir -p "$OUT/lists" "$OUT/sprites" "$W/tmp" "$W/log" "$W/prof"
OUT=$(cd "$OUT" && pwd); W=$(cd "$W" && pwd)
fail=0
say() { echo "$*" | tee -a "$OUT/summary.txt"; }

REV=$(git -C "$ROOT" rev-parse --short HEAD)
DIRTY=$(git -C "$ROOT" diff --quiet --ignore-submodules=untracked HEAD -- app core host uikit Makefile || echo ' with changes in the working tree')
echo "3DStockTheme at $REV$DIRTY" > "$OUT/source.txt"
say "== record.sh $REV$DIRTY ($(date '+%H:%M:%S'))"
make -C "$TREE" build/host/libtpcore.a build/host/art/ui_art.h build/host/test_badgeflow > "$W/host.log" 2>&1
rc=$?; say "host library, art, test_badgeflow: exit $rc"
[ $rc -eq 0 ] || exit 1
for walk in designs badges sheet; do
  sh "$HERE/build.sh" "$TREE" "$W/$walk-drv" "$HERE/$walk.c"
  rc=$?; say "driver $walk: exit $rc warning lines $(grep -c 'warning:' "$W/$walk-drv/driver-build.log")"
  [ $rc -eq 0 ] || exit 1
done
python3 -I -B "$HERE/make_zips.py" "$ROOT/tools/theme-pack" "$W/zips" || exit 1
"$TREE/build/host/test_badgeflow" fixture "$W/fixture" > "$W/fixture.log" 2>&1 || { say "badge fixture failed"; exit 1; }

# the looks in LOOKS[]'s order (main.c; REC_LOOK is the index), the schemes 0 to SETTINGS_SCHEMES - 1
LOOKS=$(sed -n 's/.*LOOKS_BUILT(X)//p' "$TREE/build/host/art/looks_built.h" | grep -o 'X([a-z]*' | cut -c3-)
SCHEMES=$(sed -n 's/^#define SETTINGS_SCHEMES \([0-9]*\).*/\1/p' "$TREE/app/source/settings.h")
[ -n "$LOOKS" ] && [ -n "$SCHEMES" ] || { say "no looks or no schemes found"; exit 1; }
Z=$W/zips
export REC_HASH=1
# one walk: its draw lists into OUT/lists/<look>-s<scheme>/<walk>, its own scratch folder removed after it
walk() {
  local name=$1 look=$2 i=$3 s=$4 d=$OUT/lists/$2-s$4/$1 t=$W/tmp/$2-s$4-$1 nd=$OUT/named/$2-s$4/$1 rc
  mkdir -p "$d" "$t" "$nd"
  case $name in
    designs | sheet) set -- "$Z/a.zip" "$Z/b.zip" "$Z/c.zip" "$Z/d.zip" "$Z/e.zip" "$s" 1 1 r ;;
    badges) set -- "$W/fixture" "$s" 1 r ;;
  esac
  (cd "$W/$name-drv" && TMPDIR=$t REC_LOOK=$i REC_NAMED=$nd LLVM_PROFILE_FILE="$W/prof/$name-$look-s$s.profraw" \
    perl -e 'alarm 150; exec @ARGV' ./driver "$d" "$@" < /dev/null > "$W/log/$look-s$s-$name.log" 2>&1)
  rc=$?
  say "$look s$s $name: exit $rc lists $(find "$d" -name '*.dl' | wc -l | tr -d ' ') named $(find "$nd" -name '*.dl' | wc -l | tr -d ' ')"
  [ $rc -eq 0 ] || fail=1
  find "$t" -delete
}
i=0
for look in $LOOKS; do
  if [ "$look" = stock ]; then list=$(seq 0 $((SCHEMES - 1))); else list="0 $((SCHEMES - 1))"; fi
  for s in $list; do
    for name in designs badges sheet; do walk $name "$look" $i "$s"; done
  done
  i=$((i + 1))
done
say "draw lists: $(find "$OUT/lists" -name '*.dl' | wc -l | tr -d ' ') ($(date '+%H:%M:%S'))"

# the art: each look's part as the host build drew it, joined for Stock with every subset of the other looks
ART=$TREE/build/host/art
OTHERS=$(echo "$LOOKS" | grep -v '^stock$' | tr '\n' ' ')
n=$(echo $OTHERS | wc -w | tr -d ' ')
for ((m = 0; m < 1 << n; m++)); do
  set=stock k=0
  for o in $OTHERS; do [ $((m >> k & 1)) -eq 1 ] && set="$set $o"; k=$((k + 1)); done
  key=$(echo $set | tr ' ' -); A=$W/art/$key
  mkdir -p "$A"
  for l in $set; do cp "$ART/$l.part" "$A/"; done
  # shellcheck disable=SC2086
  (cd "$TREE" && python3 -I uikit/art/art.py --app app/ui/app_art.py --join "$A" $set) > "$W/log/art-$key.log" 2>&1 || { say "art $key: failed"; fail=1; }
  for f in ui_art.h ui_art.c $(for l in $set; do echo art_$l.c; done) part_art.h part_art.c \
      $(for l in $set; do echo part_art_$l.c; done) banner.wav; do
    if [ -f "$A/$f" ]; then
      echo "$key $f $(stat -f %z "$A/$f") $(shasum -a 256 "$A/$f" | cut -d' ' -f1)"
    else
      echo "$key $f missing"; fail=1
    fi
  done >> "$OUT/art.txt"
  # the set's sprite list (sprites.c), built against the join
  S=$W/sprites/$key
  mkdir -p "$S"
  # shellcheck disable=SC2046
  python3 -I -B "$TREE/uikit/tools/flows/art_names.py" "$A/ui_art.h" "$S/art_names.h" > "$W/log/sprites-$key.log" 2>&1 &&
    cc -std=gnu11 -O0 -Wall -Wextra -I"$S" -I"$A" -I"$TREE/uikit/include" -I"$TREE/app/ui" -o "$S/sprites" "$HERE/sprites.c" \
      "$A/ui_art.c" $(for l in $set; do echo "$A/art_$l.c" "$A/part_art_$l.c"; done) "$A/part_art.c" -lz \
      >> "$W/log/sprites-$key.log" 2>&1 &&
    "$S/sprites" > "$OUT/sprites/$key.txt" 2>> "$W/log/sprites-$key.log" || { say "sprites $key: failed"; fail=1; }
  grep -q 'warning:' "$W/log/sprites-$key.log" && { say "sprites $key: warnings"; fail=1; }
done
say "sprites: $(find "$OUT/sprites" -name '*.txt' | wc -l | tr -d ' ') sets, $(cat "$OUT"/sprites/*.txt | wc -l | tr -d ' ') lines"
# the host's own art (all looks) must be the join of all of them
for f in ui_art.h ui_art.c part_art.h part_art.c banner.wav $(for l in $LOOKS; do echo art_$l.c part_art_$l.c; done); do
  cmp -s "$ART/$f" "$A/$f" || { say "art: the host build's $f is not the join of every look"; fail=1; }
done
say "art: $(wc -l < "$OUT/art.txt" | tr -d ' ') files in $((1 << n)) sets"

( if [ -n "${ENV_FILE:-}" ]; then set -a; . "$ENV_FILE"; set +a; fi
  [ -n "${THEME_PACK_DATA:-}" ] || echo "THEME_PACK_DATA for make test: NOT SET"
  make -C "$TREE" test > "$W/test.log" 2>&1; echo "make test: exit $?" ) | tee -a "$OUT/summary.txt" | grep -q 'exit 0$' || fail=1
# per test command make ran: its lines "ok ..." counted, and its summary lines (without the time a run took)
awk 'function put() { if (cmd != "" && (n || sum != "")) print cmd " | ok lines " n sum }
     /^(build\/|python3 )/ { put(); cmd = $0; n = 0; sum = ""; next }
     /^ok / { n++ }
     /[0-9]+ checks|[0-9]+ failed|^Ran [0-9]+ tests|^OK|^FAILED/ { sub(/ in [0-9.]+s$/, ""); sum = sum " | " $0 }
     END { put() }' "$W/test.log" > "$OUT/tests.txt"
grep -E '[1-9][0-9]* failed' "$OUT/tests.txt" && fail=1
say "tests: $(wc -l < "$OUT/tests.txt" | tr -d ' ') summary lines"

if [ -n "${COVERAGE:-}" ]; then
  python3 -I -B "$HERE/coverage.py" "$TREE" "$W" > "$OUT/coverage.txt" 2> "$W/log/coverage.log" || fail=1
  say "coverage: $(grep -c . "$OUT/coverage.txt") lines in $OUT/coverage.txt"
fi
say "== record.sh end ($(date '+%H:%M:%S')): $([ $fail -eq 0 ] && echo passed || echo FAILED)"
exit $fail
