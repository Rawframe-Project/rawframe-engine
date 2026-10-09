#!/usr/bin/env bash
# Runners is heard and drawn while its sources are recooked: the shot,
# cooked first to the short-form tier, is recooked to Opus once the host has
# started, and the tiles, cooked exact, are recooked block-compressed; the
# running client publishes the changed manifest, hears the new revision of
# the shot, and draws the new revision of the tiles (D262). Runners hit
# are told so by messages (D266).
#
# usage: runners_recooked.sh <rawframe-arena> <rawframe-cook> <repository> <work directory>
set -euo pipefail

arena=$1
cook=$2
repository=$3
work=$4

rm -rf "$work"
mkdir -p "$work/sources"
# A step that fails says which, and how the arena ended: under a loaded
# machine its World may fall behind and stop it (D302).
trap 'echo "runners_recooked: failed at line $LINENO"; grep -o "\"code\":\"stopped\".*" "$work/log.ndjson" 2>/dev/null | cut -c1-600' ERR
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
# What is checked is what is heard, not how fast a loaded machine ticks
# (D516, D569).
world.degraded_ms = 600000
world.overload_ms = 0
CONF

(cd "$repository" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson") &
running=$!

# Recook only once the client's sounds and textures were read from the
# first cook, or the client reads the recooked ones first and has nothing
# to reload. The run lasts ten seconds, room for a sanitized recook of five
# on a busy machine.
for _ in $(seq 1 200); do
    grep -q '"code":"sounds_read"' "$work/log.ndjson" 2>/dev/null &&
        grep -q '"code":"textures_read"' "$work/log.ndjson" && break
    sleep 0.05
done
resource=$(grep -o '"resourceId": "[0-9a-f]*"' "$work/sources/shot.wav.rfmeta" | grep -o '[0-9a-f]\{32\}')
printf '{\n  "schema": 1,\n  "resourceId": "%s",\n  "importer": "rawframe.audio",\n  "settings": {\n    "tier": "opus"\n  }\n}\n' \
    "$resource" >"$work/sources/shot.wav.rfmeta"
resource=$(grep -o '"resourceId": "[0-9a-f]*"' "$work/sources/tiles.png.rfmeta" | grep -o '[0-9a-f]\{32\}')
printf '{\n  "schema": 1,\n  "resourceId": "%s",\n  "importer": "rawframe.texture",\n  "settings": {}\n}\n' \
    "$resource" >"$work/sources/tiles.png.rfmeta"
"$cook" "$work/sources" "$work/content" "$work/cache" >/dev/null

wait "$running"
grep -q '"code":"content_reloaded"' "$work/log.ndjson"
grep -q '"code":"sound_reloaded"' "$work/log.ndjson"
grep -q '"code":"texture_reloaded"' "$work/log.ndjson"
if grep -q '"code":"sound_reload_failed"\|"code":"texture_reload_failed"' "$work/log.ndjson"; then
    exit 1
fi
# Runners hit were told so (D266): none sent to no one, and all arrived but
# those still on their way when the run ended, one per runner at most.
sent=$(grep -o '"messagesSent":[0-9]*' "$work/log.ndjson" | grep -o '[0-9]*$')
received=$(grep -o '"messagesReceived":[0-9]*' "$work/log.ndjson" | grep -o '[0-9]*$')
grep -q '"messagesUndelivered":0' "$work/log.ndjson"
if [ "$received" -lt 1 ] || [ "$received" -gt "$sent" ] || [ $((sent - received)) -gt 4 ]; then
    echo "messages sent $sent, received $received"
    exit 1
fi
grep -o '"code":"recording_summary".*' "$work/log.ndjson"
