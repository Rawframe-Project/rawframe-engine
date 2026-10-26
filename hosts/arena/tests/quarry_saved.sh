#!/usr/bin/env bash
# Quarry keeps its land between runs: four bots dig and
# build on a land drawn from one seed for five seconds, and the arena saves
# it as it stops; a dedicated server with another seed loads that save and
# makes no land of its own, so the land it keeps when it stops is the very
# one it loaded, byte for byte. Run from the repository root.
#
#   quarry_saved.sh <rawframe-arena> <rawframe-server>
set -euo pipefail

arena="$1"
server="$2"
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
here="$(native "$PWD")"
work="$(native "$(mktemp -d)")"
trap 'rm -rf "$work"' EXIT

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 600
host.iteration_rate = 120
world.tick_rate = 60
world.root_seed = 7
kest.game = $here/games/quarry/quarry.game
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
save.directory = $work/saves
save.namespace = 71756172727900000000000000000001
world.overload_ms = 0
# Saves kept as it stops are synced to a disk a loaded machine shares.
host.shutdown_budget_ms = 30000
CONF
cat >"$work/server.conf" <<CONF
host.maximum_iterations = 120
host.iteration_rate = 120
world.tick_rate = 60
world.root_seed = 8
kest.game = $here/games/quarry/quarry.game
save.directory = $work/saves
save.namespace = 71756172727900000000000000000001
world.overload_ms = 0
# Saves kept as it stops are synced to a disk a loaded machine shares.
host.shutdown_budget_ms = 30000
CONF

# A record a run's log must hold, or which it lacked and the log's end:
# a run starved on a loaded machine says where it stopped.
need() {
    grep -q "\"code\":\"$1\"" "$2" && return
    echo "never saw $1 in $(basename "$2")"
    tail -5 "$2"
    exit 1
}

"$arena" --config "$work/arena.conf" >"$work/first.log"
need save_absent "$work/first.log"
need save_kept "$work/first.log"
cp "$work/saves/world.rfsave" "$work/first.rfsave"

"$server" --config "$work/server.conf" >"$work/second.log"
need save_loaded "$work/second.log"
need save_kept "$work/second.log"

# The land's blocks as kept, after checking the digest.
blocks() {
    python3 - "$1" <<'PY'
import hashlib, struct, sys
data = open(sys.argv[1], "rb").read()
assert hashlib.sha256(data[:-32]).digest() == data[-32:], "digest"
at = 8 + 4 + 16
name = data[at + 2:at + 2 + struct.unpack("<H", data[at:at + 2])[0]]
assert name == b"land", name
print(len(data))
PY
}
first=$(blocks "$work/first.rfsave")
second=$(blocks "$work/saves/world.rfsave")
if ! cmp -s "$work/first.rfsave" "$work/saves/world.rfsave"; then
    echo "the land kept by the second run is not the one it loaded ($first and $second bytes)"
    exit 1
fi
grep -o '"code":"save_loaded".*' "$work/second.log" | head -1
echo "land kept across runs, $first bytes"
