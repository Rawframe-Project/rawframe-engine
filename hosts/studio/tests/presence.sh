#!/usr/bin/env bash
# Collaborators shown in Studio (ADR-0085 section 5, D884): Runners' level
# hosted headless, Studio joined as Ada, Ben joined headless. Ben chooses
# the west wall and claims the ground; Studio's title bar shows him, and
# its Hierarchy says the wall is his choice and the ground his lock. Ada
# chooses the east wall and claims it from the Edit menu; Ben's rename of
# it is then refused naming her. Prints what clicked.sh prints, then Ben's
# refusal.
#
#   presence.sh <rawframe-studio> <rawframe-author>
set -uo pipefail

here="$(dirname "$0")"
studio=$1
author=$2
work="$(mktemp -d)"
cp -R games/runners "$work/hosted"
port=$(python3 "$here/../../../tools/free_port.py")
for each in ada ben; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '%s ada read edit claim\n%s ben read edit claim\n' "$(cat "$work/ada.token")" "$(cat "$work/ben.token")" >"$work/tokens"
"$author" host "$work/hosted/runners.game" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null; rm -rf "$work"' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
ground=c5400cf8-4b07-4d16-84c2-951852c66b40
west=87e4430f-6ec1-4301-be72-ce8187494c34
east=0d077e42-123c-4358-b7e3-02e08c02e876
# Ben waits for Ada to open the level, chooses the west wall and claims
# the ground, then waits for Ada's claim on the east wall and tries it.
{
    for each in $(seq 10 900); do
        grep -q '"author":"ada","document":"level.scene"' "$work/ben.out" 2>/dev/null && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.2
    done
    echo '{"kind":"authoring.present","id":1,"scene":"level.scene","entities":["'$west'"],"view":null}'
    echo '{"kind":"authoring.claim","id":2,"scene":"level.scene","entity":"'$ground'"}'
    for each in $(seq 1000 1900); do
        grep -q '"entity":"'$east'"' "$work/ben.out" 2>/dev/null && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.2
    done
    echo '{"kind":"authoring.apply","id":3,"scene":"level.scene","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"'$east'","name":"bens wall"}]}}'
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ben.token" >"$work/ben.out" 2>&1 &
benning=$!
STUDIO_SETTINGS="studio.join.endpoint = 127.0.0.1:$port
studio.join.pin_file = $work/pin
studio.join.token_file = $work/ada.token" \
    bash "$here/clicked.sh" "$studio" games/runners/runners.game level.scene @level.scene \
    "until=studio_presence_shown,marked=2" "@west wall  [ben]" "@ground  [locked by ben]" "@east wall" @Edit "@Claim chosen" \
    "until=studio_said,status=\"claimed east wall" "until=studio_presence_shown,marked=3"
status=$?
wait "$benning"
grep -q '"id":3,"error":{"code":"conflict",.*"author":"ada"' "$work/ben.out" && echo "ben refused ada's east wall"
exit "$status"
