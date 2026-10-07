#!/usr/bin/env bash
# What a ray meets in a running World, and where it was authored (D456): a
# dedicated server plays the plaza with its tooling endpoint, and a ray cast
# from in front of the gate's post at it names the post and its id in the
# gate scene; a ray up into the sky meets nothing; and a pick out of form
# is refused.
#
# usage: author_picks.sh <rawframe-author> <rawframe-server> <repository>
#                        <content settings> <work directory>
set -euo pipefail

author=$1
server=$2
repository=$3
content=$4
work=$5

rm -rf "$work"
mkdir -p "$work"
# A step that fails says where, with what the server and the author said,
# so a failure seen only in a loaded check can be read.
trap 'echo "failed at line $LINENO"; for each in "$work"/*; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/token"
cat "$content" >"$work/server.conf"
cat >>"$work/server.conf" <<CONF
host.maximum_iterations = 1200
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $repository/games/plaza/plaza.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running 2>/dev/null || true' EXIT
for _ in $(seq 200); do
    [[ -s "$work/fingerprint" ]] && break
    sleep 0.1
done

{
    echo '{"kind":"tooling.pick","id":1,"origin":[-30,2.5,-20],"toward":[0,0,-20]}'
    echo '{"kind":"tooling.pick","id":2,"origin":[0,50,0],"toward":[0,40,0]}'
    echo '{"kind":"tooling.pick","id":3,"origin":[0,50,0]}'
} | "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/replies" || true
grep '"id":1,' "$work/replies" |
    grep -q '"answer":{"kind":"tooling.picked","entity":"[0-9]*:[0-9]*","point":\[-30,2.5,-29.[0-9]*\],"scene":"gate.scene","source":"6c632219-53a9-4af4-8781-bb2a8f4fa53b"}'
grep -q '"id":2,"answer":{"kind":"tooling.picked","entity":null}' "$work/replies"
grep -q '"id":3,"error":{"code":"malformed"' "$work/replies"
echo "picked the plaza's gate post through the tooling endpoint"
