#!/usr/bin/env bash
# Runners played and heard from its Composition alone: a publisher key is
# made, runners' cooked content is packed into a Build signed with it and
# installed in a library beside the publisher's key set, a CompositionRecord
# names it as the Game, and the process, started outside the repository,
# reads the game's description, programs, and sounds from the Build the
# record names, its signature checked against the pinned key set and its
# chunks verified one by one. A record naming another version of the Build
# is refused.
#
# usage: runners_from_build.sh <rawframe-arena> <rawframe-build> <repository> <cooked content> <work directory>
set -euo pipefail

arena=$1
build=$2
repository=$3
cooked=$4
work=$5

rm -rf "$work"
mkdir -p "$work/elsewhere"
# The cooked description's identity, from its sidecar.
game=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' "$repository/games/runners/runners.game.rfmeta")
library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
"$build" "$cooked" "$work/build" rawframe/runners 0.1.0 linux x86_64 client build.development tool \
    "$library/keys/$kid.key" >/dev/null
root=$("$build" install "$work/build" "$library" | cut -d' ' -f2)
"$build" compose "$library" "$root" tool "$work/runners.composition" >/dev/null

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

# A record that names version 0.2.0 of the same root: refused before any
# World runs, with the data category's exit code.
sed 's/"version":"0.1.0"/"version":"0.2.0"/' "$work/runners.composition" >"$work/other.composition"
status=0
run "$work/other.composition" || status=$?
if [ "$status" -ne 65 ]; then
    echo "a Composition naming another version exited $status, not 65 (SPEC-0012's data category)" >&2
    exit 1
fi
grep -q '"code":"not_started".*"exitCode":65' "$work/log.ndjson"

run "$work/runners.composition"
grep -q '"code":"composition_opened"' "$work/log.ndjson"
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
