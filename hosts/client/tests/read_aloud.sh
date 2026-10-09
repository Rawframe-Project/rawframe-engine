#!/usr/bin/env bash
# A client's UI as a screen reader reads it (D571): a private D-Bus daemon
# plays the accessibility bus (AT_SPI_BUS_ADDRESS), the client plays a game
# against its server, and once it is admitted and says assistive technology
# reads its UI, its AT-SPI tree is walked from the application's root with
# busctl, one line a node: "a11y: <depth> <role> <name>". Then the client
# is asked to stop.
#
# usage: read_aloud.sh <rawframe-server> <rawframe-client> <configuration> <game>
set -uo pipefail
here="$(dirname "$0")"
log="$(mktemp)"
work="$(mktemp -d)"
bus=""
trap '[ -n "$bus" ] && kill "$bus" 2>/dev/null; rm -rf "$log" "$work"' EXIT

started="$(dbus-daemon --session --fork --print-address=1 --print-pid=1)" || {
    echo "skip: no dbus-daemon"
    exit 0
}
address="$(head -1 <<<"$started")"
bus="$(tail -1 <<<"$started")"

grep '^content\.root' "$3" >"$work/content.conf" || true
AT_SPI_BUS_ADDRESS="$address" RAWFRAME_PLAY_WORK="$work" \
    bash "$here/../../bots/tests/play.sh" "$1" "$2" 0 1 36000 "$4" "$work/content.conf" "$3" >"$log" 2>&1 &
play=$!

# Admitted, its UI drawn a while, and read.
for _ in $(seq 600); do
    grep -q '"code":"bots_admitted"' "$work/bots-1.log" 2>/dev/null &&
        grep -q '"code":"accessibility_\(ready\|unavailable\)"' "$work/bots-1.log" && break
    sleep 0.1
done
sleep 2
name="$(busctl --address="$address" list --no-legend 2>/dev/null | awk '$3 == "rawframe-client" { print $1; exit }')"

# One node's line, then its children's, at most eight deep.
walk() {
    local path="$1" depth="$2"
    local role label
    role="$(busctl --address="$address" call "$name" "$path" org.a11y.atspi.Accessible GetRoleName 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    label="$(busctl --address="$address" get-property "$name" "$path" org.a11y.atspi.Accessible Name 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    echo "a11y: $depth ${role:-none} ${label}"
    [ "$depth" -ge 8 ] && return
    local child
    for child in $(busctl --address="$address" call "$name" "$path" org.a11y.atspi.Accessible GetChildren 2>/dev/null |
        grep -o '"/[^"]*"' | tr -d '"'); do
        walk "$child" $((depth + 1))
    done
}
if [ -n "$name" ]; then
    walk /org/a11y/atspi/accessible/root 0
else
    echo "a11y: no client on the bus"
fi

kill -INT "$(cat "$work/bots-1.pid" 2>/dev/null)" 2>/dev/null
wait "$play"
status=$?
if grep -q '"device_unavailable"' "$log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
cat "$log"
exit "$status"
