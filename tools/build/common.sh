# Sourced by the build scripts. IMG is the container every 3DS build runs in (docs/README.md,
# "Build"), named once in the kit's tools/build/image.sh: read here from the submodule's checkout, and again from
# their export (the pinned kit's) by checks.sh, device.sh and cia.sh before they build. BIN is the folder that
# holds makerom, ctrtool and bannertool: THEME_PACK_BIN, else THEME_PACK_DATA/bin, as in the Makefile.
BIN=${THEME_PACK_BIN:-${THEME_PACK_DATA:+$THEME_PACK_DATA/bin}}
ROOT=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel) || exit 2
KITHINT="the kit's submodule uikit is not checked out at the pinned commit: git -c protocol.file.allow=always submodule update --init (tools/README.md)"
[ -f "$ROOT/uikit/tools/build/image.sh" ] || { echo "$KITHINT" >&2; exit 2; }
. "$ROOT/uikit/tools/build/image.sh"
[ -n "${IMG:-}" ] || { echo "no IMG in uikit/tools/build/image.sh" >&2; exit 2; }

need_bin() {
  for t in makerom ctrtool bannertool; do
    [ -x "$BIN/$t" ] || { echo "no $t in THEME_PACK_BIN (${BIN:-not set})" >&2; exit 2; }
  done
  BIN=$(cd "$BIN" && pwd)
}

# A commit holds a submodule (the kit, .gitmodules) only as a gitlink, its pinned commit; git archive and
# git ls-tree -r stop there. pins REV prints "<commit> <path>" for each gitlink of REV; each must be in the
# checked-out submodule at its path (an empty folder there is the superproject itself, hence the prefix test).
pins() {
  git -C "$ROOT" ls-tree -r "$1" | awk -F'\t' '$1 ~ /^160000 / { split($1, f, " "); print f[3] " " $2 }'
}

pinned() {
  [ -z "$(git -C "$ROOT/$2" rev-parse --show-prefix 2>/dev/null)" ] && git -C "$ROOT/$2" rev-parse -q --verify "$1^{commit}" > /dev/null \
    || { echo "$2: no commit $1 there: $KITHINT" >&2; return 1; }
}

# export_tree REV DIR: the files of REV into DIR (git archive), each submodule's pinned commit at its path.
export_tree() {
  local links sha path
  links=$(pins "$1") || return 1
  while read -r sha path; do [ -z "$sha" ] || pinned "$sha" "$path" || return 1; done <<< "$links"
  git -C "$ROOT" archive "$1" | tar -x -C "$2" || return 1
  while read -r sha path; do
    [ -n "$sha" ] || continue
    mkdir -p "$2/$path" && git -C "$ROOT/$path" archive "$sha" | tar -x -C "$2/$path" || return 1
  done <<< "$links"
}

# tree_files REV: the path of every file of REV, a submodule's files under its path at the pinned commit.
tree_files() {
  local links sha path
  links=$(pins "$1") || return 1
  git -C "$ROOT" ls-tree -r "$1" | awk -F'\t' '$1 !~ /^160000 / { print $2 }'
  while read -r sha path; do
    [ -n "$sha" ] || continue
    pinned "$sha" "$path" && git -C "$ROOT/$path" ls-tree -r --name-only "$sha" | sed "s|^|$path/|" || return 1
  done <<< "$links"
}
