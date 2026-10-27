#!/usr/bin/env bash
# Studio joined to a shared session's host: Runners'
# level hosted by `rawframe-author host`, Studio joining as Ada with the
# token the host's owner gave her, and Ben joining headless. Studio says
# what it has open, which Ben waits to hear; Ben renames the ground; Studio
# hears his change and shows the level as the host now holds it, the row
# Ben named found in its Hierarchy by that name, the title bar's Session
# pressed for the session's card, of Ada alone as Ben has nothing open;
# then Cook cooks the game
# on the host, into Ada's own directory there. Prints what
# clicked.sh prints.
#
#   joined.sh <rawframe-studio> <rawframe-author> <rawframe-cook>
set -uo pipefail

here="$(dirname "$0")"
studio=$1
author=$2
cook=$3
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
trap 'kill "$hosting" 2>/dev/null; rm -rf "$work"' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
ground=c5400cf8-4b07-4d16-84c2-951852c66b40
# Ben asks who is present until Studio has said it has the level open,
# then renames the ground.
{
    for each in $(seq 2 900); do
        grep -q '"author":"ada","document":"level.scene"' "$work/ben.out" 2>/dev/null && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.2
    done
    echo '{"kind":"authoring.apply","id":1,"scene":"level.scene","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"'$ground'","name":"bens ground"}]}}'
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ben.token" >"$work/ben.out" 2>&1 &
STUDIO_SETTINGS="studio.join.endpoint = 127.0.0.1:$port
studio.join.pin_file = $work/pin
studio.join.token_file = $work/ada.token" \
    bash "$here/clicked.sh" "$studio" games/runners/runners.game level.scene @level.scene \
    "until=studio_host_heard,author=\"ben\"" wait=1 @Session "until=studio_said,status=\"in this session: 1\"" \
    "@bens ground" @Cook "until=studio_said,status=\"cooked"
status=$?
grep -q '"id":1,"answer":{"kind":"authoring.outcome"' "$work/ben.out" && echo "ben's rename kept"
grep -q '"bens ground"' "$work/hosted/level.scene" && echo "the host's level holds it"
[ -s "$work/cooks/ada/content/cook.receipt" ] && echo "cooked on the host"
exit "$status"
