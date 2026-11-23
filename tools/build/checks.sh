#!/bin/bash
# checks.sh REV OUT: the acceptance checks on two clean exports (git archive, with the kit's pinned commit at its
# path: common.sh's export_tree) of one commit of this repository, OUT/a and OUT/b; the logs go into OUT, the summary
# is printed. In the container (IMG, common.sh): make app, with DEMO=1 and with PROBE=1 in OUT/a, make app again in
# OUT/b (two clean plain builds must be identical), the banner; on the Mac: the CIA, make -B host, make test and the
# theme-pack tests, and hygiene.sh (the kit's) before and after. Needs THEME_PACK_BIN (makerom, ctrtool, bannertool).
# make test and the theme-pack tests get THEME_PACK_DATA from the environment or from ENV_FILE (a file of VAR=value
# lines, sourced for those two steps only; values are never printed). HYGIENE_WORDS goes on to hygiene.sh.
[ $# -eq 2 ] && [ -n "$1" ] && [ -n "$2" ] || { echo "usage: THEME_PACK_BIN=<folder> tools/build/checks.sh REV OUT" >&2; exit 2; }
. "$(dirname "$0")/common.sh"
need_bin
REV=$1; OUT=$2
for x in a b; do
  [ -e "$OUT/$x" ] && { echo "$OUT/$x exists: give a new folder" >&2; exit 2; }
done
HEAD=$(git -C "$ROOT" rev-parse --short "$REV") || exit 1
echo "== commit $HEAD ($(date '+%H:%M'))"
pins "$REV" | while read -r sha path; do echo "== $path at the pinned commit ${sha:0:7}"; done
for x in a b; do mkdir -p "$OUT/$x"; export_tree "$REV" "$OUT/$x" || exit 1; done
OUT=$(cd "$OUT" && pwd)
A=$OUT/a; B=$OUT/b; L=$OUT
# the image's name and hygiene.sh from the export: the pinned kit's, whatever the submodule's checkout holds
. "$A/uikit/tools/build/image.sh"
HYG="$A/uikit/tools/build/hygiene.sh"
cd "$A" || exit 1
echo "hygiene before: $("$HYG" "$OUT/a" | tail -1)"
for v in "" "DEMO=1" "PROBE=1"; do
  # shellcheck disable=SC2086
  docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -w /src "$IMG" make app $v > "$L/app${v:+-$v}.log" 2>&1
  echo "make app $v: exit $? warning lines $(grep -c 'warning:' "$L/app${v:+-$v}.log")"
done
(cd "$B" && docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -w /src "$IMG" make app > "$L/app-second.log" 2>&1; echo "second export make app: exit $?")
for f in app/build/3DStockTheme.3dsx app/build/demo/3DStockTheme.3dsx app/build/probe/3DStockTheme.3dsx; do
  echo "$f $(stat -f %z "$f") $(shasum -a 256 "$f" | cut -d' ' -f1)"
done
cmp -s app/build/3DStockTheme.3dsx "$B/app/build/3DStockTheme.3dsx" && echo "two clean plain builds: identical" || echo "two clean plain builds: DIFFER"
echo "strings demo|probe in plain: $(strings app/build/3DStockTheme.3dsx | grep -c -i -E 'demo|probe')"
docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -v "$BIN":/tools:ro -w /src "$IMG" make -C app banner BANNERTOOL=/tools/bannertool > "$L/banner.log" 2>&1; echo "banner: exit $?"
make cia MAKEROM="$BIN/makerom" > "$L/cia.log" 2>&1; echo "cia: exit $?"
echo "app/build/3DStockTheme.cia $(stat -f %z app/build/3DStockTheme.cia) $(shasum -a 256 app/build/3DStockTheme.cia | cut -d' ' -f1)"
"$BIN/ctrtool" app/build/3DStockTheme.cia 2>&1 | grep -i -E "title id|cpu speed|l2 cache|dsp::DSP" | sort -u
make -B host > "$L/host.log" 2>&1; echo "make -B host: exit $? warning lines $(grep -c -i 'warning:' "$L/host.log")"
( if [ -n "${ENV_FILE:-}" ]; then set -a; . "$ENV_FILE"; set +a; fi
  echo "THEME_PACK_DATA for the tests: $([ -n "${THEME_PACK_DATA:-}" ] && echo set || echo NOT SET)"
  make test > "$L/test.log" 2>&1; echo "make test: exit $?"
  python3 -I -m unittest discover -s tools/theme-pack -p "test_*.py" > "$L/pack.log" 2>&1; echo "theme-pack: exit $?" )
grep -E "checks|^[0-9]+ failed|^Ran|^OK|FAIL" "$L/test.log"
tail -3 "$L/pack.log"
echo "hygiene after: $("$HYG" "$OUT/a" | tail -1)"
echo "tracked build output in the commit: $(tree_files "$REV" | grep -v -E '^(uikit/)?tools/build/' | grep -c -E '(^|/)build/|\.3dsx$|\.cia$|\.elf$')"
echo "== end ($(date '+%H:%M'))"
