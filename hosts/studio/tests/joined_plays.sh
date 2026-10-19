#!/usr/bin/env bash
# Studio joined to a shared session's host plays what it cooked there
# (D905): Runners hosted by `rawframe-author host`, Studio joining as Ada,
# Cook cooking the game on the host into Ada's own directory there, then
# Play bringing that cook into Studio's play directory and playing it, the
# client's settings naming no content of their own. Prints what plays.sh
# prints, then whether the host holds the cook.
#
#   joined_plays.sh <rawframe-studio> <rawframe-server> <rawframe-client>
#                   <rawframe-author> <rawframe-cook> <client settings> <click>...
set -uo pipefail

here="$(dirname "$0")"
author=$4
cook=$5
work="$(mktemp -d)"
cp -R games/runners "$work/hosted"
port=$(python3 "$here/../../../tools/free_port.py")
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/ada.token"
printf '%s ada read edit cook\n' "$(cat "$work/ada.token")" >"$work/tokens"
"$author" host "$work/hosted/runners.game" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" --cooks "$work/cooks" --cook "$cook" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null; rm -rf "$work"' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
STUDIO_COOK="$cook" STUDIO_SETTINGS="studio.join.endpoint = 127.0.0.1:$port
studio.join.pin_file = $work/pin
studio.join.token_file = $work/ada.token" \
    bash "$here/plays.sh" "$1" "$2" "$3" games/runners/runners.game /dev/null "$6" "${@:7}"
status=$?
[ -s "$work/cooks/ada/content/content.manifest" ] && echo "cooked on the host"
exit "$status"
