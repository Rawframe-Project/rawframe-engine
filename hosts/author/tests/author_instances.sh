#!/usr/bin/env bash
# Runners' platforms changed through their instances' patches: one moved by
# a field set on the entity its instance brings, the other's patch reverted
# so the prefab's pose holds, and a dry run of removing the first. A fourth
# platform is instanced from the prefab beside the game, found by reading
# the level, and moved; removing it is dry-run. The game then starts on the
# level as changed.
#
# usage: author_instances.sh <rawframe-author> <rawframe-arena> <repository> <work directory>
set -euo pipefail

author=$1
arena=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work"
cp -r "$repository/games/runners/." "$work/"
pose=fdf0050d-881c-4451-b541-754c67d1bbf8
first=b4560e55-e4e2-467a-ab08-e91b7119397f
second=70d54325-4d2f-414f-8bcf-d81fce71cc73

"$author" describe >"$work/describe.out"
grep -q '"scene.revert_component"' "$work/describe.out"

cat >"$work/platforms.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.set_field", "entity": "$first", "component": "$pose", "field": "x",
   "value": {"real": -12}},
  {"operation": "scene.revert_component", "entity": "$second", "component": "$pose"}]}
JSON
"$author" apply "$work/runners.game" "$work/level.scene" "$work/platforms.json" >"$work/platforms.out"
grep -q '"deltas": 2' "$work/platforms.out"
grep -q '"x": -12' "$work/level.scene"
if grep -q '"entity": "'"$second"'"' "$work/level.scene"; then
    exit 1
fi

# The first platform's removal drops its entry and adds the removal.
cp "$work/level.scene" "$work/kept.scene"
cat >"$work/removal.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.destroy_entity", "entity": "$first"}]}
JSON
"$author" apply "$work/runners.game" "$work/level.scene" "$work/removal.json" --dry-run >"$work/removal.out"
grep -q '"deltas": 2' "$work/removal.out"
cmp -s "$work/level.scene" "$work/kept.scene"

# A fourth platform: its entity found among the level's by its instance.
prefab=8d1a709be7c6f665a9d32c2c07e122c3
cat >"$work/fourth.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.add_instance", "scene": "$prefab", "instance": "3f0e9a52-7c1d-4b8e-a6f2-0d4c9b1e5a77"}]}
JSON
"$author" apply "$work/runners.game" "$work/level.scene" "$work/fourth.json" >"$work/fourth.out"
grep -q '"deltas": 1' "$work/fourth.out"
cat >"$work/list.json" <<JSON
{"formatVersion": 1, "kind": "authoring.query", "queries": [{"operation": "scene.list_entities"}]}
JSON
"$author" read "$work/runners.game" "$work/level.scene" "$work/list.json" >"$work/list.out"
grep -B2 '"instance": 3,' "$work/list.out" >"$work/fourth.id"
fourth=$(sed -n 's/.*"id": "\([0-9a-f-]*\)".*/\1/p' "$work/fourth.id")
test -n "$fourth"
cat >"$work/move.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.set_field", "entity": "$fourth", "component": "$pose", "field": "x",
   "value": {"real": 20}},
  {"operation": "scene.set_field", "entity": "$fourth", "component": "$pose", "field": "y",
   "value": {"real": 4}}]}
JSON
"$author" apply "$work/runners.game" "$work/level.scene" "$work/move.json" >"$work/move.out"
grep -q '"x": 20' "$work/level.scene"
cp "$work/level.scene" "$work/four.scene"
cat >"$work/unmake.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.remove_instance", "entity": "$fourth"}]}
JSON
"$author" apply "$work/runners.game" "$work/level.scene" "$work/unmake.json" --dry-run >"$work/unmake.out"
grep -q '"deltas": 1' "$work/unmake.out"
cmp -s "$work/level.scene" "$work/four.scene"

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 4
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $work/runners.game
CONF
"$arena" --config "$work/arena.conf" >"$work/started.log"
grep -q '"code":"game_loaded"' "$work/started.log"
echo "authored runners' instances"
