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

# Whether `$1`, a process this shell started, still runs (or is frozen).
# Bash reaps an ended child as soon as it ends and keeps its status for
# `wait`, so the child's pid may already name another process: only the
# shell's job table says whether it is still ours.
live() {
    local pid
    for pid in $(jobs -rp) $(jobs -sp); do
        [ "$pid" = "$1" ] && return 0
    done
    return 1
}

# Every thread of `$1` and of its children (LeakSanitizer's exit check
# runs in a child that traces the others), with its state and what it
# waits in.
threads() {
    local proc task
    ps -o pid,ppid,stat,wchan:30,pcpu,etime,args -p "$1" --ppid "$1" || true
    for proc in "$1" $(ps -o pid= --ppid "$1" 2>/dev/null || true); do
        for task in /proc/"$proc"/task/*; do
            [ -r "$task/status" ] || continue
            echo "  $proc/${task##*/} $(cat "$task/comm" 2>/dev/null || true)" \
                "$(awk '/^State|^TracerPid/ {printf "%s ", $0}' "$task/status" 2>/dev/null || true)" \
                "$(cat "$task/wchan" 2>/dev/null || true)"
        done
    done
}

# Asks `$2`, the `$1` process this test started and has not waited for,
# to stop (going on first if it is frozen) and waits for it. One still
# running after 30 seconds is said, with its threads' states and its log,
# then killed, and the test fails: a process left behind is a fault to
# find, not one to leave to the reaper. One the kill does not end within
# 10 seconds (waiting in the kernel) is said and left, so the test still
# ends.
stopped() {
    local name="$1" pid="$2"
    [ -n "$pid" ] || return 0
    if live "$pid"; then
        kill -CONT "$pid" 2>/dev/null || true
        kill -TERM "$pid" 2>/dev/null || true
    fi
    for _ in $(seq 600); do
        if ! live "$pid"; then
            wait "$pid" || true
            return 0
        fi
        sleep 0.05
    done
    echo "rejoined: the $name did not stop within 30 s of being asked"
    threads "$pid"
    tail -n 40 "$work/$name.log" || true
    if live "$pid"; then
        kill -KILL "$pid" 2>/dev/null || true
    fi
    for _ in $(seq 200); do
        if ! live "$pid"; then
            wait "$pid" || true
            return 1
        fi
        sleep 0.05
    done
    echo "rejoined: the $name did not end within 10 s of SIGKILL"
    threads "$pid"
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
# Fails the test if the server has ended before `$1`: its pid may name
# another process by then, which must not be signaled.
running() {
    live "$server_pid" && return 0
    echo "rejoined: the server ended before $1"
    tail -n 40 "$work/server.log" || true
    exit 1
}
waited '"code":"bots_admitted"'
running "it was frozen"
kill -STOP "$server_pid"
waited '"code":"connection_lost"'
running "it was let go on"
kill -CONT "$server_pid"
wait "$bots_pid" || true
bots_pid=
# A server that does not stop fails the test before its summary, which
# is what passes it.
reap || exit 1
summary="$(grep -o '"code":"bots_summary".*' "$work/bots.log" || true)"
echo "rejoined: $(grep -o '"bots":[0-9]*,"admitted":[0-9]*' <<<"$summary"), $(grep -o '"retried":[0-9]*' <<<"$summary"), \
$(grep -o '"mirroredEntities":[0-9]*' <<<"$summary")"
