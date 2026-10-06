#!/usr/bin/env bash
# An authoring session over a copy of runners' level (D407): records a
# line on standard input, replies a line on standard output. A crate made
# and undone back to the level's very bytes, redone, a stale undo refused,
# the file changed under the session and opened again with no history, and
# records out of form, out of order, or naming a scene outside the root
# refused, each answered by its own id.
#
# usage: author_session.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/runners/level.scene" "$work/root/level.scene"
cp "$work/root/level.scene" "$work/original.scene"
game="$repository/games/runners/runners.game"
body=d0ae39a2-4803-4ee0-9d45-1980875324d0
crate=6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30
create='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[
{"operation":"scene.create_entity","entity":"'$crate'","name":"crate","place":0},
{"operation":"scene.add_component","entity":"'$crate'","component":"'$body'"}]}'
create=$(tr -d '\n' <<<"$create")

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

# A session begins with hello in the surface generation the tool speaks.
{
    echo '{"kind":"authoring.describe","id":1}'
    echo '{"kind":"authoring.hello","id":2,"surfaceGeneration":99}'
    echo '{"kind":"authoring.hello","id":3,"surfaceGeneration":1}'
    echo '{"kind":"authoring.describe","id":4}'
    echo '{"kind":"authoring.apply","id":5,"scene":"level.scene","request":'"$create"'}'
    echo '{"kind":"authoring.undo","id":6,"scene":"level.scene"}'
    echo '{"kind":"authoring.end","id":7}'
    echo '{"kind":"authoring.describe","id":8}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 1 | grep -q '"message":"a session begins with hello"'
reply 2 | grep -q '"code":"unsupported_operation"'
reply 3 | grep -q '"kind":"authoring.welcome"'
reply 4 | grep -q '"name":"scene.set_reference"'
reply 5 | grep -q '"written":true,"reopened":false,"undoable":1,"redoable":0,"results":\[{"deltas":2}\]'
# Undone within the session, the level is its very bytes again.
reply 6 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/level.scene" "$work/original.scene"
# Nothing is read past end.
reply 7 | grep -q '"kind":"authoring.ended"'
! grep -q '"id":8,' "$work/replies"

# The history lives as long as the session: made, undone, redone, a stale
# undo refused; then the file changed under the session is opened again,
# its history let go.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.scene","request":'"$create"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"level.scene"}'
    echo '{"kind":"authoring.redo","id":4,"scene":"level.scene"}'
    echo '{"kind":"authoring.undo","id":5,"scene":"level.scene","expects":"sha256:00"}'
    sleep 1
    cp "$work/original.scene" "$work/root/level.scene"
    echo '{"kind":"authoring.undo","id":6,"scene":"level.scene"}'
    echo '{"kind":"authoring.read","id":7,"scene":"level.scene","queries":{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"scene.list_entities"}]}}'
    echo '{"kind":"authoring.undo","id":8,"scene":"../original.scene"}'
    echo '{"kind":"authoring.apply","id":9,"scene":"level.scene"}'
    echo 'not json'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"undoable":1,"redoable":0'
reply 3 | grep -q '"undoable":0,"redoable":1'
reply 4 | grep -q '"undoable":1,"redoable":0'
reply 5 | grep -q '"code":"target_stale"'
reply 6 | grep -q '"reopened":true,"undoable":0,"redoable":0,"results":\[{"error":{"code":"target_not_found"'
cmp "$work/root/level.scene" "$work/original.scene"
reply 7 | grep -q '"kind":"authoring.answers"'
! reply 7 | grep -q "$crate"
reply 8 | grep -q '"message":"a session'"'"'s scenes are .scene files under its root"'
reply 9 | grep -q '"code":"validation_failed"'
grep -q '"id":null,"error":{"code":"validation_failed"' "$work/replies"
echo "authored runners in a session"
