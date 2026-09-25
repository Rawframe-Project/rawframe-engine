#!/usr/bin/env bash
# Replaces third_party/maul2d with the library sources of one exact Maul2D
# revision, taken from a checkout's object store, never from its working tree.
#
#   tools/update_maul2d.sh <maul2d checkout> <revision>
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target=third_party/maul2d

keep="$(mktemp)"
cp "$target/CMakeLists.txt" "$keep"
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" include src LICENSE | tar -x -C "$target"
cp "$keep" "$target/CMakeLists.txt"
rm "$keep"
echo "third_party/maul2d is now $revision; update the table in third_party/README.md and the source list in"
echo "third_party/maul2d/CMakeLists.txt if upstream's changed"
