#!/bin/bash
# cia.sh OUT <staged .3dsx name> <cia file name>: the CIA of a device build, made in the very export device.sh built
# its plain .3dsx in (OUT/d; the ELF is not rebuilt, which the script checks by the .3dsx's sha256 before, after and
# as staged): the banner in the container (IMG, common.sh), then make cia with makerom on the Mac. The CIA is
# staged as OUT/stage/<cia file name>; the lines go to OUT/summary.txt. Needs THEME_PACK_BIN.
# Example: cia.sh ../out-g14 3DStockTheme-G14.3dsx 3DStockTheme-G14.cia
[ $# -eq 3 ] && [ -n "$1" ] && [ -n "$2" ] && [ -n "$3" ] || { echo "usage: THEME_PACK_BIN=<folder> tools/build/cia.sh OUT PLAIN.3dsx FILE.cia" >&2; exit 2; }
. "$(dirname "$0")/common.sh"
need_bin
OUT=$(cd "$1" && pwd) || exit 1; PLAINFILE=$2; FILE=$3
STAGE=$OUT/stage
cd "$OUT/d" || exit 1
. uikit/tools/build/image.sh   # the image's name as the export's kit has it (the pinned commit's)
{
  echo "== cia.sh ($(date '+%H:%M'))"
  before=$(shasum -a 256 app/build/3DStockTheme.3dsx | cut -d' ' -f1)
  docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -v "$BIN":/tools:ro -w /src "$IMG" make -C app banner BANNERTOOL=/tools/bannertool > "$OUT/cia-banner.log" 2>&1
  echo "banner: exit $? warning lines $(grep -c 'warning:' "$OUT/cia-banner.log")"
  make cia MAKEROM="$BIN/makerom" > "$OUT/cia-makerom.log" 2>&1
  rc=$?
  echo "cia: exit $rc"
  after=$(shasum -a 256 app/build/3DStockTheme.3dsx | cut -d' ' -f1)
  staged=$(shasum -a 256 "$STAGE/$PLAINFILE" | cut -d' ' -f1)
  echo "the .3dsx in the export before $before after $after staged $staged"
  [ "$before" = "$after" ] && [ "$after" = "$staged" ] && echo "the ELF was not rebuilt: yes" || echo "the ELF was not rebuilt: NO"
  if [ $rc -eq 0 ] && [ -f app/build/3DStockTheme.cia ]; then
    cp app/build/3DStockTheme.cia "$STAGE/$FILE"
    echo "  $FILE $(stat -f %z "$STAGE/$FILE") $(shasum -a 256 "$STAGE/$FILE" | cut -d' ' -f1)"
    "$BIN/ctrtool" "$STAGE/$FILE" 2>&1 | grep -i -E "title id|product code|cpu speed|l2 cache|dsp::DSP" | sort -u
  else
    echo "  nothing staged: make cia failed or left no app/build/3DStockTheme.cia"
  fi
  echo "== cia.sh end ($(date '+%H:%M'))"
} >> "$OUT/summary.txt" 2>&1
