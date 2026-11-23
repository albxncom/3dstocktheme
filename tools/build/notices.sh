#!/bin/bash
# notices.sh OUT: writes OUT, the third parties' notices that travel with a release of the application
# (docs/README.md, "Dependencies": the libraries in the build ask that their notice goes with copies). No licence
# text is kept for it: the port libraries' texts and versions come out of the container the build links them from
# (IMG, common.sh), quirc's and the icons' out of the tree, so the file says what a build of this commit holds.
[ $# -eq 1 ] && [ -n "$1" ] || { echo "usage: tools/build/notices.sh OUT" >&2; exit 2; }
. "$(dirname "$0")/common.sh"
OUT=$1
rule() { printf '\n%s\n%s\n\n' "------------------------------------------------------------------------" "$1"; }
{
  cat <<'EOF'
3DStockTheme: third-party notices

3DStockTheme's own files are under the zlib licence (LICENSE in its repository). A build of the application also
holds the works below, each under its own terms. Nothing in it is Nintendo's.

Libraries from the devkitPro container, as its package records name them:
EOF
  docker run --rm --network none --platform linux/amd64 "$IMG" sh -c '
    cd /opt/devkitpro || exit 1
    for p in libctru citro2d citro3d 3ds-zlib 3ds-curl 3ds-mbedtls; do
      ls pacman/var/lib/pacman/local | grep "^$p-[0-9]" | sed "s/^/  /" || exit 1
    done
    echo
    echo "libctru, citro2d and citro3d are under the zlib licence (https://github.com/devkitPro); the container holds"
    echo "no licence file for them. The texts of the other three follow as the container holds them."
    for p in 3ds-zlib 3ds-curl 3ds-mbedtls; do
      f=$(ls portlibs/3ds/licenses/$p/* | head -1)
      printf "\n%s\n%s\n\n" "------------------------------------------------------------------------" "$p ($f)"
      cat "$f" || exit 1
    done' || exit 1
  rule "quirc 1.2, the QR code reader (app/vendor/quirc/LICENSE)"
  cat "$ROOT/app/vendor/quirc/LICENSE" || exit 1
  rule "Tabler Icons 3.51.0, the interface's icons (uikit/icons/LICENSE)"
  cat "$ROOT/uikit/icons/LICENSE" || exit 1
  rule "Sounds and music"
  cat <<'EOF'
The interface sounds are from Kenney's packs "Interface Sounds" and "UI Audio", the menu music is "Somewhere in the
Elevator" and "Urban Shop"; all of them were taken under CC0, which asks for no notice. Every source is named in
uikit/sound/CREDITS.md of the repository.
EOF
} > "$OUT" || { echo "notices.sh: failed, $OUT is not complete" >&2; exit 1; }
echo "$OUT $(wc -c < "$OUT" | tr -d ' ') bytes"
