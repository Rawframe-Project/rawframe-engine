#!/usr/bin/env bash
# An agent changes and cooks a game over the Model Context Protocol (D773):
# a copy of the plaza greeted, its tools listed from the session's own
# discovery, an entity made, given a body named as the catalog names it,
# and a field set, each written to the scene; a value of the wrong kind and
# an input the operation lacks refused as failed calls; a batch kept as one
# undo step and taken back; a tool and a method that do not exist refused
# as protocol errors; then the game cooked as a long-running call telling
# its progress, and a second cook cancelled by the agent answering
# cancelled.
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
reply 2 | grep -q '"name":"game_profile"'
reply 2 | grep -q '"name":"preview_frame"'
for id in 3 4 5 8 9; do
    reply $id | grep -q '"isError":false'
done
reply 6 | grep -q '"isError":true'
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
