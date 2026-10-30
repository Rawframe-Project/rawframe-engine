#!/usr/bin/env bash
# A connection lost without the server's word is asked for again: a
# bot is admitted, then its server is frozen past the connection's idle time
# (a second here), as a phone's suspended client comes back to one gone;
# the bot says it lost its connection, the server is let go on, and the bot
# is admitted again, its World mirrored from nothing. Run from the
# repository root.
#
#   rejoined.sh <rawframe-server> <rawframe-bots>
set -euo pipefail

server="$1"
bots="$2"
here="$PWD"
work="$(mktemp -d)"
server_pid=
bots_pid=

# Asks `$2`, the `$1` process this test started and has not waited for,
# to stop (going on first if it is frozen) and waits for it. One still
# running after 30 seconds is said, with its threads' states and its log,
# then killed, and the test fails: a process left behind is a fault to
# find, not one to leave to the reaper.
stopped() {
    local name="$1" pid="$2"
    [ -n "$pid" ] || return 0
    kill -CONT "$pid" 2>/dev/null || true
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 600); do
        # Not yet waited for, an ended process stays a zombie, so its pid
        # names no other process.
        case "$(ps -o stat= -p "$pid" 2>/dev/null || true)" in
            "" | Z*)
                wait "$pid" || true
                return 0
                ;;
        esac
        sleep 0.05
    done
    echo "rejoined: the $name did not stop within 30 s of being asked"
    ps -o pid,stat,wchan,etime,args -p "$pid" || true
    for task in /proc/"$pid"/task/*; do
        if [ -r "$task/stat" ]; then
            echo "  thread $(cat "$task/comm") $(sed 's/.*) //' "$task/stat" | cut -d' ' -f1)" \
                "$(cat "$task/wchan" 2>/dev/null || true)"
        fi
    done
    tail -n 40 "$work/$name.log" || true
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" || true
    return 1
}

# Stops and waits for every process this test started, on every path out
# of it; false if one had to be killed.
reap() {
    local ok=0
    stopped bots "$bots_pid" || ok=1
    bots_pid=
    stopped server "$server_pid" || ok=1
    server_pid=
    return "$ok"
}
trap 'reap || true; rm -rf "$work"' EXIT
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"

cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $here/games/arena/arena.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
network.quic.idle_timeout_ms = 1000
network.quic.keep_alive_ms = 300
replication.endpoint = 127.0.0.1:$port
CONF
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = 1200
host.iteration_rate = 120
kest.plan_only = true
kest.game = $here/games/arena/arena.game
network.quic.pin_file = $work/fingerprint
network.quic.idle_timeout_ms = 1000
network.quic.keep_alive_ms = 300
bots.count = 1
bots.endpoint = 127.0.0.1:$port
bots.retry_ms = 200
CONF

"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 200); do
    [ -s "$work/fingerprint" ] && break
    sleep 0.05
done
"$bots" --config "$work/bots.conf" >"$work/bots.log" 2>&1 &
bots_pid=$!
waited() {
    for _ in $(seq 400); do
        grep -q "$1" "$work/bots.log" 2>/dev/null && return 0
        sleep 0.05
    done
    echo "rejoined: never saw $1"
    cat "$work/bots.log"
    exit 1
}
waited '"code":"bots_admitted"'
kill -STOP "$server_pid"
waited '"code":"connection_lost"'
kill -CONT "$server_pid"
wait "$bots_pid" || true
bots_pid=
# A server that does not stop fails the test before its summary, which
# is what passes it.
reap || exit 1
summary="$(grep -o '"code":"bots_summary".*' "$work/bots.log" || true)"
echo "rejoined: $(grep -o '"bots":[0-9]*,"admitted":[0-9]*' <<<"$summary"), $(grep -o '"retried":[0-9]*' <<<"$summary"), \
$(grep -o '"mirroredEntities":[0-9]*' <<<"$summary")"
