#!/usr/bin/env bash
# A checkpoint restores: one server runs a Kest game with randomness and
# structural change, capturing at ticks 50 and 100; a second restores the
# tick-50 checkpoint, runs to 100, and captures. The two tick-100 artifacts
# must be the same bytes. Run from the repository root.
#
#   checkpoint.sh <rawframe-server>
set -euo pipefail

server="$1"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

common="host.maximum_iterations = 400
host.iteration_rate = 1000
world.tick_rate = 1000
world.root_seed = 7
kest.game = $PWD/modules/world_kest/tests/game/shooter.game"

printf '%s\ncheckpoint.capture_ticks = 50 100\ncheckpoint.capture_prefix = %s/a-\n' "$common" "$work" >"$work/a.conf"
printf '%s\ncheckpoint.restore = %s/a-50.rfsn\ncheckpoint.capture_ticks = 100\ncheckpoint.capture_prefix = %s/b-\n' \
    "$common" "$work" "$work" >"$work/b.conf"

"$server" --config "$work/a.conf" >"$work/a.log" 2>&1
"$server" --config "$work/b.conf" >"$work/b.log" 2>&1
cat "$work/a.log" "$work/b.log"
if cmp -s "$work/a-100.rfsn" "$work/b-100.rfsn"; then
    echo "checkpoints identical: $(wc -c <"$work/a-100.rfsn") bytes"
else
    echo "checkpoints differ"
    exit 1
fi
