#!/usr/bin/env bash
# A server started to stop with its parent (D646) stops in order when the
# parent is killed, as a test's runner kills a tool that started it: a
# shell starts the server, told its identity, and is killed with SIGKILL;
# the server, set to run a minute, stops at once. On Linux and macOS; a
# shell's identity on Windows is not the system's, and the launcher's
# test (D649) proves it there.
#
# usage: orphaned.sh <rawframe-server> <work directory>
set -euo pipefail

server=$1
work=$2
case "$(uname -s)" in
Linux | Darwin) ;;
*)
    echo "skip: not Linux or macOS"
    exit 0
    ;;
esac
rm -rf "$work"
mkdir -p "$work"
cat >"$work/server.conf" <<CONF
host.maximum_iterations = 6000
host.iteration_rate = 100
world.tick_rate = 100
CONF
# The shell is the server's parent and waits on it; its identity is $$.
sh -c 'RAWFRAME_PARENT=$$ "$0" --config "$1" >"$2" 2>&1 & echo $! >"$3"; wait' \
    "$server" "$work/server.conf" "$work/server.log" "$work/server.pid" &
parent=$!
for _ in $(seq 100); do
    grep -q '"state":"active"' "$work/server.log" 2>/dev/null && break
    sleep 0.1
done
kill -9 "$parent"
pid=$(cat "$work/server.pid")
for _ in $(seq 100); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid"
    echo "the server outlived its parent"
    exit 1
fi
grep -q '"code":"stopped"' "$work/server.log"
echo "stopped with its parent"
