#!/usr/bin/env bash
# A game's scripts debugged through the tooling endpoint (D460): a dedicated
# server plays the plaza with the debug grant; a breakpoint at plaza's
# stroll system stops the game in its tick, where the stopped frame names
# the function and its arguments, every other verb is refused, and the
# breakpoints are taken out; told to carry on, the game runs again. Stopped
# again, with its debugger still there, the server asked to stop carries on
# and drains, rather than waiting for the debugger.
#
# usage: author_debugs.sh <rawframe-author> <rawframe-server> <repository>
#                         <content settings> <work directory>
set -euo pipefail

author=$1
server=$2
repository=$3
content=$4
work=$5

rm -rf "$work"
mkdir -p "$work"
# A step that fails says where, with what the debugger and the server
# said, so a failure seen only in a loaded check can be read.
trap 'echo "failed at line $LINENO"; cat "$work"/replies "$work"/held 2>/dev/null; tail -40 "$work"/server.log 2>/dev/null' ERR
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/token"
cat "$content" >"$work/server.conf"
cat >>"$work/server.conf" <<CONF
host.maximum_iterations = 6000
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $repository/games/plaza/plaza.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
tooling.grants = inspect debug
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running 2>/dev/null || true' EXIT
for _ in $(seq 200); do
    [[ -s "$work/fingerprint" ]] && break
    sleep 0.1
done

{
    echo '{"kind":"debug.break","id":1,"functions":["stroll","nowhere"]}'
    sleep 2
    echo '{"kind":"debug.status","id":2}'
    echo '{"kind":"tooling.status","id":3}'
    echo '{"kind":"debug.break","id":4,"functions":[]}'
    echo '{"kind":"debug.continue","id":5}'
    sleep 1
    echo '{"kind":"debug.status","id":6}'
    echo '{"kind":"tooling.status","id":7}'
    echo '{"kind":"debug.continue","id":8}'
    echo '{"kind":"debug.break","id":9,"functions":[1]}'
} | "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/replies" || true
grep -q '"grants":\["inspect","debug"\]' "$work/replies"
grep -q '"id":1,"answer":{"kind":"debug.broken","found":1}' "$work/replies"
grep '"id":2,' "$work/replies" |
    grep -q '"answer":{"kind":"debug.status","stopped":true,"stops":[1-9][0-9]*,"frames":\[{"function":"plaza.stroll","locals":\[{"name":"count","value":"[0-9]*"}'
grep -q '"id":3,"error":{"code":"stopped"' "$work/replies"
grep -q '"id":4,"answer":{"kind":"debug.broken","found":0}' "$work/replies"
grep -q '"id":5,"answer":{"kind":"debug.continued"}' "$work/replies"
grep -q '"id":6,"answer":{"kind":"debug.status","stopped":false,"stops":1,"frames":\[\]}' "$work/replies"
grep -q '"id":7,"answer":{"kind":"tooling.status","tick":' "$work/replies"
grep -q '"id":8,"error":{"code":"stopped"' "$work/replies"
grep -q '"id":9,"error":{"code":"malformed"' "$work/replies"

# Stopped again, the debugger staying, and the server asked to stop.
{
    echo '{"kind":"debug.break","id":10,"functions":["plaza.stroll"]}'
    sleep 1
    echo '{"kind":"debug.status","id":11}'
    sleep 8
} | "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" >"$work/held" &
holding=$!
for _ in $(seq 100); do
    grep -q '"id":11,' "$work/held" 2>/dev/null && break
    sleep 0.1
done
grep -q '"id":11,"answer":{"kind":"debug.status","stopped":true,' "$work/held"
kill -TERM $running
for _ in $(seq 50); do
    kill -0 $running 2>/dev/null || break
    sleep 0.1
done
if kill -0 $running 2>/dev/null; then
    echo "the server waited for its debugger after it was asked to stop"
    exit 1
fi
wait $holding || true
grep -q '"id":10,"answer":{"kind":"debug.broken","found":1}' "$work/held"
grep -q '"exit":"clean_stop"' "$work/server.log"
echo "stopped the plaza at its stroll system and carried on through the tooling endpoint"
