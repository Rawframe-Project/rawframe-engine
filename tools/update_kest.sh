#!/usr/bin/env bash
# Replaces third_party/kest with the library sources of one exact Kest
# revision, taken from a checkout's object store, never from its working tree.
#
#   tools/update_kest.sh <kest checkout> <revision>
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target=third_party/kest

keep="$(mktemp)"
cp "$target/CMakeLists.txt" "$keep"
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" include src lib LICENSE | tar -x -C "$target"
rm "$target/src/main.c"
cp "$keep" "$target/CMakeLists.txt"
rm "$keep"
echo "third_party/kest is now $revision; update the table in third_party/README.md"
