#!/usr/bin/env bash
# Runners' scenes migrated after its Age component changes: a field added
# carries the hall's age over by name and the game starts again; a field
# renamed, which is a field removed and another added, is refused, naming
# the field, and the scene is left as it was.
#
# usage: author_migrate.sh <rawframe-author> <rawframe-arena> <repository> <work directory>
set -euo pipefail

author=$1
arena=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work"
cp -r "$repository/games/runners/." "$work/"
age=8c4e2a17-5b90-4d3f-a6e8-1f7c9d0b2e53
hall=909c0889-a695-4893-a218-b36b9eb97339

cat >"$work/aged.json" <<JSON
{"formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  {"operation": "scene.set_field", "entity": "$hall", "component": "$age", "field": "ticks",
   "value": {"unsigned": "5"}}]}
JSON
"$author" apply "$work/runners.game" "$work/hall.scene" "$work/aged.json" >/dev/null
grep -q '"ticks": 5' "$work/hall.scene"

# Age grows a field: the hall is authored against an older layout.
perl -pi -e 's/^struct Age {$/struct Age {\n    laps: u32/' "$work/runners.kest"
cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 4
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $work/runners.game
CONF
if "$arena" --config "$work/arena.conf" >"$work/stale.log" 2>&1; then
    exit 1
fi
grep -q 'another layout of a component' "$work/stale.log"

"$author" migrate "$work/runners.game" "$work/hall.scene" "$work/level.scene" --dry-run >"$work/dry.out"
grep -q '"verdict": "migrated"' "$work/dry.out"
if "$arena" --config "$work/arena.conf" >/dev/null 2>&1; then
    exit 1
fi
"$author" migrate "$work/runners.game" "$work/hall.scene" "$work/level.scene" >"$work/migrated.out"
grep -q '"verdict": "migrated"' "$work/migrated.out"
grep -q '"verdict": "unchanged"' "$work/migrated.out"
grep -q '"ticks": 5' "$work/hall.scene"
"$arena" --config "$work/arena.conf" >"$work/started.log"
grep -q '"code":"game_loaded"' "$work/started.log"
"$author" migrate "$work/runners.game" "$work/hall.scene" >"$work/again.out"
grep -q '"verdict": "unchanged"' "$work/again.out"

# Age's ticks renamed: a new field and an old one gone, so the hall's age
# has nowhere to go, and nothing is dropped.
cp "$work/hall.scene" "$work/kept.scene"
perl -pi -e 's/^    ticks: u64$/    played: u64/; s/ages\[i\]\.ticks/ages[i].played/g' "$work/runners.kest"
if "$author" migrate "$work/runners.game" "$work/hall.scene" >"$work/refused.out"; then
    exit 1
fi
grep -q '"verdict": "refused"' "$work/refused.out"
grep -q '"residual": "ticks"' "$work/refused.out"
cmp -s "$work/hall.scene" "$work/kept.scene"
echo "migrated runners"
