#!/usr/bin/env bash
# A copy of runners' level changed through rawframe-author: a crate with a
# body and a pose made in one atomic request, then a stale request, a
# failing atomic request, and a dry run, none of which changes the file.
#
# usage: author_runners.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work"
cp "$repository/games/runners/level.scene" "$work/level.scene"
game="$repository/games/runners/runners.game"
body=d0ae39a2-4803-4ee0-9d45-1980875324d0
pose=fdf0050d-881c-4451-b541-754c67d1bbf8
crate=6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30

"$author" describe >"$work/describe.out"
grep -q '"scene.set_reference"' "$work/describe.out"

cat >"$work/crate.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.create_entity", "entity": "$crate", "name": "crate", "place": 0},
  {"operation": "scene.add_component", "entity": "$crate", "component": "$body"},
  {"operation": "scene.set_field", "entity": "$crate", "component": "$body", "field": "shape",
   "value": {"unsigned": "1"}},
  {"operation": "scene.set_field", "entity": "$crate", "component": "$body", "field": "width",
   "value": {"real": 2}},
  {"operation": "scene.add_component", "entity": "$crate", "component": "$pose"},
  {"operation": "scene.set_field", "entity": "$crate", "component": "$pose", "field": "x",
   "value": {"real": 3.5}}
]}
JSON
"$author" apply "$game" "$work/level.scene" "$work/crate.json" >"$work/crate.out"
grep -q '"written": true' "$work/crate.out"
grep -q '"deltas": 6' "$work/crate.out"
grep -q '"id": "'"$crate"'"' "$work/level.scene"
grep -q '"x": 3.5' "$work/level.scene"
cp "$work/level.scene" "$work/made.scene"

# Computed against the scene before the crate: stale, and nothing changes.
before=$(sha256sum "$repository/games/runners/level.scene" | cut -d' ' -f1)
cat >"$work/stale.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "expects": "sha256:$before",
 "operations": [{"operation": "scene.rename_entity", "entity": "$crate", "name": "box"}]}
JSON
if "$author" apply "$game" "$work/level.scene" "$work/stale.json" >"$work/stale.out"; then
    exit 1
fi
grep -q '"code": "target_stale"' "$work/stale.out"

# Atomic: the rename would do, the second crate would not; neither is kept.
cat >"$work/twice.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.rename_entity", "entity": "$crate", "name": "box"},
  {"operation": "scene.create_entity", "entity": "$crate", "name": "again"}
]}
JSON
if "$author" apply "$game" "$work/level.scene" "$work/twice.json" >"$work/twice.out"; then
    exit 1
fi
grep -q '"code": "conflict"' "$work/twice.out"
grep -q '"written": false' "$work/twice.out"

# A dry run of a rename that would succeed writes nothing.
cat >"$work/dry.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.rename_entity", "entity": "$crate", "name": "box"}]}
JSON
"$author" apply "$game" "$work/level.scene" "$work/dry.json" --dry-run >"$work/dry.out"
grep -q '"deltas": 1' "$work/dry.out"
grep -q '"written": false' "$work/dry.out"
cmp -s "$work/level.scene" "$work/made.scene"
echo "authored runners"
