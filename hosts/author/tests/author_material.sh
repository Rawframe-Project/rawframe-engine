#!/usr/bin/env bash
# A game's material instance authored in a session, over a copy of
# Gale's shore and the rock it is an instance of: the instance read whole
# with its base's interface, its tint changed and a texture override made
# by one atomic request, written to the file in its canonical form; undone
# back to its very bytes and redone; its last override unset, an input not
# named as Kest names are, and a scene's operation refused with the file
# untouched; and its history listed. The rock, a material graph, authored
# too: read whole, its surface's roughness set and undone to its
# very bytes, and an instance's operation refused.
#
# usage: author_material.sh <rawframe-author> <repository> <work directory>
set -euo pipefail

author=$1
repository=$2
work=$3

rm -rf "$work"
mkdir -p "$work/root"
cp "$repository/games/gale/shore.rfmaterial" "$repository/games/gale/rock.rfmaterial" "$work/root/"
cp "$work/root/shore.rfmaterial" "$work/original.rfmaterial"
game="$repository/games/gale/gale.game"
request() {
    echo '{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":['"$1"']}'
}
set_override() {
    echo '{"operation":"materials.set_override","input":"'"$1"'","value":'"$2"'}'
}
read_instance='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"materials.read_instance"}]}'

# The reply to the record of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"shore.rfmaterial","queries":'"$read_instance"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"shore.rfmaterial","request":'"$(request "$(set_override tint '[0.6,0.2,0.1]'),$(set_override stone '"ea9d135c1c4e2806"')")"'}'
    echo '{"kind":"authoring.read","id":4,"scene":"shore.rfmaterial","queries":'"$read_instance"'}'
    echo '{"kind":"authoring.undo","id":5,"scene":"shore.rfmaterial"}'
} | "$author" session "$game" "$work/root" >"$work/replies"
reply 2 | grep -q '"answer":{"instance":{"formatVersion":1,"kind":"surface","base":"rock.rfmaterial","overrides":{"tint":\[0.3,0.29,0.28\]}},"interface":{"inputs":{"stone":{"default":"ea9d135c1c4e2805","type":"texture_2d"}'
reply 3 | grep -q '"written":true,"reopened":false,"undoable":1,"redoable":0,"selection":\[\],"view":null,"results":\[{"deltas":2}\]'
# Overrides stand in name order, as an instance reads.
reply 4 | grep -q '"overrides":{"stone":"ea9d135c1c4e2806","tint":\[0.6,0.2,0.1\]}'
reply 5 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/shore.rfmaterial" "$work/original.rfmaterial"

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.apply","id":2,"scene":"shore.rfmaterial","request":'"$(request "$(set_override tint '[0.6,0.2,0.1]')")"'}'
    echo '{"kind":"authoring.undo","id":3,"scene":"shore.rfmaterial"}'
    echo '{"kind":"authoring.redo","id":4,"scene":"shore.rfmaterial"}'
    echo '{"kind":"authoring.apply","id":5,"scene":"shore.rfmaterial","request":'"$(request "$(set_override tint null)")"'}'
    echo '{"kind":"authoring.apply","id":6,"scene":"shore.rfmaterial","request":'"$(request "$(set_override Tint 1)")"'}'
    echo '{"kind":"authoring.apply","id":7,"scene":"shore.rfmaterial","request":'"$(request '{"operation":"scene.destroy_entity","entity":"6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30"}')"'}'
    echo '{"kind":"authoring.history","id":8,"scene":"shore.rfmaterial"}'
    echo '{"kind":"authoring.read","id":9,"scene":"rock.rfmaterial","queries":'"$read_instance"'}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 4 | grep -q '"undoable":1,"redoable":0'
grep -q '^      0.6,$' "$work/root/shore.rfmaterial"
cp "$work/root/shore.rfmaterial" "$work/redone.rfmaterial"
reply 5 | grep -q '"code":"validation_failed".*"message":"the material instance does not read: '
reply 6 | grep -q '"code":"validation_failed".*"message":"an override names an input or a switch as a Kest name is"'
reply 7 | grep -q '"code":"validation_failed".*"message":"the operation does not change a material instance"'
reply 8 | grep -q '"entries":\[{"summary":"set tint","deltas":1,"applied":true,"kind":"change"}\]'
# A graph is no instance to author here.
reply 9 | grep -q '"code":"validation_failed"'
cmp "$work/root/shore.rfmaterial" "$work/redone.rfmaterial"

cp "$work/root/rock.rfmaterial" "$work/rock.original"
read_graph='{"formatVersion":1,"kind":"authoring.query","queries":[{"operation":"materials.read_graph"}]}'
rough='{"operation":"materials.set_node_input","node":"60c0000000000005","input":"specular_roughness","value":0.4}'
{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.read","id":2,"scene":"rock.rfmaterial","queries":'"$read_graph"'}'
    echo '{"kind":"authoring.apply","id":3,"scene":"rock.rfmaterial","request":'"$(request "$rough")"'}'
    echo '{"kind":"authoring.apply","id":4,"scene":"rock.rfmaterial","request":'"$(request "$(set_override tint 1)")"'}'
    echo '{"kind":"authoring.history","id":5,"scene":"rock.rfmaterial"}'
    echo '{"kind":"authoring.undo","id":6,"scene":"rock.rfmaterial"}'
} | "$author" session "$game" "$work/root" >"$work/replies" || true
reply 2 | grep -q '"answer":{"graph":{"formatVersion":1,"kind":"surface","interface":{"inputs":{"stone"'
reply 3 | grep -q '"written":true,.*"results":\[{"deltas":1}\]'
reply 4 | grep -q '"code":"validation_failed".*"message":"the operation does not change a material graph"'
reply 5 | grep -q '"entries":\[{"summary":"set 60c00000.specular_roughness","deltas":1,"applied":true,"kind":"change"}\]'
reply 6 | grep -q '"undoable":0,"redoable":1'
cmp "$work/root/rock.rfmaterial" "$work/rock.original"
echo "authored gale's shore in a session"
