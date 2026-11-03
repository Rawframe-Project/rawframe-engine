#!/usr/bin/env bash
# Replaces third_party/wuffs with release/c/wuffs-v0.4.c and the licenses of
# one exact revision of google/wuffs-mirror-release-c, taken from a
# checkout's object store, never from its working tree, then applies the
# engine's changes in tools/patches/wuffs, each refused if it no longer
# applies. A change upstream now has is deleted with the move. The build
# file and wuffs.cpp are the engine's and stay.
#
#   tools/update_wuffs.sh <checkout> <revision>
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target="third_party/wuffs"

keep="$(mktemp -d)"
cp "$target/CMakeLists.txt" "$target/wuffs.cpp" "$keep/"
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" LICENSE LICENSE-APACHE LICENSE-MIT | tar -x -m -C "$target"
git -C "$checkout" archive "$revision" release/c/wuffs-v0.4.c | tar -x -m -O > "$target/wuffs-v0.4.c"
cp "$keep/CMakeLists.txt" "$keep/wuffs.cpp" "$target/"
rm -r "$keep"
shopt -s nullglob
for change in tools/patches/wuffs/*.patch; do
    git apply "$change"
done
echo "$target is now $revision; update the table in docs/operations/dependencies.md"
