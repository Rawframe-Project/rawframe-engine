#!/usr/bin/env bash
# A game's files moved, removed, and its folders made through a session
# (D929), in a copy of the plaza: a folder made and listed though empty, a
# texture moved into it with its sidecar, its identity kept and the game's
# line renamed, then the folder renamed with it inside, and the game cooked
# from where it is now; an empty folder removed, and a texture removed
# with its sidecar and its line. Refused: the game's description, a path
# leaving the directory, a target taken, a file not there, a folder made
# twice, and a folder moved into itself.
#
# usage: author_files.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
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
trap 'echo "failed at line $LINENO"; [ -f "$work/replies" ] && tail -20 "$work/replies"' ERR
game="$work/plaza/plaza.game"
# A texture of its own to remove, declared as an import would.
cp "$work/plaza/paving.png" "$work/plaza/spare.png"
sed 's/"resourceId": *"[0-9a-f]\{32\}"/"resourceId": "5bae0000000000000000000000000001"/' \
    "$work/plaza/paving.png.rfmeta" >"$work/plaza/spare.png.rfmeta"
echo "texture 5bae000000000001 spare.png" >>"$work/plaza/plaza.game"
resource=$(sed -n 's/.*"resourceId": *"\([0-9a-f]\{32\}\)".*/\1/p' "$work/plaza/paving.png.rfmeta")

reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.make_folder","id":2,"folder":"art"}'
    echo '{"kind":"authoring.make_folder","id":3,"folder":"empty/deeper"}'
    echo '{"kind":"authoring.assets","id":4}'
    echo '{"kind":"authoring.move_file","id":5,"from":"paving.png","to":"art/paving.png"}'
    echo '{"kind":"authoring.move_file","id":6,"from":"art","to":"textures"}'
    echo '{"kind":"authoring.assets","id":7}'
    echo '{"kind":"authoring.remove_file","id":8,"path":"empty"}'
    echo '{"kind":"authoring.remove_file","id":9,"path":"spare.png"}'
    echo '{"kind":"authoring.move_file","id":10,"from":"plaza.game","to":"other.game"}'
    echo '{"kind":"authoring.move_file","id":11,"from":"crate.png","to":"../crate.png"}'
    echo '{"kind":"authoring.move_file","id":12,"from":"crate.png","to":"sky.hdr"}'
    echo '{"kind":"authoring.move_file","id":13,"from":"nothing.png","to":"art/nothing.png"}'
    echo '{"kind":"authoring.make_folder","id":14,"folder":"textures"}'
    echo '{"kind":"authoring.move_file","id":15,"from":"textures","to":"textures/inner"}'
    echo '{"kind":"authoring.remove_file","id":16,"path":"kest.project"}'
    echo '{"kind":"authoring.cook","id":17,"output":"'"$work/content"'","cache":null}'
    # Each wait ends as the reply comes; four minutes only bounds a cook
    # on a sanitized tree under load (D507).
    for _ in $(seq 2400); do
        grep -q '"id":17,"answer"\|"id":17,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.end","id":18}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true

reply 2 | grep -q '"answer":{"kind":"authoring.folder_made","folder":"art"}'
reply 3 | grep -q '"answer":{"kind":"authoring.folder_made","folder":"empty/deeper"}'
# Listed though nothing is in them.
reply 4 | grep -q '"folders":\[[^]]*"art"[^]]*"empty"[^]]*"empty/deeper"'
# The texture moved with its sidecar, the game's line renamed.
reply 5 | grep -q '"answer":{"kind":"authoring.moved","from":"paving.png","to":"art/paving.png","files":\["paving.png","paving.png.rfmeta"\],"lines":1,'
reply 6 | grep -q '"answer":{"kind":"authoring.moved","from":"art","to":"textures","files":\["art"\],"lines":1,'
grep -q "^texture 9f822820a44af4fc textures/paving.png$" "$game"
[[ -f "$work/plaza/textures/paving.png" && -f "$work/plaza/textures/paving.png.rfmeta" && ! -e "$work/plaza/art" ]]
# Its identity is its own still, as its sidecar keeps it.
reply 7 | grep -q '"path":"textures/paving.png","folder":"textures","importer":"rawframe.texture","id":"'"$resource"'"'
reply 8 | grep -q '"answer":{"kind":"authoring.removed","from":"empty","files":\["empty"\],"lines":0,'
reply 9 | grep -q '"answer":{"kind":"authoring.removed","from":"spare.png","files":\["spare.png","spare.png.rfmeta"\],"lines":1,'
if grep -q "spare.png" "$game"; then false; fi
[[ ! -e "$work/plaza/empty" && ! -e "$work/plaza/spare.png" && ! -e "$work/plaza/spare.png.rfmeta" ]]
reply 10 | grep -q '"code":"validation_failed"'
reply 11 | grep -q '"code":"validation_failed"'
reply 12 | grep -q '"code":"conflict"'
reply 13 | grep -q '"code":"target_not_found"'
reply 14 | grep -q '"code":"conflict"'
reply 15 | grep -q '"code":"validation_failed"'
reply 16 | grep -q '"code":"validation_failed"'
# The game cooks from where its files are now.
reply 17 | grep -q '"answer":{"kind":"authoring.cooked"'
grep -q 'textures/paving.png' "$work/content/cook.receipt"
echo "moved, removed, and made the plaza's files and folders and cooked it"
