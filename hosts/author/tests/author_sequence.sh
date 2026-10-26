#!/usr/bin/env bash
# A sequence edited in a session, headless: a key moved and a key
# added line by line and written; a key out of time order and one on a
# track the sequence does not name refused by the sequence's reader with
# the file untouched; and the changes undone back to the file's very bytes.
#
# usage: author_sequence.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$3")"
trap 'echo "author_sequence: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
printf 'sequence 1\nduration 8\ntrack lift\nplay 0 lift 0000000000000000\nevent 4 0000000000000001 halfway\n' \
    >"$work/root/ride.sequence"
cp "$work/root/ride.sequence" "$work/original.sequence"
game="$repository/games/ledges/ledges.game"
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"lines.'"$1"'","line":'"$2"',"text":"'"$3"'"}]}'
}
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"ride.sequence","request":'"$(request set 4 "event 3 0000000000000001 halfway")"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"ride.sequence","request":'"$(request insert 5 "event 6 0000000000000001 halfway")"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"ride.sequence","request":'"$(request insert 6 "event 5 0000000000000001 halfway")"'}'
    echo '{"kind":"authoring.apply","id":5,"scene":"ride.sequence","request":'"$(request set 3 "play 0 door 0000000000000000")"'}'
    echo '{"kind":"authoring.undo","id":6,"scene":"ride.sequence"}'
    echo '{"kind":"authoring.undo","id":7,"scene":"ride.sequence"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 3 | grep -q '"written":true,.*"undoable":2,"redoable":0'
reply 4 | grep -q '"code":"validation_failed",.*"message":"the sequence does not read: sequence line 7: keys are in time order'
reply 5 | grep -q '"code":"validation_failed",.*"message":"the sequence does not read: sequence line 4: a play key is'
reply 7 | grep -q '"undoable":0,"redoable":2'
cmp "$work/root/ride.sequence" "$work/original.sequence"
echo "edited a sequence in a session and undid it"
