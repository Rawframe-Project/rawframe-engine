#!/usr/bin/env bash
# Runners is heard while its sources are recooked: the shot, cooked first to
# the short-form tier, is recooked to Opus once the host has started, and
# the running client publishes the changed manifest and hears the new
# revision of the shot.
#
# usage: runners_recooked.sh <rawframe-arena> <rawframe-cook> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work/sources"
cp -r "$repository/games/runners/." "$work/sources/"
"$cook" "$work/sources" "$work/content" "$work/cache" >/dev/null

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 1200
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $repository/games/runners/runners.game
content.root = $work/content
content.reload_every = 12
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
audio.record = $work/heard.wav
CONF

(cd "$repository" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson") &
running=$!

# Recook only once the client's sounds were read from the first cook. The
# run lasts ten seconds, room for a sanitized recook of five on a busy
# machine.
for _ in $(seq 1 200); do
    grep -q '"code":"started"' "$work/log.ndjson" 2>/dev/null && break
    sleep 0.05
done
resource=$(grep -o '"resourceId": "[0-9a-f]*"' "$work/sources/shot.wav.rfmeta" | grep -o '[0-9a-f]\{32\}')
printf '{\n  "schema": 1,\n  "resourceId": "%s",\n  "importer": "rawframe.audio",\n  "settings": {\n    "tier": "opus"\n  }\n}\n' \
    "$resource" >"$work/sources/shot.wav.rfmeta"
"$cook" "$work/sources" "$work/content" "$work/cache" >/dev/null

wait "$running"
grep -q '"code":"content_reloaded"' "$work/log.ndjson"
grep -q '"code":"sound_reloaded"' "$work/log.ndjson"
if grep -q '"code":"sound_reload_failed"' "$work/log.ndjson"; then
    exit 1
fi
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
