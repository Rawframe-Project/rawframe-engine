#!/usr/bin/env bash
# What a collaborator has chosen outlined in a joined Studio's preview, in
# their color (D907): Runners hosted by `rawframe-author host`, Ben joined
# headless with the west wall chosen in the level, Studio joining as Ada,
# cooking at the host, playing, and previewing the level, its preview
# outlining Ada's choice and Ben's. Prints what plays.sh prints.
#
#   joined_outlines.sh <rawframe-studio> <rawframe-server> <rawframe-client>
#                      <rawframe-author> <rawframe-cook> <client settings> <click>...
set -uo pipefail

here="$(dirname "$0")"
author=$4
cook=$5
work="$(mktemp -d)"
cp -R games/runners "$work/hosted"
port=$(python3 "$here/../../../tools/free_port.py")
for each in ada ben; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '%s ada read edit cook\n%s ben read edit\n' "$(cat "$work/ada.token")" "$(cat "$work/ben.token")" >"$work/tokens"
"$author" host "$work/hosted/runners.game" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" --cooks "$work/cooks" --cook "$cook" >"$work/host.log" 2>&1 &
hosting=$!
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
# Ben says he has the west wall chosen, then stays until Studio is done.
wall=87e4430f-6ec1-4301-be72-ce8187494c34
{
    echo '{"kind":"authoring.present","id":1,"scene":"level.scene","entities":["'$wall'"],"view":null}'
    while [ ! -e "$work/done" ]; do
        sleep 0.2
    done
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ben.token" >"$work/ben.out" 2>&1 &
trap 'touch "$work/done"; kill "$hosting" 2>/dev/null; sleep 0.5; rm -rf "$work"' EXIT
STUDIO_COOK="$cook" STUDIO_SETTINGS="studio.join.endpoint = 127.0.0.1:$port
studio.join.pin_file = $work/pin
studio.join.token_file = $work/ada.token" \
    bash "$here/plays.sh" "$1" "$2" "$3" games/runners/runners.game /dev/null "$6" "${@:7}"
status=$?
grep -q '"id":1,"answer":{"kind":"authoring.present' "$work/ben.out" && echo "ben chose the west wall"
exit "$status"
