#!/usr/bin/env bash
# Replaces third_party/opus with libopus's library sources (include, src,
# celt, silk, their source lists, and COPYING) of one exact revision, taken
# from a checkout's object store, never from its working tree.
#
#   tools/update_opus.sh <checkout> <revision>
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target="third_party/opus"

keep="$(mktemp)"
if [[ -f "$target/CMakeLists.txt" ]]; then
    cp "$target/CMakeLists.txt" "$keep"
fi
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" COPYING include src celt silk \
    opus_sources.mk celt_sources.mk silk_sources.mk | tar -x -C "$target"
if [[ -s "$keep" ]]; then
    cp "$keep" "$target/CMakeLists.txt"
fi
rm "$keep"
echo "$target is now $revision; update the table in third_party/README.md"
