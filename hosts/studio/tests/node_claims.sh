#!/usr/bin/env bash
# Collaborators' choices and claims on a UI document's nodes in Studio
# (ADR-0085 section 3, D886): Stalls' UI hosted headless, Studio joined as
# Ada, Ben joined headless. Ben chooses the purse and claims the sign;
# Studio's Hierarchy says the purse is his choice and the sign his lock.
# Ada chooses the board and claims it from the Edit menu; Ben's change to
# it is then refused naming her. Prints what clicked.sh prints, then Ben's
# refusal.
#
#   node_claims.sh <rawframe-studio> <rawframe-author>
set -uo pipefail

here="$(dirname "$0")"
studio=$1
author=$2
work="$(mktemp -d)"
cp -R games/tycoon "$work/hosted"
port=$(python3 "$here/../../../tools/free_port.py")
for each in ada ben; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '%s ada read edit claim\n%s ben read edit claim\n' "$(cat "$work/ada.token")" "$(cat "$work/ben.token")" >"$work/tokens"
"$author" host "$work/hosted/tycoon.game" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null; rm -rf "$work"' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
# Ben waits for Ada to open the UI, chooses the purse and claims the sign,
# then waits for Ada's claim on the board and tries it.
{
    for each in $(seq 10 900); do
        grep -q '"author":"ada","document":"tycoon.ui"' "$work/ben.out" 2>/dev/null && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.2
    done
    echo '{"kind":"authoring.present","id":1,"scene":"tycoon.ui","nodes":["tycoon.purse"],"view":null}'
    echo '{"kind":"authoring.claim","id":2,"scene":"tycoon.ui","node":"tycoon.sign"}'
    for each in $(seq 1000 1900); do
        grep -q '"node":"tycoon.board"' "$work/ben.out" 2>/dev/null && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.2
    done
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.ui","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"ui.set_field","node":"tycoon.board","field":"radius","value":9}]}}'
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ben.token" >"$work/ben.out" 2>&1 &
benning=$!
STUDIO_SETTINGS="studio.join.endpoint = 127.0.0.1:$port
studio.join.pin_file = $work/pin
studio.join.token_file = $work/ada.token" \
    bash "$here/clicked.sh" "$studio" games/tycoon/tycoon.game tycoon.ui @tycoon.ui \
    "until=studio_presence_shown,marked=2" "@purse  [ben]" "@sign  [locked by ben]" @board \
    @Edit "@Claim chosen" "until=studio_said,status=\"claimed tycoon.board" "until=studio_presence_shown,marked=3"
status=$?
wait "$benning"
grep -q '"id":3,"error":{"code":"conflict",.*"author":"ada"' "$work/ben.out" && echo "ben refused ada's board"
exit "$status"
