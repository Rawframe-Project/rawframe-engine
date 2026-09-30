#!/usr/bin/env bash
# Runs the arena (or the bake, D326) on a configuration that draws
# offscreen (D279) and prints what it logged. A machine with no adapter
# the configuration allows skips, unless RAWFRAME_REQUIRE_GPU is set, as
# the check sets it where lavapipe is: then no device is a failure.
#
# usage: runners_drawn.sh <rawframe-arena or rawframe-bake> <configuration>
set -uo pipefail

output=$("$1" --config "$2" 2>&1)
status=$?
if grep -q '"device_unavailable"' <<<"$output" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
printf '%s\n' "$output"
exit "$status"
