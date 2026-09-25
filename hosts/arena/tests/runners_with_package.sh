#!/usr/bin/env bash
# Runners played from a Composition of two Builds: the game without its
# shot's recording, and a Package that holds only the recording. Both are
# cooked apart, packed, signed, and installed; composed with the Package,
# a process started outside the repository hears shots, the game's sound
# resolving to the Package's clip by its identity; composed without it, the
# Composition is not the closure the game needs, and the process does not
# start.
#
# usage: runners_with_package.sh <rawframe-arena> <rawframe-cook> <rawframe-build> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
build=$3
repository=$4
work=$5

rm -rf "$work"
mkdir -p "$work/game" "$work/package" "$work/elsewhere"
cp -r "$repository/games/runners/." "$work/game/"
mv "$work/game/shot.wav" "$work/game/shot.wav.rfmeta" "$work/package/"
"$cook" "$work/game" "$work/game-cooked" >/dev/null
"$cook" "$work/package" "$work/package-cooked" >/dev/null

library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
pack() {
    "$build" "$1" "$2" "$3" 0.1.0 linux x86_64 client build.development tool "$library/keys/$kid.key" >/dev/null
    "$build" install "$2" "$library" | cut -d' ' -f2
}
game_root=$(pack "$work/game-cooked" "$work/game-build" rawframe/runners)
package_root=$(pack "$work/package-cooked" "$work/package-build" rawframe/runners-shots)
"$build" compose "$library" "$game_root" tool "$work/with.composition" "$package_root" >/dev/null
"$build" compose "$library" "$game_root" tool "$work/without.composition" >/dev/null
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/runners/runners.game.rfmeta")

run() {
    cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 360
host.iteration_rate = 120
world.tick_rate = 60
kest.game_resource = $game
content.composition = $1
content.library = $library
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
audio.record = $work/heard.wav
CONF
    (cd "$work/elsewhere" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
}

# Without the Package: the shot's recording is no resource of the
# Composition, and nothing starts.
if run "$work/without.composition"; then
    echo "a Composition without the Package that holds a sound's recording was run" >&2
    exit 1
fi
grep -q '"code":"start_failed".*"message":"no such resource"' "$work/log.ndjson"

run "$work/with.composition"
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
