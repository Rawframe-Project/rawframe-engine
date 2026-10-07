#!/usr/bin/env bash
# An authoring session over a copy of runners' level (D407): records a
# line on standard input, replies a line on standard output. A crate made
# and undone back to the level's very bytes, redone, a stale undo refused,
# the file changed under the session and opened again with no history, and
# records out of form, out of order, or naming a scene outside the root
# refused, each answered by its own id; and a selection kept beside the
# level, put back by undo (D417), as is where the level is looked at from
# (D432).
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
reply 5 | grep -q '"written":true,"reopened":false,"undoable":1,"redoable":0,"selection":\[\],"view":null,"results":\[{"deltas":2}\]'
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
reply 6 | grep -q '"reopened":true,"undoable":0,"redoable":0,"selection":\[\],"view":null,"results":\[{"error":{"code":"target_not_found"'
cmp "$work/root/level.scene" "$work/original.scene"
reply 7 | grep -q '"kind":"authoring.answers"'
! reply 7 | grep -q "$crate"
reply 8 | grep -q '"message":"a session'"'"'s scenes are .scene files under its root"'
reply 9 | grep -q '"code":"validation_failed"'
grep -q '"id":null,"error":{"code":"validation_failed"' "$work/replies"

# A selection is no change to the level, and undo puts back the one from
# before the change it undoes.
cp "$work/original.scene" "$work/root/level.scene"
rename='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[
{"operation":"scene.rename_entity","entity":"'$crate'","name":"box"}]}'
rename=$(tr -d '\n' <<<"$rename")
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.select","id":2,"scene":"level.scene","entities":["'$crate'"]}'
    echo '{"kind":"authoring.apply","id":3,"scene":"level.scene","request":'"$create"'}'
    echo '{"kind":"authoring.select","id":4,"scene":"level.scene","entities":["'$crate'","'$crate'"]}'
    echo '{"kind":"authoring.apply","id":5,"scene":"level.scene","request":'"$rename"'}'
    echo '{"kind":"authoring.select","id":6,"scene":"level.scene","entities":[]}'
    echo '{"kind":"authoring.undo","id":7,"scene":"level.scene"}'
    echo '{"kind":"authoring.undo","id":8,"scene":"level.scene"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
# The crate is not there to choose until it is made.
reply 2 | grep -q '"code":"target_not_found"'
reply 3 | grep -q '"selection":\[\]'
made=$(reply 3 | grep -o '"document":"[^"]*"')
reply 4 | grep -q "\"kind\":\"authoring.selection\",$made,\"reopened\":false,\"selection\":\[\"$crate\"\]"
reply 6 | grep -q '"selection":\[\]'
# Undoing the rename chooses the crate again; undoing its making leaves
# nothing chosen, the crate gone with it.
reply 7 | grep -q "\"selection\":\[\"$crate\"\]"
reply 8 | grep -q '"selection":\[\]'
cmp "$work/root/level.scene" "$work/original.scene"

# A view is no change to the level either. Looked at from high as the
# crate is made, then from low: undoing and redoing the making put back
# the view from when it opened and from when it committed, high both.
cp "$work/original.scene" "$work/root/level.scene"
high='{"eye":[0,20,0.5],"target":[0,0,0],"fieldOfView":50}'
low='{"eye":[3,1,3],"target":[0,1,0],"fieldOfView":70}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.view","id":2,"scene":"level.scene","view":'"$high"'}'
    echo '{"kind":"authoring.view","id":3,"scene":"level.scene","view":{"eye":[1,1,1],"target":[1,1,1],"fieldOfView":60}}'
    echo '{"kind":"authoring.apply","id":4,"scene":"level.scene","request":'"$create"'}'
    echo '{"kind":"authoring.view","id":5,"scene":"level.scene","view":'"$low"'}'
    echo '{"kind":"authoring.undo","id":6,"scene":"level.scene"}'
    echo '{"kind":"authoring.redo","id":7,"scene":"level.scene"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
original=$(reply 2 | grep -o '"document":"[^"]*"')
reply 2 | grep -q "\"kind\":\"authoring.view\",$original,\"reopened\":false,\"view\":{\"eye\":\[0,20,0.5\],\"target\":\[0,0,0\],\"fieldOfView\":50}"
reply 3 | grep -q '"code":"validation_failed"'
reply 4 | grep -q '"view":{"eye":\[0,20,0.5\]'
reply 6 | grep -q '"view":{"eye":\[0,20,0.5\]'
reply 5 | grep -q '"view":{"eye":\[3,1,3\],"target":\[0,1,0\],"fieldOfView":70}'
reply 7 | grep -q '"view":{"eye":\[0,20,0.5\]'
# A preview that cannot be reached is refused, and the session goes on
# (D433).
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.preview","id":2,"scene":"level.scene","preview":{"endpoint":"127.0.0.1:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
    echo '{"kind":"authoring.preview","id":3,"scene":"level.scene","preview":null}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"code":"capability_denied".*"said":"the pin and token files must read"'
reply 3 | grep -q '"answer":{"kind":"authoring.preview","reopened":false,"previewing":false}'
# Nor is one off this machine, or a file that is not a small regular one.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.preview","id":2,"scene":"level.scene","preview":{"endpoint":"192.0.2.1:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
    echo '{"kind":"authoring.preview","id":3,"scene":"level.scene","preview":{"endpoint":"127.0.0.1.example:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
    echo '{"kind":"authoring.preview","id":4,"scene":"level.scene","preview":{"endpoint":"127.0.0.1:9","pinFile":"/dev/zero","tokenFile":"/dev/zero"}}'
    echo '{"kind":"authoring.preview","id":5,"scene":"level.scene","preview":{"endpoint":"127.999.0.1:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
    echo '{"kind":"authoring.preview","id":6,"scene":"level.scene","preview":{"endpoint":"127.0.0.010:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
    echo '{"kind":"authoring.preview","id":7,"scene":"level.scene","preview":{"endpoint":"localhost:9","pinFile":"'"$work"'/none","tokenFile":"'"$work"'/none"}}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
for refused in 5 6 7; do
    reply $refused | grep -q '"code":"capability_denied","class":"permission_denied"'
done
reply 2 | grep -q '"code":"capability_denied".*"endpoint":"192.0.2.1:9"'
reply 3 | grep -q '"code":"capability_denied".*"endpoint":"127.0.0.1.example:9"'
reply 4 | grep -q '"said":"the pin and token files must read"'
# A new scene (D449): empty, with a sidecar of its own identity; one where
# a scene is, outside the root, or in a directory not there is refused. Given
# an entity, it is found by that identity in a later session on the game,
# and the level places an instance of it.
cp -R "$repository/games/runners" "$work/runners"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.create_scene","id":2,"scene":"crate.scene"}'
    echo '{"kind":"authoring.create_scene","id":3,"scene":"crate.scene"}'
    echo '{"kind":"authoring.create_scene","id":4,"scene":"../outside.scene"}'
    echo '{"kind":"authoring.create_scene","id":5,"scene":"parts/crate.scene"}'
    echo '{"kind":"authoring.create_scene","id":6,"scene":"level.scene"}'
    echo '{"kind":"authoring.read","id":7,"scene":"crate.scene","queries":{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"scene.list_entities"}]}}'
    echo '{"kind":"authoring.apply","id":8,"scene":"crate.scene","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.create_entity","entity":"3c9e1d40-7a2b-4f6e-8d15-0b4c2e9f7a63","name":"crate"}]}}'
} | "$author" session "$work/runners/runners.game" "$work/runners" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.created","scene":"crate.scene","resource":"[0-9a-f]\{32\}","document":"sha256:'
reply 3 | grep -q '"code":"conflict","class":"already_exists"'
reply 4 | grep -q '"class":"not_found"'
reply 5 | grep -q '"class":"not_found"'
reply 6 | grep -q '"code":"conflict","class":"already_exists"'
reply 7 | grep -q '"entities":\[\]'
reply 8 | grep -q '"written":true'
grep -q '"importer": "rawframe.scene"' "$work/runners/crate.scene.rfmeta"
[ ! -e "$work/outside.scene" ] && [ ! -e "$work/runners/parts" ]
resource=$(reply 2 | grep -o '"resource":"[0-9a-f]*"' | cut -d'"' -f4)
place='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.add_instance","scene":"'"$resource"'","instance":"'"$crate"'"}]}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"level.scene","request":'"$place"'}'
} | "$author" session "$work/runners/runners.game" "$work/runners" >"$work/replies" || true
reply 2 | grep -q '"written":true'
grep -q "\"scene\": \"$resource\"" "$work/runners/level.scene"
# A scene's history (D454): each entry summed up, oldest first, the undone
# one after the applied, on a level of its own.
mkdir -p "$work/history"
cp "$work/original.scene" "$work/history/level.scene"
rename='{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"c5400cf8-4b07-4d16-84c2-951852c66b40","name":"floor"}]}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.history","id":2,"scene":"level.scene"}'
    echo '{"kind":"authoring.apply","id":3,"scene":"level.scene","request":'"$rename"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"level.scene","request":'"$create"'}'
    echo '{"kind":"authoring.undo","id":5,"scene":"level.scene"}'
    echo '{"kind":"authoring.history","id":6,"scene":"level.scene"}'
} | "$author" session "$game" "$work/history" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.history","reopened":false,"entries":\[\]}'
reply 6 | grep -q '"entries":\[{"summary":"rename to floor","deltas":1,"applied":true},{"summary":"create entity crate[^"]*","deltas":[0-9]*,"applied":false}\]'
# The assets runners declares (D455), each by kind, identity, and name.
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.assets","id":2}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"answer":{"kind":"authoring.assets","assets":\[{"kind":"texture","id":"[0-9a-f]\{16\}","name":"runner.png"}'
reply 2 | grep -q '{"kind":"texture","id":"c067c4be8ce86d12","name":"hud.png"}'
reply 2 | grep -q '{"kind":"sound","id":"[0-9a-f]\{16\}","name":"jump.sound"}'
echo "authored runners in a session"
