#!/usr/bin/env bash
# A port one server holds is not shared with a second (D470): server A
# serves the arena game on a port; server B, told the same port, ends with
# the endpoint taken, rather than sharing it and splitting the datagrams
# sent to it between the two. Run from the repository root.
#
#   taken.sh <rawframe-server>
set -uo pipefail

server="$1"
# Paths as the server reads them: Git's bash on Windows names D:/a as /d/a,
# which only its own tools understand (D237, D501).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
here="$(native "$PWD")"
work="$(mktemp -d)"
trap 'kill "$first" 2>/dev/null; wait "$first" 2>/dev/null; rm -rf "$work"' EXIT
port="$(python3 "$(dirname "$0")/../../../tools/free_port.py")"
config() {
    printf 'host.maximum_iterations = %s\nhost.iteration_rate = 100\nworld.tick_rate = 60\n' "$1"
    printf 'kest.game = %s/games/arena/arena.game\nnetwork.quic.self_signed = true\n' "$here"
    printf 'replication.endpoint = 127.0.0.1:%s\n' "$port"
}
config 6000 >"$work/a.conf"
config 100 >"$work/b.conf"
"$server" --config "$work/a.conf" >"$work/a.log" 2>&1 &
first=$!
for _ in $(seq 1 600); do
    grep -q '"code":"listening"' "$work/a.log" 2>/dev/null && break
    sleep 0.1
done
if ! grep -q '"code":"listening"' "$work/a.log"; then
    echo "server A never listened"
    cat "$work/a.log"
    exit 1
fi
"$server" --config "$work/b.conf" >"$work/b.log" 2>&1
status=$?
cat "$work/b.log"
if [ "$status" -ne 0 ] && grep -q 'the endpoint is taken' "$work/b.log"; then
    echo "server B refused the port A holds"
fi
