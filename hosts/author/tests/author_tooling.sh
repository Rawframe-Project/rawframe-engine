#!/usr/bin/env bash
# A running dedicated server inspected through its tooling endpoint (D408):
# runners served with an endpoint and a token, rawframe-author connected to
# it reading the World's status twice as it ticks, a verb it does not have
# answered with an error, the hall found by its component and its age read
# by name (D409), and a client with another token refused.
#
# usage: author_tooling.sh <rawframe-author> <rawframe-server> <repository> <work directory>
set -euo pipefail

author=$1
server=$2
repository=$3
work=$4

native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
rm -rf "$work"
mkdir -p "$work"
# A step that fails says where, with what the server and the author said,
# so a failure seen only in a loaded check can be read.
trap 'echo "failed at line $LINENO"; for each in "$work"/*; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
work="$(native "$work")"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
printf '%s\n' "$(python3 -c 'import secrets; print(secrets.token_hex(24))')" >"$work/token"
printf '%s\n' "$(python3 -c 'import secrets; print(secrets.token_hex(24))')" >"$work/other"

cat >"$work/server.conf" <<CONF
host.maximum_iterations = 1200
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $(native "$repository")/games/runners/runners.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [[ -s "$work/fingerprint" ]] && break
    sleep 0.1
done

{
    echo '{"kind":"tooling.status","id":1}'
    sleep 0.5
    echo '{"kind":"tooling.status","id":2}'
    echo '{"kind":"tooling.jump","id":3}'
} | "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/replies" || true
grep -q '"answer":{"kind":"tooling.welcome","protocolVersion":1,"grants":\["inspect"\]}' "$work/replies"
first=$(grep '"id":1,' "$work/replies" | python3 -c 'import json, sys; print(json.load(sys.stdin)["answer"]["tick"])')
second=$(grep '"id":2,' "$work/replies" | python3 -c 'import json, sys; print(json.load(sys.stdin)["answer"]["tick"])')
(( second > first ))
grep '"id":1,' "$work/replies" | grep -q '"world":true'
grep '"id":1,' "$work/replies" | grep -q '"name":"runners.age","entities":1'
grep -q '"id":3,"error":{"code":"unsupported"' "$work/replies"

# The hall found by its component and read by its fields (D409).
echo '{"kind":"tooling.entities","id":1,"component":"runners.age"}' |
    "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/found"
hall=$(grep '"id":1,' "$work/found" | python3 -c 'import json, sys; e = json.load(sys.stdin)["answer"]["entities"]; assert len(e) == 1; print(e[0]["entity"])')
echo '{"kind":"tooling.read_entity","id":1,"entity":"'"$hall"'"}' |
    "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/read"
grep '"id":1,' "$work/read" | python3 -c '
import json, sys
components = {c["name"]: c for c in json.load(sys.stdin)["answer"]["components"]}
assert components["runners.age"]["fields"]["ticks"] > 0, components
assert "rawframe.world.persistent" in components, components
'

# Another token is refused, and the client says so.
! echo '{"kind":"tooling.status","id":1}' |
    "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/other" >"$work/refused"
grep -q '"error":{"code":"unauthenticated"' "$work/refused"
echo "inspected runners through the tooling endpoint"
