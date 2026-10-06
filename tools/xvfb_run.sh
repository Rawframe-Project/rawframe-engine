#!/usr/bin/env bash
# Runs a command under an X server of its own (D428). Xvfb picks a display
# number no server holds and says which (-displayfd), so tests started at
# the same moment never share one: xvfb-run -a chooses by lock files and
# could give two of them the same display, one test's clicks and keys then
# reaching the other's window.
#
# usage: xvfb_run.sh [-s "<Xvfb arguments>"] <command> [<argument>...]
set -uo pipefail

arguments="-screen 0 1280x1024x24"
if [ "${1:-}" = "-s" ]; then
    arguments="$2"
    shift 2
fi
told="$(mktemp)"
trap 'rm -f "$told"' EXIT
# shellcheck disable=SC2086
Xvfb -displayfd 3 -nolisten tcp $arguments 3>"$told" >/dev/null 2>&1 &
server=$!
for _ in $(seq 1 400); do
    if [ -s "$told" ]; then
        break
    fi
    if ! kill -0 "$server" 2>/dev/null; then
        echo "xvfb_run.sh: Xvfb did not start" >&2
        exit 1
    fi
    sleep 0.05
done
number="$(tr -d '[:space:]' <"$told")"
if [ -z "$number" ]; then
    echo "xvfb_run.sh: Xvfb named no display" >&2
    kill "$server" 2>/dev/null
    exit 1
fi
DISPLAY=":$number" "$@"
status=$?
kill "$server" 2>/dev/null
wait "$server" 2>/dev/null
exit "$status"
