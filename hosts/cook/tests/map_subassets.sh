#!/usr/bin/env bash
# An author brings plaza's crate with a sidecar that maps none of its
# subassets: the cook refuses it by the keys to map, `--map` gives each a
# new identity and writes the sidecar, the cook then passes, and a second
# map finds nothing to give (D316).
#
# usage: map_subassets.sh <rawframe-cook> <repository> <work directory>
set -euo pipefail

cook=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work/sources"
cp "$repository/games/plaza/crate.gltf" "$repository/games/plaza/crate.png" "$work/sources/"
printf '{\n  "schema": 1,\n  "resourceId": "5cdfde71bbc7f622b4c029a1aed59f5d",\n  "importer": "rawframe.mesh"\n}\n' \
    >"$work/sources/crate.gltf.rfmeta"

if "$cook" "$work/sources" "$work/content" >"$work/refused.log" 2>&1; then
    echo "a crate with no subasset map cooked"
    exit 1
fi
grep -q "a subasset the sidecar maps to no resource: texture/Planks" "$work/refused.log"

"$cook" --map "$work/sources" >"$work/map.log"
grep -qx "crate.gltf.rfmeta: material/Iron" "$work/map.log"
grep -qx "crate.gltf.rfmeta: material/Planks" "$work/map.log"
grep -qx "crate.gltf.rfmeta: texture/Planks" "$work/map.log"
grep -q '"texture/Planks": "' "$work/sources/crate.gltf.rfmeta"

"$cook" "$work/sources" "$work/content" >/dev/null
"$cook" --map "$work/sources" | grep -qx "mapped 0 sidecars, failed 0"
rm -rf "$work"
echo "mapped and cooked"
