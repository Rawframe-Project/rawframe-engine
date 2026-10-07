#!/usr/bin/env bash
# A scene edited while its game runs (D410): a dedicated server plays a copy
# of runners, watching its sources; an authoring session adds a crate to the
# level and the running World holds it within a second, as the tooling
# endpoint reads it; moved, the World's crate is written rather than made
# again; undone in the session, the World lets it go.
#
# usage: author_live.sh <rawframe-author> <rawframe-server> <repository> <work directory>
set -euo pipefail

author=$1
server=$2
repository=$3
work=$4

native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
rm -rf "$work"
mkdir -p "$work"
# A step that fails says where, with what the server and the author said,
# so a failure seen only in a loaded check can be read.
trap 'echo "failed at line $LINENO"; for each in "$work"/*; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
work="$(native "$work")"
cp -R "$repository/games/runners" "$work/runners"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/token"
body=d0ae39a2-4803-4ee0-9d45-1980875324d0
pose=fdf0050d-881c-4451-b541-754c67d1bbf8
crate=6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30

cat >"$work/server.conf" <<CONF
host.maximum_iterations = 1800
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $work/runners/runners.game
kest.reload_every = 12
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
tooling.endpoint = 127.0.0.1:$port
tooling.token_file = $work/token
CONF
"$server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
running=$!
trap 'kill $running 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [[ -s "$work/fingerprint" ]] && break
    sleep 0.1
done

# How many entities hold a pose now.
poses() {
    echo '{"kind":"tooling.status","id":1}' |
        "$author" connect "127.0.0.1:$port" "$work/fingerprint" "$work/token" |
        grep '"id":1,' | python3 -c '
import json, sys
held = {c["name"]: c["entities"] for c in json.load(sys.stdin)["answer"]["components"]}
print(held.get("rawframe.physics2d.pose", 0))'
}
before=$(poses)

create='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[
{"operation":"scene.create_entity","entity":"'$crate'","name":"crate","place":0},
{"operation":"scene.add_component","entity":"'$crate'","component":"'$body'"},
{"operation":"scene.set_field","entity":"'$crate'","component":"'$body'","field":"width","value":{"real":2}},
{"operation":"scene.add_component","entity":"'$crate'","component":"'$pose'"},
{"operation":"scene.set_field","entity":"'$crate'","component":"'$pose'","field":"x","value":{"real":3.5}}]}'
create=$(tr -d '\n' <<<"$create")
move='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.set_field","entity":"'$crate'","component":"'$pose'","field":"x","value":{"real":7}}]}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.scene","request":'"$create"'}'
    # The running World follows the scene within its reload period.
    sleep 1
    poses >"$work/with_crate"
    # Moved: the World's crate is written, not made again.
    echo '{"kind":"authoring.apply","id":3,"scene":"level.scene","request":'"$move"'}'
    sleep 1
    echo '{"kind":"authoring.undo","id":4,"scene":"level.scene"}'
    echo '{"kind":"authoring.undo","id":5,"scene":"level.scene"}'
    sleep 1
    poses >"$work/without_crate"
    echo '{"kind":"authoring.end","id":6}'
} | "$author" session "$work/runners/runners.game" >"$work/replies"
grep -q '"id":2,"answer":{"kind":"authoring.outcome"' "$work/replies"
grep -q '"id":3,"answer":{"kind":"authoring.outcome"' "$work/replies"
grep -q '"id":5,"answer":{"kind":"authoring.outcome"' "$work/replies"
(( $(cat "$work/with_crate") == before + 1 ))
(( $(cat "$work/without_crate") == before ))
grep -q '"code":"scene_followed".*"created":1' "$work/server.log"
grep -q '"code":"scene_followed".*"created":0,"destroyed":0,"changed":1' "$work/server.log"
grep -q '"code":"scene_followed".*"destroyed":1' "$work/server.log"
! grep -q '"code":"scene_refused"' "$work/server.log"
echo "runners' level followed live"
