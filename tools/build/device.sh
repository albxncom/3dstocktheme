#!/bin/bash
# device.sh REV OUT "<make options>|<launcher name>|<file name>|<build folder under app/>" ...: checks.sh on REV into
# OUT (summary in OUT/summary.txt), then the device builds from a third clean export (OUT/d, made as checks.sh's), one
# per argument, in the container (IMG, common.sh): make app <make options> LISTED=<launcher name>; each .3dsx is
# staged as OUT/stage/<file name> with its size, sha256, the SMDH's title and the count of "demo" and "probe" strings
# beside the plain build's. cia.sh makes the CIA from the same export afterwards. Needs THEME_PACK_BIN; see checks.sh.
# Example: device.sh HEAD ../out-g14 "|3DStockTheme G14|3DStockTheme-G14.3dsx|build" \
#            "PROBE=1|3DStockTheme G14 Probe|3DStockTheme-G14-Probe.3dsx|build/probe"
[ $# -ge 3 ] && [ -n "$1" ] && [ -n "$2" ] || { echo "usage: THEME_PACK_BIN=<folder> tools/build/device.sh REV OUT SPEC..." >&2; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/common.sh"
need_bin
REV=$1; OUT=$2; shift 2
[ -e "$OUT" ] && { echo "$OUT exists: give a new folder" >&2; exit 2; }
mkdir -p "$OUT/stage" "$OUT/d"
OUT=$(cd "$OUT" && pwd); STAGE=$OUT/stage
"$HERE/checks.sh" "$REV" "$OUT" > "$OUT/summary.txt" 2>&1
echo "checks.sh exit $?" >> "$OUT/summary.txt"
export_tree "$REV" "$OUT/d" || exit 1
. "$OUT/d/uikit/tools/build/image.sh"   # the image's name as the pinned kit has it
cd "$OUT/d" || exit 1
PLAIN=$OUT/a/app/build/3DStockTheme.3dsx
n=0
for spec in "$@"; do
  n=$((n+1)); IFS='|' read -r OPTS NAME FILE DIR <<< "$spec"
  # shellcheck disable=SC2086
  docker run --rm --network none --platform linux/amd64 -v "$PWD":/src -w /src "$IMG" make app $OPTS "LISTED=$NAME" > "$OUT/device-$n.log" 2>&1
  rc=$?
  B=app/$DIR/3DStockTheme
  {
    echo "device build $n: make app $OPTS LISTED=$NAME: exit $rc warning lines $(grep -c 'warning:' "$OUT/device-$n.log")"
    if [ -f "$B.3dsx" ]; then
      cp "$B.3dsx" "$STAGE/$FILE"
      echo "  $FILE $(stat -f %z "$STAGE/$FILE") $(shasum -a 256 "$STAGE/$FILE" | cut -d' ' -f1)"
      echo "  smdh title: $(python3 -I -c 'import sys;d=open(sys.argv[1],"rb").read();print(d[:4].decode(),"|",d[8:8+0x80].decode("utf-16-le").rstrip("\0"))' "$B.smdh")"
      echo "  strings demo|probe: design $(strings "$B.3dsx" | grep -c -i -E 'demo|probe') plain $(strings "$PLAIN" | grep -c -i -E 'demo|probe')"
    else
      echo "  no $B.3dsx"
    fi
  } >> "$OUT/summary.txt"
done
echo "== device.sh end ($(date '+%H:%M'))" >> "$OUT/summary.txt"
