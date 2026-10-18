#!/usr/bin/env bash
# An agent changes and cooks a game over the Model Context Protocol (D773):
# a copy of the plaza greeted, its tools listed from the session's own
# discovery, an entity made, given a body named as the catalog names it,
# and a field set, each written to the scene; a value of the wrong kind and
# an input the operation lacks refused as failed calls; a batch kept as one
# undo step and taken back; an entity chosen; two scenes changed as one,
# and neither where one part fails (D889); a tool and a method that do not
# exist refused as protocol errors; then the game cooked as a long-running
# call telling its progress, and a second cook cancelled by the agent
# answering cancelled.
#
# usage: author_mcp.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
# Paths as the programs under test read them: Git's bash on Windows names
# D:/a as /d/a, which only its own tools understand (D237).
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
rm -rf "$work"
mkdir -p "$work"
cp -R "$repository/games/plaza" "$work/plaza"
game="$work/plaza/plaza.game"
crate=5f0e4c2a-1b3d-4e5f-8a9b-0c1d2e3f4a5b
lamp=6a1f5d3b-2c4e-4f6a-9b0c-1d2e3f4a5b6c
post=7b2a6e4c-3d5f-4a7b-8c1d-2e3f4a5b6c7d
stone=8c3b7f5d-4e6a-4b8c-9d2e-3f4a5b6c7d8e

# The reply to the call of id $1.
reply() { grep "\"id\":$1," "$work/replies"; }
call() {
    printf '{"jsonrpc":"2.0","id":%s,"method":"tools/call","params":{"name":"%s","arguments":%s}}\n' "$1" "$2" "$3"
}

{
    echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"author_mcp","version":"1"}}}'
    echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    call 3 scene_create_entity '{"document":"gate.scene","entity":"'$crate'","name":"crate","place":null}'
    call 4 scene_add_component '{"document":"gate.scene","entity":"'$crate'","component":"rawframe.physics3d.body"}'
    call 5 scene_set_field '{"document":"gate.scene","entity":"'$crate'","component":"rawframe.physics3d.body","field":"width","value":{"real":2.5}}'
    call 6 scene_set_field '{"document":"gate.scene","entity":"'$crate'","component":"rawframe.physics3d.body","field":"width","value":{"real":"wide"}}'
    call 7 scene_rename_entity '{"document":"gate.scene","entity":"'$crate'","name":"box","colour":"red"}'
    call 8 document_apply '{"document":"gate.scene","operations":[{"operation":"scene.rename_entity","entity":"'$crate'","name":"box"},{"operation":"scene.set_field","entity":"'$crate'","component":"rawframe.physics3d.body","field":"height","value":{"real":1}}]}'
    call 9 document_undo '{"document":"gate.scene"}'
    call 14 scene_select '{"document":"gate.scene","entities":["'$crate'"]}'
    call 15 document_create_scene '{"document":"yard.scene"}'
    made() { echo '{"operation":"scene.create_entity","entity":"'$1'","name":"'$2'"}'; }
    call 16 document_apply_together '{"documents":[{"document":"gate.scene","operations":['"$(made $lamp lamp)"']},{"document":"yard.scene","operations":['"$(made $post post)"']}]}'
    call 17 document_apply_together '{"documents":[{"document":"yard.scene","operations":['"$(made $stone stone)"']},{"document":"gate.scene","operations":[{"operation":"scene.rename_entity","entity":"'$stone'","name":"none"}]}]}'
    call 10 nothing '{}'
    echo '{"jsonrpc":"2.0","id":11,"method":"resources/list"}'
    printf '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"game_cook","arguments":{"output":"%s"},"_meta":{"progressToken":"cooking"}}}\n' "$work/content"
    # Each wait ends as the reply comes; four minutes only bounds a cook
    # on a sanitized tree under load (D507).
    for _ in $(seq 2400); do
        grep -q '"id":12,"result"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    printf '{"jsonrpc":"2.0","id":13,"method":"tools/call","params":{"name":"game_cook","arguments":{"output":"%s"}}}\n' "$work/other"
    echo '{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":13,"reason":"changed my mind"}}'
    for _ in $(seq 2400); do
        grep -q '"id":13,"result"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
} | "$author" mcp "$game" --cook "$cook" >"$work/replies"

reply 1 | grep -q '"protocolVersion":"2025-06-18"'
# The tools are the session's operations and its own verbs.
reply 2 | grep -q '"name":"scene_set_field"'
reply 2 | grep -q '"name":"materials_set_node_input"'
reply 2 | grep -q '"name":"game_describe"'
reply 2 | grep -q '"name":"extension_run"'
reply 2 | grep -q '"name":"game_profile"'
reply 2 | grep -q '"name":"preview_frame"'
for id in 3 4 5 8 9; do
    reply $id | grep -q '"isError":false'
done
reply 6 | grep -q '"isError":true'
# What is chosen, and two scenes changed as one, or neither (D889).
reply 14 | grep -q "$crate"
for id in 14 15 16; do
    reply $id | grep -q '"isError":false'
done
reply 17 | grep -q '"isError":true'
grep -q '"name": "lamp"' "$work/plaza/gate.scene"
grep -q '"name": "post"' "$work/plaza/yard.scene"
! grep -q '"name": "stone"' "$work/plaza/yard.scene"
reply 7 | grep -q 'has no input colour'
reply 8 | grep -q '\\"undoable\\":4'
reply 9 | grep -q '\\"undoable\\":3,\\"redoable\\":1'
reply 10 | grep -q '"code":-32602'
reply 11 | grep -q '"code":-32601'
# The scene holds what was kept: the crate with its width, not the batch
# taken back.
grep -A3 '"name": "crate"' "$work/plaza/gate.scene" | tr -d ' \n' |
    grep -q '"components":{"rawframe.physics3d.body":{"width":2.5'
! grep -q '"name": "box"' "$work/plaza/gate.scene"
steps=$(grep -c '"method":"notifications/progress","params":{"progressToken":"cooking"' "$work/replies")
[[ $steps -gt 0 ]]
reply 12 | grep -q 'authoring.cooked\\",\\"cooked\\":'"$steps"
reply 12 | grep -q '"isError":false'
reply 13 | grep -q 'requested'
[[ -s "$work/content/content.manifest" ]]
echo "an agent authored and cooked the plaza over MCP, $steps sources"
