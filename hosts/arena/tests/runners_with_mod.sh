#!/usr/bin/env bash
# Runners played with a mod from a Composition (D180): the game and the
# runners-timers mod are cooked, packed, and signed apart, and composed with
# the mod as a Mod. Runners takes it under its open policy, and its two
# timers join the World beside the game's own entities. A mod whose range
# the game's Mod API version is outside, and one whose timer is also
# persistent, are refused before any World runs.
#
# usage: runners_with_mod.sh <rawframe-arena> <rawframe-cook> <rawframe-build> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
build=$3
repository=$4
work=$5

rm -rf "$work"
mkdir -p "$work/game" "$work/mod" "$work/late" "$work/wide" "$work/elsewhere"
cp -r "$repository/games/runners/." "$work/game/"
cp -r "$repository/games/runners-timers/." "$work/mod/"
cp -r "$repository/games/runners-timers/." "$work/late/"
sed -i 's/^modapi 1$/modapi >=2/' "$work/late/timers.mod"
cp -r "$repository/games/runners-timers/." "$work/wide/"
cat >"$work/wide/timers.scene" <<'SCENE'
{
  "kind": "rawframe.scene",
  "formatVersion": 1,
  "schema": {
    "rawframe.world.persistent": "afc8322918a647ff",
    "runners.age": "b1e3cd3c575b7a33"
  },
  "entities": [
    {
      "id": "faadb25f-9d04-442e-bbd6-db5e371a6a89",
      "name": "dawn",
      "components": {
        "rawframe.world.persistent": {},
        "runners.age": {}
      }
    }
  ]
}
SCENE
"$cook" "$work/game" "$work/game-cooked" >/dev/null
"$cook" "$work/mod" "$work/mod-cooked" >/dev/null
"$cook" "$work/late" "$work/late-cooked" >/dev/null
"$cook" "$work/wide" "$work/wide-cooked" >/dev/null

library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
pack() {
    "$build" "$1" "$2" "$3" 0.1.0 linux x86_64 client build.development tool "$library/keys/$kid.key" >/dev/null
    "$build" install "$2" "$library" | cut -d' ' -f2
}
game_root=$(pack "$work/game-cooked" "$work/game-build" rawframe/runners)
mod_root=$(pack "$work/mod-cooked" "$work/mod-build" rawframe/runners-timers)
late_root=$(pack "$work/late-cooked" "$work/late-build" rawframe/runners-late)
wide_root=$(pack "$work/wide-cooked" "$work/wide-build" rawframe/runners-wide)
"$build" compose "$library" "$game_root" tool "$work/plain.composition" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/modded.composition" --mod "$mod_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/late.composition" --mod "$late_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/wide.composition" --mod "$wide_root" >/dev/null
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/runners/runners.game.rfmeta")

run() {
    cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 30
host.iteration_rate = 120
world.tick_rate = 60
kest.game_resource = $game
content.composition = $1
content.library = $library
CONF
    (cd "$work/elsewhere" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
}
entities() {
    grep -o '"code":"game_loaded".*"entities":[0-9]*' "$work/log.ndjson" | head -1 | sed 's/.*"entities"://'
}

# Out of range: refused, and nothing starts.
if run "$work/late.composition"; then
    echo "a mod outside the game's Mod API version was taken" >&2
    exit 1
fi
grep -q "Mod API version is outside a mod's range" "$work/log.ndjson"
# A timer that is also persistent: refused, as a mod adds values only.
if run "$work/wide.composition"; then
    echo "a mod's entity holding more than the point's component was spawned" >&2
    exit 1
fi
grep -q "holds the point's component and nothing else" "$work/log.ndjson"

run "$work/plain.composition"
plain=$(entities)
run "$work/modded.composition"
modded=$(entities)
if [ "$modded" -ne $((plain + 2)) ]; then
    echo "the mod's two timers did not join the World: $plain without it, $modded with it" >&2
    exit 1
fi
echo "mod taken: $plain entities without it, $modded with it"
