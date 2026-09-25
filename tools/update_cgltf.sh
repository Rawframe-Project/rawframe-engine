#!/usr/bin/env bash
# Replaces third_party/cgltf with cgltf's parser header and LICENSE of one
# exact revision, taken from a checkout's object store, never from its
# working tree. The build file and the implementation unit beside them are
# ours and are kept.
#
#   tools/update_cgltf.sh <checkout> <revision>
#
# Files are extracted with the time they are written, not their commit's,
# so a build that already ran builds everything from them again.
set -euo pipefail
cd "$(dirname "$0")/.."

checkout="$1"
revision="$(git -C "$checkout" rev-parse --verify "$2^{commit}")"
target="third_party/cgltf"

keep="$(mktemp -d)"
for ours in CMakeLists.txt cgltf.c; do
    if [[ -f "$target/$ours" ]]; then
        cp "$target/$ours" "$keep/$ours"
    fi
done
rm -rf "$target"
mkdir -p "$target"
git -C "$checkout" archive "$revision" LICENSE cgltf.h | tar -x -m -C "$target"
cp "$keep"/* "$target/" 2>/dev/null || true
rm -rf "$keep"
echo "$target is now $revision; update the table in third_party/README.md"
