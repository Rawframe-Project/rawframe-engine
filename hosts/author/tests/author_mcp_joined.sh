#!/usr/bin/env bash
# An agent a collaborator of a shared session (ADR-0085 section 7, D883):
# Runners' level hosted headless, an MCP client joined through
# `rawframe-author mcp --join` under the token named "claude", and Ben
# joined headless. The agent's tools are the session's; its rename of the
# ground is kept under its own name, which Ben hears; its undo takes back
# only its own change; it cooks through the host into its own directory
# there (D885); and the level holds what the agent left.
#
# usage: author_mcp_joined.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
rm -rf "$work"
mkdir -p "$work"
trap 'echo "failed at line $LINENO"; for each in "$work"/*.out; do [ -f "$each" ] && { echo "== $each"; tail -20 "$each"; }; done' ERR
cp -R "$repository/games/runners" "$work/hosted"
port=$(python3 "$(dirname "$0")/../../../tools/free_port.py")
for each in claude ben; do
    python3 -c 'import secrets; print(secrets.token_hex(24))' >"$work/$each.token"
done
printf '%s claude read edit cook\n%s ben read edit\n' "$(cat "$work/claude.token")" "$(cat "$work/ben.token")" >"$work/tokens"
"$author" host "$work/hosted/runners.game" --endpoint "127.0.0.1:$port" --tokens "$work/tokens" \
    --fingerprint-file "$work/pin" --cooks "$work/cooks" --cook "$cook" >"$work/host.log" 2>&1 &
hosting=$!
trap 'kill "$hosting" 2>/dev/null || true' EXIT
for _ in $(seq 100); do
    [[ -s "$work/pin" ]] && break
    sleep 0.1
done
heard() {
    for _ in $(seq 600); do
        grep -q "$2" "$work/$1" 2>/dev/null && return 0
        sleep 0.05
    done
    return 1
}
ground=c5400cf8-4b07-4d16-84c2-951852c66b40
wall=87e4430f-6ec1-4301-be72-ce8187494c34
call() {
    printf '{"jsonrpc":"2.0","id":%s,"method":"tools/call","params":{"name":"%s","arguments":%s}}\n' "$1" "$2" "$3"
}
rename() {
    echo '{"kind":"authoring.apply","id":'"$1"',"scene":"level.scene","request":{"formatVersion":1,"kind":"authoring.request","batch":"atomic","operations":[{"operation":"scene.rename_entity","entity":"'"$2"'","name":"'"$3"'"}]}}'
}

# Ben joins first, renames a wall once the agent has renamed the ground,
# and asks who changed what until he has heard the agent's undo.
{
    echo '{"kind":"authoring.presence","id":1}'
    heard agent.out '"id":3,"result"'
    rename 2 "$wall" "bens wall"
    for each in $(seq 10 300); do
        [ "$(grep -c '"kind":"authoring.changed","author":"claude"' "$work/ben.out")" -ge 2 ] && break
        echo '{"kind":"authoring.presence","id":'"$each"'}'
        sleep 0.05
    done
} | "$author" join "127.0.0.1:$port" "$work/pin" "$work/ben.token" >"$work/ben.out" 2>&1 &
benning=$!
{
    echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"author_mcp_joined","version":"1"}}}'
    echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    heard ben.out '"id":1,"answer"'
    call 3 scene_rename_entity '{"document":"level.scene","entity":"'$ground'","name":"claudes ground"}'
    heard ben.out '"id":2,"answer"'
    call 4 document_undo '{"document":"level.scene"}'
    printf '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"game_cook","arguments":{"output":"content"},"_meta":{"progressToken":"cooking"}}}\n'
    heard agent.out '"id":5,"result"'
} | "$author" mcp --join "127.0.0.1:$port" "$work/pin" "$work/claude.token" >"$work/agent.out" 2>&1 &
agenting=$!
wait "$agenting" || true
wait "$benning" || true
grep -q '"id":2,"result":{"tools":\[.*"name":"scene_rename_entity"' "$work/agent.out"
grep -q '"id":3,"result":{"content":\[' "$work/agent.out"
grep -q '"id":4,"result":{"content":\[' "$work/agent.out"
grep -q '"method":"notifications/progress","params":{"progressToken":"cooking"' "$work/agent.out"
grep '"id":5,"result"' "$work/agent.out" | grep -q 'authoring.cooked'
[ -s "$work/cooks/claude/content/cook.receipt" ]
grep -q '{"kind":"authoring.changed","author":"claude","document":"level.scene"' "$work/ben.out"
# The agent's undo took back its own rename past Ben's later one.
grep -q '"bens wall"' "$work/hosted/level.scene"
grep -q '"ground"' "$work/hosted/level.scene"
if grep -q '"claudes ground"' "$work/hosted/level.scene"; then
    echo "the agent's undo did not take back its rename"
    exit 1
fi
echo "an agent shared a hosted session under its own name"
