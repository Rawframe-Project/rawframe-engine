#!/usr/bin/env bash
# Runners heard from a Build: its cooked content is packed into a Build,
# and a client reads its sounds from the Build named by the root hash the
# packer printed, verified chunk by chunk.
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
packed=$("$build" "$cooked" "$work/build" rawframe/runners 0.1.0 linux x86_64 client build.development tool)
root=$(printf '%s\n' "$packed" | grep -o '^build sha256:[0-9a-f]\{64\}' | cut -d' ' -f2)

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 360
host.iteration_rate = 120
world.tick_rate = 60
kest.game = games/runners/runners.game
kest.library = third_party/kest/lib/
content.build = $work/build
content.build_root = $root
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
audio.record = $work/heard.wav
CONF

(cd "$repository" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson")
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
