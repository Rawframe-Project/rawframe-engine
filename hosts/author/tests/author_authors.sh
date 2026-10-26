#!/usr/bin/env bash
# Two authors in one session, headless: each record names
# its author; Ada's undo takes back her own crate past Ben's later one, as
# an entry of its own, and her redo puts it back; Ben's undo takes back
# his; the history names who made each entry and what it is; an undo a
# later change by another author stands in the way of is refused naming
# that author; and an author's name out of form is refused.
#
# usage: author_authors.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3
trap 'echo "author_authors: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/runners/level.scene" "$work/root/level.scene"
printf 'blockout 1\nbox floor at 0 0 0 size 8 0.5 8\n' >"$work/root/yard.blockout"
game="$repository/games/runners/runners.game"
crate() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.create_entity","entity":"'"$1"'","name":"'"$2"'","place":0}]}'
}
line() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"lines.set","line":'"$1"',"text":"'"$2"'"}]}'
}
ada=6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b31
ben=6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b32
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.scene","author":"ada","request":'"$(crate $ada "ada crate")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"level.scene","author":"ben","request":'"$(crate $ben "ben crate")"'}'
    echo '{"kind":"authoring.undo","id":4,"scene":"level.scene","author":"ada"}'
    echo '{"kind":"authoring.undo","id":5,"scene":"level.scene","author":"ada"}'
    echo '{"kind":"authoring.history","id":6,"scene":"level.scene"}'
    echo '{"kind":"authoring.redo","id":7,"scene":"level.scene","author":"ada"}'
    echo '{"kind":"authoring.undo","id":8,"scene":"level.scene","author":"ben"}'
    echo '{"kind":"authoring.history","id":9,"scene":"level.scene"}'
    echo '{"kind":"authoring.apply","id":10,"scene":"yard.blockout","author":"ada","request":'"$(line 1 "box floor at 0 0 0 size 9 0.5 9")"'}'
    echo '{"kind":"authoring.apply","id":11,"scene":"yard.blockout","author":"ben","request":'"$(line 1 "box floor at 0 0 0 size 7 0.5 7")"'}'
    echo '{"kind":"authoring.undo","id":12,"scene":"yard.blockout","author":"ada"}'
    echo '{"kind":"authoring.history","id":13,"scene":"yard.blockout"}'
    echo '{"kind":"authoring.undo","id":14,"scene":"level.scene","author":""}'
    echo '{"kind":"authoring.end","id":15}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
digest() { reply "$1" | grep -o '"document":"sha256:[0-9a-f]*"' | cut -d'"' -f4; }
reply 2 | grep -q '"written":true'
reply 3 | grep -q '"written":true'
# Ada's crate is taken back past Ben's later one, as an entry of its own:
# the level as it was before either, but for Ben's crate.
reply 4 | grep -q '"written":true,.*"undoable":3,"redoable":0'
reply 5 | grep -q '"code":"target_not_found"'
reply 6 | grep -q '"entries":\[{[^}]*"author":"ada","kind":"change"},{[^}]*"author":"ben","kind":"change"},{[^}]*"author":"ada","kind":"undo"}\]'
# Her redo puts it back where it stood, and Ben's undo leaves the level
# as Ada's change alone left it, byte for byte.
reply 7 | grep -q '"written":true'
reply 8 | grep -q '"written":true'
if [ "$(digest 8)" != "$(digest 2)" ] || [ "$(digest 7)" != "$(digest 3)" ]; then
    echo "the authors' steps did not leave the level as their own changes alone would"
    exit 1
fi
reply 9 | grep -q '"author":"ada","kind":"redo"},{[^}]*"author":"ben","kind":"undo"}\]'
grep -q '"ada crate"' "$work/root/level.scene"
if grep -q '"ben crate"' "$work/root/level.scene"; then
    echo "Ben's crate stayed after his undo"
    exit 1
fi
# A line document's undo past another author's change is refused.
reply 12 | grep -q '"code":"conflict",.*"authorNumber":"2".*"author":"ben"'
grep -qx 'box floor at 0 0 0 size 7 0.5 7' "$work/root/yard.blockout"
reply 13 | grep -q '"author":"ada","kind":"change"},{[^}]*"author":"ben","kind":"change"}\]'
reply 14 | grep -q '"code":"validation_failed"'
echo "two authors each undid their own changes in one session"
