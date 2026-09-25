#!/usr/bin/env bash
# Replaces third_party/<engine> with the library sources of one exact Maul2D
# or Maul3D revision, taken from a checkout's object store, never from its
# working tree.
#
#   tools/update_maul.sh <maul2d|maul3d> <checkout> <revision>
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

engine="$1"
case "$engine" in
maul2d | maul3d) ;;
*)
    echo "update_maul.sh: the engine is maul2d or maul3d" >&2
    exit 2
    ;;
esac
checkout="$2"
revision="$(git -C "$checkout" rev-parse --verify "$3^{commit}")"
target="third_party/$engine"

keep="$(mktemp)"
if [[ -f "$target/CMakeLists.txt" ]]; then
    cp "$target/CMakeLists.txt" "$keep"
fi
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" include src LICENSE | tar -x -m -C "$target"
if [[ -s "$keep" ]]; then
    cp "$keep" "$target/CMakeLists.txt"
fi
rm "$keep"
echo "$target is now $revision; update the table in third_party/README.md and the source list in"
echo "$target/CMakeLists.txt if upstream's changed"
