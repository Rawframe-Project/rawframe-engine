#!/usr/bin/env bash
# References between a game's UI document and its style sheet kept in a
# session (D642), over a copy of Stalls: a node's style set to a class the
# sheet lacks is refused, to one it has kept; a class a node names is not
# removed; a class made and named by a node is not undone while the node
# names it, its history as it was, and is once the node's change is undone.
#
# usage: author_references.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work"
cp -R "$repository/games/tycoon" "$work/game"
game="$work/game/tycoon.game"
button=85ecf7b643b63d65
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
styled() {
    echo '{"operation":"ui.set_field","node":"'"$1"'","field":"style","value":"'"$2"'"}'
}

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"tycoon.ui","request":'"$(request "$(styled tycoon.purse 0000000000000001)")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.ui","request":'"$(request "$(styled tycoon.tip $button)")"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"tycoon.styles","request":'"$(request '{"operation":"styles.remove_class","style":"'$button'"}')"'}'
    echo '{"kind":"authoring.apply","id":5,"scene":"tycoon.styles","request":'"$(request '{"operation":"styles.create_class","style":"00000000000000a1","name":"coin"}')"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"tycoon.ui","request":'"$(request "$(styled tycoon.sign 00000000000000a1)")"'}'
    echo '{"kind":"authoring.undo","id":7,"scene":"tycoon.styles"}'
    echo '{"kind":"authoring.undo","id":8,"scene":"tycoon.ui"}'
    echo '{"kind":"authoring.undo","id":9,"scene":"tycoon.styles"}'
} | "$author" session "$game" "$work/game" >"$work/replies" || true
reply 2 | grep -q '"code":"validation_failed".*"message":"a node.s style is a class the game.s style sheet has"'
reply 3 | grep -q '"written":true'
reply 4 | grep -q '"code":"conflict".*"message":"a class a node of the game.s UI document names stays"'
reply 5 | grep -q '"written":true'
reply 6 | grep -q '"written":true'
reply 7 | grep -q '"code":"conflict".*"message":"the step would leave a node naming a class the sheet does not have"'
reply 8 | grep -q '"written":true'
# Stepped back, the sheet's history is as it was: the class's making is
# what the next undo takes back, and nothing is left to redo but it.
reply 9 | grep -q '"written":true,"reopened":false,"undoable":0,"redoable":1'
if grep -q '00000000000000a1' "$work/game/tycoon.styles" "$work/game/tycoon.ui"; then
    exit 1
fi
cmp "$work/game/tycoon.styles" "$repository/games/tycoon/tycoon.styles"
echo "kept stalls' references in a session"
