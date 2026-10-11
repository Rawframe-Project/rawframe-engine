#!/usr/bin/env bash
# A game's UI document authored in a session (D641), over a copy of
# Stalls': the document read whole; the purse's fill set, a node made
# inside the wallet, and the banner removed by one atomic request,
# written; the history listed; undone back to its very bytes; and a field
# a node has not, a node the document has not, and a style sheet's
# operation refused with the file untouched.
#
# usage: author_ui.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/tycoon/tycoon.ui" "$work/root/tycoon.ui"
cp "$work/root/tycoon.ui" "$work/original.ui"
game="$repository/games/tycoon/tycoon.game"
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
read_document='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"ui.read_document"}]}'
fill='{"operation":"ui.set_field","node":"tycoon.purse","field":"fill","value":"#c01818c0"}'
made='{"operation":"ui.create_node","node":"tycoon.coin","parent":"tycoon.wallet","place":0}'
removed='{"operation":"ui.remove_node","node":"tycoon.banner"}'

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"tycoon.ui","queries":'"$read_document"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.ui","request":'"$(request "$fill,$made,$removed")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"tycoon.ui","queries":'"$read_document"'}'
    echo '{"kind":"authoring.history","id":5,"scene":"tycoon.ui"}'
    echo '{"kind":"authoring.undo","id":6,"scene":"tycoon.ui"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"answer":{"document":{"kind":"ui.document","formatVersion":1,"nodes":\[{"component":"tycoon.wallet"'
reply 3 | grep -q '"written":true,"reopened":false,"undoable":1,"redoable":0,"selection":\[\],"view":null,"results":\[{"deltas":3}\]'
reply 4 | grep -q '"children":\[{"component":"tycoon.coin"},{"component":"tycoon.purse"'
reply 4 | grep -q '"fill":"#c01818c0"'
if reply 4 | grep -q 'tycoon.banner'; then
    exit 1
fi
reply 5 | grep -q '"entries":\[{"summary":"set fill of tycoon.purse and 2 more","deltas":3,"applied":true}\]'
reply 6 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/tycoon.ui" "$work/original.ui"

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"tycoon.ui","request":'"$(request '{"operation":"ui.set_field","node":"tycoon.purse","field":"colour","value":null}')"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"tycoon.ui","request":'"$(request '{"operation":"ui.remove_node","node":"tycoon.gone"}')"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"tycoon.ui","request":'"$(request '{"operation":"styles.remove_class","style":"85ecf7b643b63d65"}')"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"code":"validation_failed"'
reply 3 | grep -q '"code":"target_not_found"'
reply 4 | grep -q '"message":"the operation does not change a UI document"'
cmp "$work/root/tycoon.ui" "$work/original.ui"
echo "authored stalls' UI document in a session"
