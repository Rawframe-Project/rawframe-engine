#!/usr/bin/env bash
# Patrol's guards authored from nothing (D271): the discovery gives the
# guard's `at` field as a case with its enum's cases, a guard set to one by
# name reads back by it, the first case is the default and no field, a name
# the enum lacks is refused, and the game starts on the scene made.
#
# usage: author_cases.sh <rawframe-author> <rawframe-arena> <repository> <work directory>
set -euo pipefail

author=$1
arena=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work"
cp "$repository/modules/world_kest/tests/game/patrol.kest" "$work/"
sed '/^spawn/d' "$repository/modules/world_kest/tests/game/patrol.game" >"$work/patrol.game"
echo "scene guards.scene" >>"$work/patrol.game"
printf '{\n  "kind": "rawframe.scene",\n  "formatVersion": 1,\n  "schema": {},\n  "entities": []\n}\n' \
    >"$work/guards.scene"
game="$work/patrol.game"
guard=7c2e9a14-3b58-4d61-9f0a-2e8d5b1c6a73
first=2197be45-b62c-4756-a4d6-744b05b97268
second=82846218-af54-4389-ad03-70bff12f711b

"$author" describe "$game" >"$work/discovery.json"
grep -A8 '"name": "at"' "$work/discovery.json" | tr -d ' \n' | grep -q '"kind":"case","cases":\["Start","Walking","Looking","Done"\]'

cat >"$work/guards.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.create_entity", "entity": "$first", "name": "walking", "place": 0},
  {"operation": "scene.add_component", "entity": "$first", "component": "$guard"},
  {"operation": "scene.set_field", "entity": "$first", "component": "$guard", "field": "posts",
   "value": {"signed": "3"}},
  {"operation": "scene.create_entity", "entity": "$second", "name": "done", "place": 1},
  {"operation": "scene.add_component", "entity": "$second", "component": "$guard"},
  {"operation": "scene.set_field", "entity": "$second", "component": "$guard", "field": "at",
   "value": {"case": "Done"}},
  {"operation": "scene.set_field", "entity": "$first", "component": "$guard", "field": "at",
   "value": {"case": "Walking"}},
  {"operation": "scene.set_field", "entity": "$first", "component": "$guard", "field": "at",
   "value": {"case": "Start"}}
]}
JSON
"$author" apply "$game" "$work/guards.scene" "$work/guards.json" >"$work/guards.out"
grep -q '"written": true' "$work/guards.out"
grep -q '"at": "Done"' "$work/guards.scene"
test "$(grep -c '"at"' "$work/guards.scene")" = 1

cat >"$work/read.json" <<JSON
{"formatVersion": 1, "kind": "authoring.query", "queries": [
  {"operation": "scene.read_entity", "entity": "$second"}]}
JSON
"$author" read "$game" "$work/guards.scene" "$work/read.json" >"$work/read.out"
tr -d ' \n' <"$work/read.out" | grep -q '"name":"at","value":{"case":"Done"}'

cp "$work/guards.scene" "$work/made.scene"
cat >"$work/lost.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.set_field", "entity": "$first", "component": "$guard", "field": "at",
   "value": {"case": "Lost"}}]}
JSON
if "$author" apply "$game" "$work/guards.scene" "$work/lost.json" >"$work/lost.out"; then
    exit 1
fi
grep -q '"code": "validation_failed"' "$work/lost.out"
cmp -s "$work/guards.scene" "$work/made.scene"

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 4
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $game
CONF
"$arena" --config "$work/arena.conf" >"$work/started.log"
grep -q '"code":"game_loaded"' "$work/started.log"
echo "authored patrol's cases"
