#!/usr/bin/env bash
# Quarry modded: quarry and quarry-canyon cooked, packed, installed
# into one library, and composed twice, plain and with the mod; a dedicated
# server plays each from the same seed with no player, so nothing changes
# the land but its making. With the canyon providing quarry's ground
# service, its land holds far fewer blocks: the canyon's columns are one
# block deep.
#
# usage: quarry_with_mod.sh <rawframe-server> <rawframe-cook> <rawframe-build> <repository> <work directory>
set -euo pipefail

server=$1
cook=$2
build=$3
repository=$4
work=$5

rm -rf "$work"
mkdir -p "$work/game" "$work/canyon" "$work/elsewhere"
cp -r "$repository/games/quarry/." "$work/game/"
cp -r "$repository/games/quarry-canyon/." "$work/canyon/"
"$cook" "$work/game" "$work/game-cooked" >/dev/null
"$cook" "$work/canyon" "$work/canyon-cooked" >/dev/null

library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
pack() {
    "$build" "$1" "$2" "$3" 0.1.0 linux x86_64 server build.development tool "$library/keys/$kid.key" >/dev/null
    "$build" install "$2" "$library" | cut -d' ' -f2
}
game_root=$(pack "$work/game-cooked" "$work/game-build" rawframe/quarry)
canyon_root=$(pack "$work/canyon-cooked" "$work/canyon-build" rawframe/quarry-canyon)
"$build" compose "$library" "$game_root" tool "$work/plain.composition" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/canyon.composition" --mod "$canyon_root" >/dev/null
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/quarry/quarry.game.rfmeta")

# The entities at the end of a run of the composition: the land's one
# entity and its blocks.
run() {
    cat >"$work/server.conf" <<CONF
host.maximum_iterations = 90
host.iteration_rate = 120
world.tick_rate = 60
world.root_seed = 7
world.overload_ms = 0
kest.game_resource = $game
content.composition = $1
content.library = $library
CONF
    (cd "$work/elsewhere" && "$server" --config "$work/server.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
    grep -o '"code":"tick_summary".*"entities":[0-9]*' "$work/log.ndjson" | sed 's/.*"entities"://'
}

plain=$(run "$work/plain.composition")
grep -q '"modProviders":0,' "$work/log.ndjson"
carved=$(run "$work/canyon.composition")
grep -q '"modProviders":1,' "$work/log.ndjson"
echo "plain $plain entities, carved $carved"
# The canyon's four columns and its sides take hundreds of blocks away.
if [ "$carved" -ge $((plain - 500)) ]; then
    echo "the canyon took too little away" >&2
    exit 1
fi
echo "mod shaped the land: $plain to $carved entities"
