#!/usr/bin/env bash
# Replaces third_party/zstd with Zstandard's library sources (the common,
# compress, and decompress directories, the public headers, and LICENSE) of
# one exact revision, taken from a checkout's object store, never from its
# working tree.
#
#   tools/update_zstd.sh <checkout> <revision>
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target="third_party/zstd"

keep="$(mktemp)"
if [[ -f "$target/CMakeLists.txt" ]]; then
    cp "$target/CMakeLists.txt" "$keep"
fi
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" LICENSE lib/common lib/compress lib/decompress lib/zstd.h lib/zstd_errors.h |
    tar -x -m -C "$target"
if [[ -s "$keep" ]]; then
    cp "$keep" "$target/CMakeLists.txt"
fi
rm "$keep"
echo "$target is now $revision; update the table in third_party/README.md"
