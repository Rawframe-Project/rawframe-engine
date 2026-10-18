#!/usr/bin/env bash
# A game's description, its settings, authored in a session (D817), over a
# copy of Quarry's: its lines read, an aspect put in after its program line
# and written, the lines no change named (its comments among them) kept as
# they stand; undone back to its very bytes and redone; a line its reader
# refuses, a place past its lines, and a scene's operation refused with the
# file untouched.
#
# usage: author_game.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3
trap 'echo "author_game: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/quarry/quarry.game" "$work/root/quarry.game"
cp "$work/root/quarry.game" "$work/original.game"
game="$repository/games/quarry/quarry.game"
# The program line's place, from nought, and the count of lines.
program=$(($(grep -n '^program ' "$work/original.game" | cut -d: -f1) - 1))
count=$(wc -l <"$work/original.game")
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
insert() {
    echo '{"operation":"lines.insert","line":'"$1"',"text":"'"$2"'"}'
}
read_lines='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"lines.read"}]}'

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"quarry.game","queries":'"$read_lines"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"quarry.game","request":'"$(request "$(insert $((program + 1)) "aspect 16 9")")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"quarry.game","queries":'"$read_lines"'}'
    echo '{"kind":"authoring.history","id":5,"scene":"quarry.game"}'
    echo '{"kind":"authoring.undo","id":6,"scene":"quarry.game"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"answer":{"lines":\["# Quarry (D735), the small block game'
reply 2 | grep -q '"program quarry.kest"'
reply 3 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 4 | grep -q '"program quarry.kest","aspect 16 9"'
reply 5 | grep -q '"entries":\[{"summary":"insert line '$((program + 1))'","deltas":1,"applied":true,"kind":"change"}\]'
# Undone, the description is its very bytes again.
reply 6 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/quarry.game" "$work/original.game"

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"quarry.game","request":'"$(request "$(insert $((program + 1)) "aspect 16 9")")"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"quarry.game"}'
    echo '{"kind":"authoring.redo","id":4,"scene":"quarry.game"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"quarry.game","request":'"$(request "$(insert 0 "aspect sixteen nine")")"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"quarry.game","request":'"$(request '{"operation":"lines.set","line":'$((count + 5))',"text":null}')"'}'
    echo '{"kind":"authoring.apply","id":7,"scene":"quarry.game","request":'"$(request '{"operation":"scene.destroy_entity","entity":"6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30"}')"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 4 | grep -q '"undoable":1,"redoable":0'
cp "$work/root/quarry.game" "$work/redone.game"
reply 5 | grep -q '"code":"validation_failed",.*"message":"the game description does not read: '
reply 6 | grep -q '"code":"target_not_found"'
reply 7 | grep -q '"code":"validation_failed",.*"message":"a line document'"'"'s operations are lines.set and lines.insert"'
cmp "$work/root/quarry.game" "$work/redone.game"
# The redone description differs from the original by the one line put in,
# after the program line.
[[ "$(diff "$work/original.game" "$work/redone.game")" == "$((program + 1))a$((program + 2))
> aspect 16 9" ]]
echo "authored quarry's description in a session"
