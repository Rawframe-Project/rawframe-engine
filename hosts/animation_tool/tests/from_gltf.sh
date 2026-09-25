#!/usr/bin/env bash
# An arm imported from glTF (its buffer a sibling file), imported again with
# its identities kept, then cooked: the skeleton and the clip each become a
# resource of their kind.
#
# usage: from_gltf.sh <rawframe-animation> <cook tool> <arm.gltf> <work directory>
set -euo pipefail

tool=$1
cook=$2
source=$3
work=$4

rm -rf "$work"
"$tool" from-gltf "$source" "$work/rig" loop
test -f "$work/rig/skeleton.rfanim"
test -f "$work/rig/raise_arm.rfanim"
cp "$work/rig/skeleton.rfanim.rfmeta" "$work/skeleton.first"
cp "$work/rig/raise_arm.rfanim.rfmeta" "$work/clip.first"
"$tool" from-gltf "$source" "$work/rig" loop
cmp "$work/rig/skeleton.rfanim.rfmeta" "$work/skeleton.first"
cmp "$work/rig/raise_arm.rfanim.rfmeta" "$work/clip.first"
# The clip names the skeleton by the identity its sidecar gives it.
grep -q "$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$work/skeleton.first")" "$work/rig/raise_arm.rfanim"
"$cook" "$work/rig" "$work/content" "$work/cache" | tail -1
echo "animation imported and cooked"
