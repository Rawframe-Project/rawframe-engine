#!/usr/bin/env bash
# Runners keeps its hall between runs (ADR-0057): a server plays the level
# for a while and saves when it stops; a second server loads that save and
# plays on. The save is read here by a reader of its own, which checks the
# digest, that the hall carries the identity its scene names it by, and that
# its age grew across both runs. Run from the repository root.
#
#   save.sh <rawframe-server>
set -euo pipefail

server="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cat >"$work/server.conf" <<CONF
host.maximum_iterations = 120
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $PWD/games/runners/runners.game
save.directory = $work/saves
save.namespace = 72756e6e657273000000000000000001
CONF

# The hall's age as kept, after checking everything the save says.
age() {
    python3 - "$work/saves/world.rfsave" <<'PY'
import hashlib, struct, sys
data = open(sys.argv[1], "rb").read()
body, digest = data[:-32], data[-32:]
assert hashlib.sha256(body).digest() == digest, "digest"
at = 0
def take(n):
    global at
    at += n
    return body[at - n:at]
assert take(8) == b"RFSAVE\0\0"
assert struct.unpack("<I", take(4))[0] == 2
high, low = struct.unpack("<QQ", take(16))
assert (high, low) == (0x72756E6E65727300, 1), "namespace"
name = take(struct.unpack("<H", take(2))[0])
assert name == b"hall", name
assert struct.unpack("<I", take(4))[0] == 1
take(16 + 8)
size, fields = struct.unpack("<IH", take(6))
assert (size, fields) == (8, 1)
name = take(take(1)[0])
offset, kind = struct.unpack("<IB", take(5))
assert (name, offset, kind) == (b"ticks", 0, 8), "the age's one field, a u64"
assert struct.unpack("<I", take(4))[0] == 1, "one entity"
high, low, present = struct.unpack("<QQQ", take(24))
# The hall's identity: from hall.scene's resource and its entity's id.
scene = bytes.fromhex("fd3a48dd33db10fae01e4d65d993d8f3")
entity = bytes.fromhex("909c0889a6954893a218b36b9eb97339")
expected = hashlib.sha256(b"rawframe.world.persistent.source.v1" + scene + entity).digest()
assert (high, low) == struct.unpack(">QQ", expected[:16]), "identity"
assert present == 1
ticks = struct.unpack("<Q", take(8))[0]
assert at == len(body), "trailing"
print(ticks)
PY
}

"$server" --config "$work/server.conf" >"$work/first.log"
grep -q '"code":"save_absent"' "$work/first.log"
grep -q '"code":"save_kept"' "$work/first.log"
first=$(age)
"$server" --config "$work/server.conf" >"$work/second.log"
grep -q '"code":"save_loaded".*"updated":1,"created":0' "$work/second.log"
second=$(age)
echo "hall aged $first ticks, then $second"
[ "$first" -gt 0 ] && [ "$second" -gt "$first" ]
echo "hall kept across runs"
