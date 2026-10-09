#!/usr/bin/env bash
# Studio as a screen reader reads and presses it (D575): a private D-Bus
# daemon plays the accessibility bus (AT_SPI_BUS_ADDRESS), Studio is opened
# on a copy of a game, and once it is shown and says assistive technology
# reads it, its AT-SPI tree is walked from the application's root with
# busctl, one line a node: "a11y: <depth> <role> <name>". Each name given
# after the scene is then found among the push buttons and pressed through
# AT-SPI's Action interface, a few seconds apart; a last argument `until=`
# and a status waits, up to a minute, for Studio to say it. Then it is asked
# to stop, and its log printed. Run under an X server, from the repository
# root.
#
#   read_aloud.sh <rawframe-studio> <game description> <scene> [<button name>...] [until=<status>]
set -uo pipefail
work="$(mktemp -d)"
bus=""
trap '[ -n "$bus" ] && kill "$bus" 2>/dev/null; rm -rf "$work"' EXIT

started="$(dbus-daemon --session --fork --print-address=1 --print-pid=1)" || {
    echo "skip: no dbus-daemon"
    exit 0
}
address="$(head -1 <<<"$started")"
bus="$(tail -1 <<<"$started")"

cp -R "$(dirname "$2")" "$work/game"
cat >"$work/studio.conf" <<CONF
host.maximum_iterations = 14400
host.iteration_rate = 120
render.device = any
studio.game = $work/game/$(basename "$2")
CONF
AT_SPI_BUS_ADDRESS="$address" "$1" --config "$work/studio.conf" >"$work/log" 2>&1 &
studio=$!

# Shown, and read.
for _ in $(seq 600); do
    grep -q '"code":"studio_shown"' "$work/log" &&
        grep -q '"code":"studio_accessibility_\(ready\|unavailable\)"' "$work/log" && break
    sleep 0.1
done
sleep 2
name="$(busctl --address="$address" list --no-legend 2>/dev/null | awk '$3 == "rawframe-studio" { print $1; exit }')"

# One node's line, then its children's, at most eight deep, printed when
# PRINT is set; the first push button named `wanted` kept in `found`.
wanted=""
found=""
walk() {
    local path="$1" depth="$2"
    local role label
    role="$(busctl --address="$address" call "$name" "$path" org.a11y.atspi.Accessible GetRoleName 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    label="$(busctl --address="$address" get-property "$name" "$path" org.a11y.atspi.Accessible Name 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    [ -n "${PRINT:-}" ] && echo "a11y: $depth ${role:-none} ${label}"
    if [ "$role" = "push button" ] && [ "$label" = "$wanted" ] && [ -z "$found" ]; then
        found="$path"
    fi
    [ "$depth" -ge 8 ] && return
    [ -z "${PRINT:-}" ] && [ -n "$found" ] && return
    local child
    for child in $(busctl --address="$address" call "$name" "$path" org.a11y.atspi.Accessible GetChildren 2>/dev/null |
        grep -o '"/[^"]*"' | tr -d '"'); do
        walk "$child" $((depth + 1))
    done
}
if [ -n "$name" ]; then
    PRINT=1 walk /org/a11y/atspi/accessible/root 0
else
    echo "a11y: no Studio on the bus"
fi

until=""
for asked in "${@:4}"; do
    if [[ "$asked" == until=* ]]; then
        until="${asked#until=}"
        continue
    fi
    [ -z "$name" ] && break
    wanted="$asked"
    found=""
    walk /org/a11y/atspi/accessible/root 0
    if [ -z "$found" ]; then
        echo "a11y: no button $asked"
        continue
    fi
    done="$(busctl --address="$address" call "$name" "$found" org.a11y.atspi.Action DoAction i 0 2>&1)"
    echo "a11y: pressed $asked: $done"
    sleep 2
done
if [ -n "$until" ]; then
    for _ in $(seq 600); do
        grep '"code":"studio_said"' "$work/log" | grep -q "\"status\":\"$until\"" && break
        sleep 0.1
    done
fi

kill -INT "$studio" 2>/dev/null
wait "$studio"
status=$?
if grep -q '"device_unavailable"' "$work/log" && [ -z "${RAWFRAME_REQUIRE_GPU:-}" ]; then
    echo "skip: no device"
    exit 0
fi
if cmp -s "$(dirname "$2")/$3" "$work/game/$3"; then
    echo "$3 written: no"
else
    echo "$3 written: yes"
fi
cat "$work/log"
exit "$status"
