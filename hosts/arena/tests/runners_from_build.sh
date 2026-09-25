#!/usr/bin/env bash
# Runners heard from its Composition: a publisher key is made, runners'
# cooked content is packed into a Build signed with it and installed in a
# library beside the publisher's key set, a CompositionRecord names it as
# the Game, and a client reads its sounds from the Build the record names,
# its signature checked against the pinned key set and its chunks verified
# one by one. A record naming another version of the Build is refused.
#
# usage: runners_from_build.sh <rawframe-arena> <rawframe-build> <repository> <cooked content> <work directory>
set -euo pipefail

arena=$1
build=$2
repository=$3
cooked=$4
work=$5

rm -rf "$work"
mkdir -p "$work"
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
kest.game = games/runners/runners.game
content.composition = $1
content.library = $library
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
audio.record = $work/heard.wav
CONF
    (cd "$repository" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson" 2>"$work/errors.txt")
}

# A record that names version 0.2.0 of the same root: refused before any
# World runs.
sed 's/"version":"0.1.0"/"version":"0.2.0"/' "$work/runners.composition" >"$work/other.composition"
if run "$work/other.composition"; then
    echo "a Composition naming another version was run" >&2
    exit 1
fi

run "$work/runners.composition"
grep -q '"code":"composition_opened"' "$work/log.ndjson"
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
