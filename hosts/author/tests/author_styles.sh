#!/usr/bin/env bash
# A game's style sheet authored in a session (D636), over a copy of
# Stalls' styles: the sheet read whole, a button's fill set and a pressed
# look made by one atomic request, written to the file; undone back to
# its very bytes and redone; a value the sheet does not read and a scene's
# operation refused with the file untouched; and a stale request refused.
# Classes made, renamed, and removed, and the sheet's history (D638).
#
# usage: author_styles.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/tycoon/tycoon.styles" "$work/root/tycoon.styles"
cp "$work/root/tycoon.styles" "$work/original.styles"
game="$repository/games/tycoon/tycoon.game"
button=85ecf7b643b63d65
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
set_part() {
    echo '{"operation":"styles.set_part","style":"'$button'","variant":"'"$1"'","part":"fill","value":'"$2"'}'
}
read_sheet='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"styles.read_sheet"}]}'

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"tycoon.styles","queries":'"$read_sheet"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.styles","request":'"$(request "$(set_part base '"#c01818c0"'),$(set_part pressed '"#400000ff"')")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"tycoon.styles","queries":'"$read_sheet"'}'
    echo '{"kind":"authoring.undo","id":5,"scene":"tycoon.styles"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"answer":{"sheet":{"kind":"ui.styles","formatVersion":1,"styles":\[{"styleId":"'$button'","name":"button","base":{"fill":"#101820c0"'
reply 3 | grep -q '"written":true,"reopened":false,"undoable":1,"redoable":0,"selection":\[\],"view":null,"results":\[{"deltas":2}\]'
reply 4 | grep -q '"base":{"fill":"#c01818c0"'
reply 4 | grep -q '"pressed":{"fill":"#400000ff"'
# Undone, the sheet is its very bytes again.
reply 5 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/tycoon.styles" "$work/original.styles"

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"tycoon.styles","request":'"$(request "$(set_part base '"#c01818c0"')")"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"tycoon.styles"}'
    echo '{"kind":"authoring.redo","id":4,"scene":"tycoon.styles"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"tycoon.styles","request":'"$(request "$(set_part base '"red"')")"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"tycoon.styles","request":'"$(request '{"operation":"scene.destroy_entity","entity":"6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30"}')"'}'
    echo '{"kind":"authoring.apply","id":7,"scene":"tycoon.styles","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","expects":"sha256:00","operations":[]}}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 4 | grep -q '"undoable":1,"redoable":0'
grep -q '"fill": "#c01818c0"' "$work/root/tycoon.styles"
cp "$work/root/tycoon.styles" "$work/redone.styles"
reply 5 | grep -q '"code":"validation_failed".*"message":"the style sheet does not read: a color is # and 8 lowercase hexadecimal digits, #rrggbbaa at $.styles\[0\].base.fill"'
reply 6 | grep -q '"code":"validation_failed".*"message":"the operation changes a scene, not a style sheet"'
reply 7 | grep -q '"code":"target_stale"'
cmp "$work/root/tycoon.styles" "$work/redone.styles"

# Classes made, renamed, and removed (D638), the sheet's history listed,
# and undone back to the bytes before.
tip='{"operation":"styles.create_class","style":"00000000000000a1","name":"tip","place":0}'
renamed='{"operation":"styles.rename_class","style":"00000000000000a1","name":"tooltip"}'
removed='{"operation":"styles.remove_class","style":"'$button'"}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"tycoon.styles","request":'"$(request "$tip,$renamed")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.styles","request":'"$(request "$removed")"'}'
    echo '{"kind":"authoring.history","id":4,"scene":"tycoon.styles"}'
    echo '{"kind":"authoring.undo","id":5,"scene":"tycoon.styles"}'
    echo '{"kind":"authoring.undo","id":6,"scene":"tycoon.styles"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"results":\[{"deltas":2}\]'
reply 3 | grep -q '"written":true'
reply 4 | grep -q '"entries":\[{"summary":"create class tip and 1 more","deltas":2,"applied":true},{"summary":"delete class button","deltas":1,"applied":true}\]'
cmp "$work/root/tycoon.styles" "$work/redone.styles"
echo "authored stalls' styles in a session"
