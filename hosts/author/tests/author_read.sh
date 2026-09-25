#!/usr/bin/env bash
# Runners' level read as an agent would: the pose component's type id found
# in the game's discovery, the level's entities listed, a platform read with
# its patch's values typed, and the value read set back through a request,
# which changes nothing; a query of an entity the level lacks is answered
# with the one error record beside the others' answers.
#
# usage: author_read.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work"
cp "$repository/games/runners/level.scene" "$work/level.scene"
game="$repository/games/runners/runners.game"
platform=b4560e55-e4e2-467a-ab08-e91b7119397f

"$author" describe "$game" >"$work/discovery.json"
# The id is the member before the name in each component's record.
pose=$(grep -B1 '"name": "rawframe.physics2d.pose"' "$work/discovery.json" | sed -n 's/.*"id": "\([0-9a-f-]*\)".*/\1/p')
test -n "$pose"
grep -q '"name": "scene.read_entity"' "$work/discovery.json"

cat >"$work/queries.json" <<JSON
{"formatVersion": 1, "kind": "authoring.query", "queries": [
  {"operation": "scene.list_entities"},
  {"operation": "scene.read_entity", "entity": "$platform"},
  {"operation": "scene.read_entity", "entity": "00000000-0000-4000-8000-000000000000"}]}
JSON
if "$author" read "$game" "$work/level.scene" "$work/queries.json" >"$work/answers.json"; then
    exit 1
fi
grep -q '"kind": "authoring.answers"' "$work/answers.json"
grep -q '"source": "e48e0c63-5d86-4575-95b8-dfe21c096e88"' "$work/answers.json"
grep -q '"patch": "set"' "$work/answers.json"
grep -q '"component": "'"$pose"'"' "$work/answers.json"
grep -q '"real": -15' "$work/answers.json"
grep -q '"code": "target_not_found"' "$work/answers.json"

# The value as read, set again: nothing to do, nothing written.
cp "$work/level.scene" "$work/kept.scene"
cat >"$work/again.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.set_field", "entity": "$platform", "component": "$pose", "field": "x",
   "value": {"real": -15}}]}
JSON
"$author" apply "$game" "$work/level.scene" "$work/again.json" >"$work/again.out"
grep -q '"deltas": 0' "$work/again.out"
grep -q '"written": false' "$work/again.out"
cmp -s "$work/level.scene" "$work/kept.scene"
echo "read runners"
