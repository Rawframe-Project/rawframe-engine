#!/usr/bin/env bash
# SPEC-0041's divergence drill (D204) and its two-sided capture (D275): four
# bots predict their runners and flip one bit of every state they hash, so
# the server finds every checksum record diverged, logs each, and plays on;
# both sides capture what they hashed, and set side by side, each diverged
# record differs from what the server committed in that one bit alone.
# Development builds only.
#
# usage: runners_divergence_drill.sh <rawframe-arena> <repository> <work directory>
set -euo pipefail

arena=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work"
cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 600
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $repository/games/runners/runners.game
network.loopback.latency_ms = 10
replication.endpoint = arena
replication.divergence_capture = true
bots.count = 4
bots.endpoint = arena
bots.checksum_interval = 30
bots.divergence_drill = true
bots.divergence_capture = true
CONF
(cd "$repository" && "$arena" --config "$work/arena.conf" >"$work/log.ndjson")
grep -q '"checksumsVerified":0,"checksumsUnverifiable":0,"checksumsDiverged":[1-9]' "$work/log.ndjson"

python3 - "$work/log.ndjson" <<'PY'
import json, sys

server, client = {}, {}
for line in open(sys.argv[1]):
    record = json.loads(line)
    fields = record.get("fields", {})
    if record.get("code") == "divergence_capture":
        server.setdefault((fields["tick"], fields["received"]), {})[fields["component"]] = fields["value"]
    elif record.get("code") == "checksum_capture":
        client.setdefault((fields["tick"], fields["checksum"]), {})[fields["component"]] = fields["value"]

paired = 0
for key, committed in server.items():
    hashed = client.get(key)
    if hashed is None:
        continue
    if sorted(committed) != sorted(hashed):
        sys.exit(f"tick {key[0]}: the sides captured other components")
    # The drill flips the lowest bit of the first byte hashed: the first
    # component's first byte, and nothing else.
    for component in sorted(committed):
        ours, theirs = bytes.fromhex(hashed[component]), bytes.fromhex(committed[component])
        flipped = [i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]]
        expected = [0] if component == 0 else []
        if len(ours) != len(theirs) or flipped != expected or (flipped and ours[0] ^ theirs[0] != 1):
            sys.exit(f"tick {key[0]}, component {component}: not the drill's one bit")
    paired += 1
if paired == 0:
    sys.exit(f"no diverged record found on both sides ({len(server)} on the server's, {len(client)} on the bots')")
print(f"captured both sides of {paired} diverged records")
PY
