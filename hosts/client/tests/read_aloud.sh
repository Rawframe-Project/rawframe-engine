#!/usr/bin/env bash
# A client's UI as a screen reader reads it (D571): a private D-Bus daemon
# plays the accessibility bus (AT_SPI_BUS_ADDRESS), the client plays a game
# against its server, and once it is admitted and says assistive technology
# reads its UI, its AT-SPI tree is walked from the application's root with
# busctl, one line a node: "a11y: <depth> <role> <name>". Given `press`,
# the first push button found is pressed as a screen reader presses it,
# through AT-SPI's Action interface (D572), and the client plays on a
# while. Given `type <text>`, the text is inserted into the first entry
# found as a screen reader types (D613), through AT-SPI's EditableText
# interface, and the entry's text is read back. Given `slide <value>`, the
# first slider found is set to the value as a screen reader sets one
# (D621), through AT-SPI's Value interface, and its value is read back.
# Then it is asked to stop.
#
# usage: read_aloud.sh <rawframe-server> <rawframe-client> <configuration> <game>
#                      [press | type <text> | slide <value>]
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

# One node's line, then its children's, at most eight deep; the first push
# button and the first entry kept.
button=""
entry=""
slider=""
walk() {
    local path="$1" depth="$2"
    local role label
    role="$(busctl --address="$address" call "$name" "$path" org.a11y.atspi.Accessible GetRoleName 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    label="$(busctl --address="$address" get-property "$name" "$path" org.a11y.atspi.Accessible Name 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    echo "a11y: $depth ${role:-none} ${label}"
    if [ "$role" = "push button" ] && [ -z "$button" ]; then
        button="$path"
    fi
    if [ "$role" = "entry" ] && [ -z "$entry" ]; then
        entry="$path"
    fi
    if [ "$role" = "slider" ] && [ -z "$slider" ]; then
        slider="$path"
    fi
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
if [ "${5:-}" = press ] && [ -n "$button" ]; then
    done="$(busctl --address="$address" call "$name" "$button" org.a11y.atspi.Action DoAction i 0 2>&1)"
    echo "a11y: pressed $button: $done"
    sleep 3
fi
if [ "${5:-}" = type ] && [ -n "$entry" ]; then
    done="$(busctl --address="$address" call "$name" "$entry" org.a11y.atspi.EditableText InsertText isi 0 "$6" \
        "${#6}" 2>&1)"
    echo "a11y: typed into $entry: $done"
    sleep 2
    count="$(busctl --address="$address" get-property "$name" "$entry" org.a11y.atspi.Text CharacterCount 2>/dev/null |
        sed -n 's/^i \(.*\)$/\1/p')"
    read="$(busctl --address="$address" call "$name" "$entry" org.a11y.atspi.Text GetText ii 0 "${count:-0}" 2>/dev/null |
        sed -n 's/^s "\(.*\)"$/\1/p')"
    echo "a11y: the entry reads $read"
fi
if [ "${5:-}" = slide ] && [ -n "$slider" ]; then
    done="$(busctl --address="$address" set-property "$name" "$slider" org.a11y.atspi.Value CurrentValue d "$6" 2>&1)"
    echo "a11y: slid $slider: ${done:-done}"
    sleep 2
    value="$(busctl --address="$address" get-property "$name" "$slider" org.a11y.atspi.Value CurrentValue 2>/dev/null |
        sed -n 's/^d \(.*\)$/\1/p')"
    echo "a11y: the slider reads $value"
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
