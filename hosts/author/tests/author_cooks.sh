#!/usr/bin/env bash
# A session cooks its game as a long-running operation (SPEC-0040, D502):
# the plaza cooked while a record is answered, its progress told source by
# source and its reply when it ends; a second cook while one runs refused
# by the session's limit; a cook asked to stop answering cancelled, which
# is no error; a cook the tool refuses (into the game's own directory)
# answering its error; and a cook running when the input ends stopped and
# answering before the session does. Its sources listed, each as the
# cook left it (D799).
#
# usage: author_cooks.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
# A check that fails says which, and what the session answered, so a run
# on a machine of another system can be read (D813).
trap 'echo "author_cooks: failed at line $LINENO"; grep -v "\"kind\":\"authoring.progress\"" "$work/replies" 2>/dev/null | cut -c1-600' ERR
rm -rf "$work"
mkdir -p "$work"
game="$(native "$repository")/games/plaza/plaza.game"

reply() { grep "\"id\":$1," "$work/replies"; }

# The plaza cooked, a second cook refused while it runs, and a describe
# answered between them; the session ends once the first has answered.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.assets","id":10}'
    echo '{"kind":"authoring.thumbnail","id":12,"asset":"paving.png","size":64}'
    echo '{"kind":"authoring.cook","id":2,"output":"'"$work/content"'","cache":"'"$work/cache"'"}'
    echo '{"kind":"authoring.cook","id":3,"output":"'"$work/other"'","cache":null}'
    echo '{"kind":"authoring.cancel","id":4,"operation":"none"}'
    # Each wait ends as the reply comes; four minutes only bounds a cook
    # on a sanitized tree under load, which took more than thirty seconds
    # (D507).
    for _ in $(seq 2400); do
        grep -q '"id":2,"answer"\|"id":2,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.assets","id":11}'
    echo '{"kind":"authoring.thumbnail","id":13,"asset":"paving.png","size":64}'
    echo '{"kind":"authoring.thumbnail","id":14,"asset":"gate.scene","size":64}'
    echo '{"kind":"authoring.asset_references","id":15,"asset":"paving.png"}'
    echo '{"kind":"authoring.asset_references","id":16,"asset":"crate.gltf"}'
    echo '{"kind":"authoring.end","id":5}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true
steps=$(grep -c '"kind":"authoring.progress","id":2,' "$work/replies")
reply 2 | grep -q '"answer":{"kind":"authoring.cooked","cooked":'"$steps"',"reused":0}'
grep -q '"id":2,"step":'"$steps"',"steps":'"$steps"',' "$work/replies"
reply 3 | grep -q '"code":"limit_exceeded"'
reply 4 | grep -q '"found":false'
reply 5 | grep -q '"authoring.ended"'
# Each source listed with its folder, importer, and identity, unknown to the
# cook until it cooked, then cooked (D799).
reply 10 | grep -q '{"path":"blade/rig.rfanim","folder":"blade","importer":"rawframe.animation","id":"[0-9a-f]\{32\}","cook":"unknown"}'
reply 10 | grep -q '{"path":"gate.scene","folder":"","importer":"rawframe.scene","id":"[0-9a-f]\{32\}","cook":"unknown"}'
[[ $(reply 11 | grep -o '"cook":"cooked"' | wc -l) -eq $steps ]]
reply 11 | grep -q '{"path":"gate.scene","folder":"","importer":"rawframe.scene","id":"[0-9a-f]\{32\}","cook":"cooked"}'
# A texture's thumbnail once the game has cooked, the first level no wider
# than 64 pixels, in the cooked object named; a scene has none (D801).
reply 12 | grep -q '"code":"target_not_found"'
reply 13 | grep -q '"answer":{"kind":"authoring.thumbnail","asset":"paving.png","object":"[^"]*/content/objects/[0-9a-f]\{64\}","level":[1-9][0-9]*,"width":64,"height":64}'
reply 14 | grep -q '"code":"unsupported_operation"'
# The paving read by the game's description, which declares it; the crate's
# glTF reading its picture, which has no sidecar of its own, and read by the
# description (D803).
reply 15 | grep -q '"answer":{"kind":"authoring.asset_references","asset":"paving.png","uses":\[\],"usedBy":\[[^]]*"plaza.game"'
reply 16 | grep -q '"uses":\["crate.png"\],"usedBy":\["plaza.game"\]}'
[[ -s "$work/content/content.manifest" && -s "$work/content/cook.receipt" && -s "$work/content/cook.log" ]]

# Again from the cache: every source reused. Then a cook stopped as asked,
# one into the game's own directory, and one the input's end stops.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.cook","id":6,"output":"'"$work/again"'","cache":"'"$work/cache"'"}'
    for _ in $(seq 2400); do
        grep -q '"id":6,"answer"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.cook","id":"stopped","output":"'"$work/stopped"'","cache":null}'
    echo '{"kind":"authoring.cancel","id":7,"operation":"stopped"}'
    for _ in $(seq 2400); do
        grep -q '"id":"stopped","cancelled"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.cook","id":8,"output":"'"$(native "$repository")/games/plaza/content"'","cache":null}'
    for _ in $(seq 2400); do
        grep -q '"id":8,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.cook","id":9,"output":"'"$work/ended"'","cache":null}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true
reply 6 | grep -q '"answer":{"kind":"authoring.cooked","cooked":0,"reused":'"$steps"'}'
reply 7 | grep -q '"found":true'
grep -q '"id":"stopped","cancelled":{"reason":"requested"}' "$work/replies"
[[ ! -e "$work/stopped/content.manifest" ]]
reply 8 | grep -q '"error":{"code":"validation_failed",'
grep -q '"id":9,"cancelled":{"reason":"ended"}\|"id":9,"answer":{"kind":"authoring.cooked"' "$work/replies"
grep -q '"id":null,"answer":{"kind":"authoring.ended"}' "$work/replies"
[[ ! -e "$repository/games/plaza/content" ]]
echo "cooked the plaza through a session, $steps sources, stopped and refused as asked"
