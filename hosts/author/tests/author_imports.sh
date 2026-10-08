#!/usr/bin/env bash
# Files from outside made a game's assets through a session (D503), in a
# copy of the plaza: a texture imported into a new directory, declared by
# the game and listed among its assets; a glTF imported with the image it
# names beside it, its materials mapped by the cook tool; then the game
# cooked with both. Refused: a path leaving the game's directory, one
# taken, one whose name says no importer's kind, and a source whose kind
# is not the one it is imported as.
#
# usage: author_imports.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
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
cp -R "$repository/games/plaza" "$work/plaza"
game="$work/plaza/plaza.game"
plaza="$(native "$repository")/games/plaza"

reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.import","id":2,"source":"'"$plaza"'/paving.png","as":"art/bark.png"}'
    echo '{"kind":"authoring.import","id":3,"source":"'"$plaza"'/crate.gltf","as":"props/crate.gltf"}'
    echo '{"kind":"authoring.assets","id":4}'
    echo '{"kind":"authoring.import","id":5,"source":"'"$plaza"'/paving.png","as":"../bark.png"}'
    echo '{"kind":"authoring.import","id":6,"source":"'"$plaza"'/paving.png","as":"art/bark.png"}'
    echo '{"kind":"authoring.import","id":7,"source":"'"$plaza"'/paving.png","as":"art/bark.txt"}'
    echo '{"kind":"authoring.import","id":8,"source":"'"$plaza"'/crate.gltf","as":"art/crate.png"}'
    echo '{"kind":"authoring.cook","id":9,"output":"'"$work/content"'","cache":null}'
    # Each wait ends as the reply comes; four minutes only bounds a cook
    # on a sanitized tree under load, which took more than thirty seconds
    # (D507).
    for _ in $(seq 2400); do
        grep -q '"id":9,"answer"\|"id":9,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.end","id":10}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true

reply 2 | grep -q '"answer":{"kind":"authoring.imported","asset":"texture","id":"[0-9a-f]\{16\}","resource":"[0-9a-f]\{32\}","path":"art/bark.png","mapped":false}'
reply 3 | grep -q '"answer":{"kind":"authoring.imported","asset":"mesh",.*"path":"props/crate.gltf","mapped":true}'
# The game declares both, each by the identity its answer gave.
texture=$(reply 2 | sed 's/.*"id":"\([0-9a-f]\{16\}\)".*/\1/')
mesh=$(reply 3 | sed 's/.*"id":"\([0-9a-f]\{16\}\)".*/\1/')
grep -qx "texture $texture art/bark.png" "$game"
grep -qx "mesh $mesh props/crate.gltf" "$game"
reply 4 | grep -q '"name":"art/bark.png"'
reply 4 | grep -q '"name":"props/crate.gltf"'
# Each with its sidecar, the glTF with the image it names, its materials
# mapped.
[[ -s "$work/plaza/art/bark.png.rfmeta" && -s "$work/plaza/props/crate.png" ]]
grep -q '"importer": "rawframe.mesh"' "$work/plaza/props/crate.gltf.rfmeta"
grep -q '"subassets"' "$work/plaza/props/crate.gltf.rfmeta"
reply 5 | grep -q '"code":"validation_failed"'
reply 6 | grep -q '"code":"conflict"'
reply 7 | grep -q '"code":"validation_failed"'
reply 8 | grep -q '"code":"target_not_found"'
# The game cooks with what was imported.
reply 9 | grep -q '"answer":{"kind":"authoring.cooked"'
grep -q 'art/bark.png' "$work/content/cook.receipt"
grep -q 'props/crate.gltf' "$work/content/cook.receipt"
echo "imported a texture and a mesh into the plaza and cooked them"
