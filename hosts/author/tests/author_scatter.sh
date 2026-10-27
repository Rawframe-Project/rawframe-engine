#!/usr/bin/env bash
# A scatter edited in a session, headless: its count and seed set
# line by line and written; a count past its limit and a layer with no
# terrain refused by the scatter's reader with the file untouched; and the
# change undone back to the file's very bytes.
#
# usage: author_scatter.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$3")"
trap 'echo "author_scatter: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
printf 'scatter 1\nprefab crate.scene\nseed 11\narea -27 6.5 54 3\nspacing 2\ncount 12\n' >"$work/root/crates.scatter"
cp "$work/root/crates.scatter" "$work/original.scatter"
game="$repository/games/ledges/ledges.game"
set_line() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"lines.set","line":'"$1"',"text":"'"$2"'"}]}'
}
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"crates.scatter","request":'"$(set_line 5 "count 20")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"crates.scatter","request":'"$(set_line 2 "seed 12")"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"crates.scatter","request":'"$(set_line 5 "count 20000")"'}'
    echo '{"kind":"authoring.apply","id":5,"scene":"crates.scatter","request":'"$(set_line 4 "layer grass")"'}'
    echo '{"kind":"authoring.undo","id":6,"scene":"crates.scatter"}'
    echo '{"kind":"authoring.undo","id":7,"scene":"crates.scatter"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 3 | grep -q '"written":true,.*"undoable":2,"redoable":0'
reply 4 | grep -q '"code":"validation_failed",.*"message":"the scatter does not read: scatter line 6: a count is 1 to 10000"'
reply 5 | grep -q '"code":"validation_failed",.*"message":"the scatter does not read: scatter line [0-9]*: a layer is a terrain'
reply 7 | grep -q '"undoable":0,"redoable":2'
cmp "$work/root/crates.scatter" "$work/original.scatter"
echo "edited a scatter in a session and undid it"
