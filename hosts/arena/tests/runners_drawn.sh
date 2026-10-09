#!/usr/bin/env bash
# Runs the arena (or the bake, D326) on a configuration that draws
# offscreen (D279) and prints what it logged. A machine with no adapter
# the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is set, as
# the check sets it where lavapipe is: then no device is a failure.
#
# Given a pattern, the run is asked to stop, as an interrupt asks, once a
# line it logged holds the pattern: a test that waits for one record ends
# when the record is there, not when the configuration's iterations are
# (D564). It stops as it would at its end, its summaries logged.
#
# usage: runners_drawn.sh <rawframe-arena or rawframe-bake> <configuration> [<pattern>]
set -uo pipefail

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
"$1" --config "$2" >"$log" 2>&1 &
run=$!
if [ -n "${3:-}" ]; then
    while kill -0 "$run" 2>/dev/null; do
        if grep -qF -- "$3" "$log"; then
            kill -INT "$run" 2>/dev/null
            break
        fi
        sleep 0.2
    done
fi
wait "$run"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
