#!/usr/bin/env bash
# A session cooks its game as a long-running operation (SPEC-0040, D502):
# the plaza cooked while a record is answered, its progress told source by
# source and its reply when it ends; a second cook while one runs refused
# by the session's limit; a cook asked to stop answering cancelled, which
# is no error; a cook the tool refuses (into the game's own directory)
# answering its error; and a cook running when the input ends stopped and
# answering before the session does.
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
rm -rf "$work"
mkdir -p "$work"
game="$(native "$repository")/games/plaza/plaza.game"

reply() { grep "\"id\":$1," "$work/replies"; }

# The plaza cooked, a second cook refused while it runs, and a describe
# answered between them; the session ends once the first has answered.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
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
    echo '{"kind":"authoring.end","id":5}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true
steps=$(grep -c '"kind":"authoring.progress","id":2,' "$work/replies")
reply 2 | grep -q '"answer":{"kind":"authoring.cooked","cooked":'"$steps"',"reused":0}'
grep -q '"id":2,"step":'"$steps"',"steps":'"$steps"',' "$work/replies"
reply 3 | grep -q '"code":"limit_exceeded"'
reply 4 | grep -q '"found":false'
reply 5 | grep -q '"authoring.ended"'
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
