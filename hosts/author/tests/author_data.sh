#!/usr/bin/env bash
# A game's data document authored in a session, over a copy of
# Plaza's action map: the map read whole, its move action's shown name set
# and written, its members kept in their order; undone back to its very
# bytes and redone; a value the map's reader refuses, a path through an
# action it does not hold, and a scene's operation refused with the file
# untouched. And a skeleton and a clip of Plaza's walker: read, a
# bone's translation and a key's time set through their places, a key taken
# away by its place and a place past the tracks refused.
#
# usage: author_data.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3
trap 'echo "author_data: failed at line $LINENO"; cut -c1-600 "$work/replies" 2>/dev/null' ERR

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/plaza/plaza.actions" "$work/root/plaza.actions"
cp "$work/root/plaza.actions" "$work/original.actions"
game="$repository/games/plaza/plaza.game"
move=2b8c866778ceb662
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
set_value() {
    echo '{"operation":"data.set","path":"'"$1"'","value":'"$2"'}'
}
read_data='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"data.read"}]}'

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"plaza.actions","queries":'"$read_data"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"plaza.actions","request":'"$(request "$(set_value "actions[actionId=$move].displayName" '"Walk"')")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"plaza.actions","queries":'"$read_data"'}'
    echo '{"kind":"authoring.undo","id":5,"scene":"plaza.actions"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"answer":{"data":{"kind":"input.actions","formatVersion":1,"actions":\[{"actionId":"'$move'","name":"move","valueType":"axis2d","displayName":"Move"'
reply 3 | grep -q '"written":true,.*"undoable":1,"redoable":0'
reply 4 | grep -q '"actionId":"'$move'","name":"move","valueType":"axis2d","displayName":"Walk"'
# Undone, the map is its very bytes again.
reply 5 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/plaza.actions" "$work/original.actions"

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"plaza.actions","request":'"$(request "$(set_value "actions[actionId=$move].displayName" '"Walk"')")"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"plaza.actions"}'
    echo '{"kind":"authoring.redo","id":4,"scene":"plaza.actions"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"plaza.actions","request":'"$(request "$(set_value "actions[actionId=$move].valueType" '"sideways"')")"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"plaza.actions","request":'"$(request "$(set_value "actions[actionId=0000000000000001].bindings" '[]')")"'}'
    echo '{"kind":"authoring.apply","id":7,"scene":"plaza.actions","request":'"$(request '{"operation":"scene.destroy_entity","entity":"6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30"}')"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 4 | grep -q '"undoable":1,"redoable":0'
grep -q '"displayName": "Walk"' "$work/root/plaza.actions"
cp "$work/root/plaza.actions" "$work/redone.actions"
reply 5 | grep -q '"code":"validation_failed",.*"message":"the action map does not read: a value type is bool, axis1d, axis2d, or pose at \$.actions\[0\].valueType"'
reply 6 | grep -q '"code":"target_not_found"'
reply 7 | grep -q '"code":"validation_failed",.*"message":"a data document'"'"'s only operation is data.set"'
cmp "$work/root/plaza.actions" "$work/redone.actions"
# The redone map differs from the original only in the shown name.
[[ "$(diff "$work/original.actions" "$work/redone.actions" | grep -c '^[<>]')" -eq 2 ]]

mkdir -p "$work/root/stroll"
cp "$repository/games/plaza/stroll/rig.rfanim" "$repository/games/plaza/stroll/walk.rfanim" "$work/root/stroll/"
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"stroll/rig.rfanim","queries":'"$read_data"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"stroll/rig.rfanim","request":'"$(request "$(set_value "bones[1].translation" '[0,0.55,0]')")"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"stroll/walk.rfanim","request":'"$(request "$(set_value "tracks[0].keys[1].time" '0.25')")"'}'
    echo '{"kind":"authoring.apply","id":5,"scene":"stroll/walk.rfanim","request":'"$(request "$(set_value "tracks[0].keys[0]" 'null')")"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"stroll/walk.rfanim","request":'"$(request "$(set_value "tracks[9].channel" '"rotation"')")"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"answer":{"data":{"formatVersion":1,"kind":"animation.skeleton","bones":\[{'
reply 3 | grep -q '"written":true,'
python3 -c 'import json,sys; b=json.load(open(sys.argv[1]))["bones"][1]; assert b["name"]=="spine" and b["translation"]==[0,0.55,0]' "$work/root/stroll/rig.rfanim"
reply 4 | grep -q '"written":true,'
python3 -c 'import json,sys; k=json.load(open(sys.argv[1]))["tracks"][0]["keys"]; assert [e["time"] for e in k]==[0,0.25]' "$work/root/stroll/walk.rfanim"
reply 5 | grep -q '"code":"validation_failed",.*"message":"an item named by its place is set, never taken away'
reply 6 | grep -q '"code":"target_not_found"'
echo "authored plaza's action map, a skeleton, and a clip in a session"
