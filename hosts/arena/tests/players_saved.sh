#!/usr/bin/env bash
# Runners' players keep their scores between visits (ADR-0057's per-player
# scope): two bots play as the sessions runner-0 and runner-1, and each is
# kept when the arena stops; the next arena gives each their save as they
# join. A reader of the test's own finds each player's slot by the identity
# its session derives, checks the save, and that no score went back. Run
# from the repository root.
#
#   players_saved.sh <rawframe-arena>
set -euo pipefail

arena="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 120
host.iteration_rate = 120
world.tick_rate = 60
kest.game = games/runners/runners.game
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 2
bots.endpoint = arena
bots.session = runner
save.directory = $work/saves
save.namespace = 72756e6e657273000000000000000001
CONF

# Each player's shots as kept, after checking the save and its slot.
shots() {
    python3 - "$work/saves" <<'PY'
import hashlib, struct, sys
out = []
for n in range(2):
    identity = hashlib.sha256(b"rawframe.player.identity.v1" + b"runner-%d" % n).digest()[:16]
    data = open("%s/p-%s.rfsave" % (sys.argv[1], identity.hex()), "rb").read()
    body, digest = data[:-32], data[-32:]
    assert hashlib.sha256(body).digest() == digest, "digest"
    at = 8 + 4 + 16
    length = struct.unpack_from("<H", body, at)[0]
    assert body[at + 2:at + 2 + length] == b"runner"
    at += 2 + length
    assert struct.unpack_from("<I", body, at)[0] == 1
    at += 4 + 16 + 8
    size, fields = struct.unpack_from("<II", body, at)
    assert (size, fields) == (16, 0)
    at += 8
    assert struct.unpack_from("<I", body, at)[0] == 1
    at += 4
    assert body[at:at + 16] == struct.pack("<QQ", *struct.unpack(">QQ", identity)), "identity"
    at += 16
    assert struct.unpack_from("<Q", body, at)[0] == 1
    at += 8
    shots, hits, cooldown, taken = struct.unpack_from("<iiii", body, at)
    assert at + 16 == len(body), "trailing"
    out.append(shots)
print(" ".join(map(str, out)))
PY
}

(cd . && "$arena" --config "$work/arena.conf" >"$work/first.log")
[ "$(grep -c '"code":"save_kept".*"slot":"p-' "$work/first.log")" -eq 2 ]
first=$(shots)
"$arena" --config "$work/arena.conf" >"$work/second.log"
[ "$(grep -c '"code":"player_save_loaded"' "$work/second.log")" -eq 2 ]
second=$(shots)
echo "shots $first, then $second"
read -r a0 a1 <<<"$first"
read -r b0 b1 <<<"$second"
[ "$b0" -ge "$a0" ] && [ "$b1" -ge "$a1" ]
echo "players kept across runs"
