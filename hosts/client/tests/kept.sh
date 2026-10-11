#!/usr/bin/env bash
# The player's settings kept between runs (D626): a client plays a game,
# clicked at the first points given, with its settings kept in a file of
# its own; then again, clicked at the points after `then`, the file as the
# first left it. Prints the file between the runs, then both runs' logs.
# Run as clicked.sh is.
#
# usage: kept.sh <rawframe-server> <rawframe-client> <client settings>
#                <game> <x>,<y>... then <x>,<y>...
set -uo pipefail

here="$(dirname "$0")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$3" "$work/client.conf"
printf 'ui.kept = %s\n' "$work/settings/settings.json" >>"$work/client.conf"
first=()
second=()
into=first
for point in "${@:5}"; do
    if [ "$point" = then ]; then
        into=second
    elif [ "$into" = first ]; then
        first+=("$point")
    else
        second+=("$point")
    fi
done
bash "$here/clicked.sh" "$1" "$2" "$work/client.conf" "$4" "${first[@]}" >"$work/first.log" 2>&1
status=$?
if grep -q '^skip: no device' "$work/first.log"; then
    cat "$work/first.log"
    exit 0
fi
echo "kept: $(tr -d '\n ' <"$work/settings/settings.json" 2>/dev/null || echo none)"
bash "$here/clicked.sh" "$1" "$2" "$work/client.conf" "$4" "${second[@]}" >"$work/second.log" 2>&1
again=$?
echo "== first run"
cat "$work/first.log"
echo "== second run"
cat "$work/second.log"
[ "$status" -eq 0 ] && [ "$again" -eq 0 ]
