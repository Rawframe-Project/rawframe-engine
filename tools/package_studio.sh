#!/usr/bin/env bash
# Packages Studio for an author with no build of their own (D768): Studio,
# the cook tool, the dedicated server, the client, the authoring tool, and
# the thumbnail tool from one build tree, beside the templates and the
# sources of the games named, which Studio's start screen offers as
# samples. Studio opens as an application does, with no configuration and
# no script (D920): double-clicked, it shows its start screen. A Windows
# tree's programs take the DLLs they import from the tree, and the C++
# runtime's from the system. Each game also has an MCP configuration giving
# an agent the game's authoring tools without Studio (D773):
# `claude --mcp-config <game>.mcp.json` from the folder.
#
#   tools/package_studio.sh <build tree> <folder> <game>...
set -euo pipefail
cd "$(dirname "$0")/.."

tree="$1"
into="$2"
shift 2
exe=""
[ -e "$tree/hosts/studio/rawframe-studio.exe" ] && exe=.exe
mkdir -p "$into/games"
cp "$tree/hosts/studio/rawframe-studio$exe" "$tree/hosts/cook/rawframe-cook$exe" \
    "$tree/hosts/dedicated_server/rawframe-server$exe" "$tree/hosts/client/rawframe-client$exe" \
    "$tree/hosts/author/rawframe-author$exe" "$into/"
# And the tool that draws a mesh's thumbnail, where the tree has the
# scene's renderer (D899).
thumbnail="$tree/hosts/thumbnail/rawframe-thumbnail$exe"
[ -e "$thumbnail" ] && cp "$thumbnail" "$into/"
# The templates a new game is made from (D780).
cp -R templates "$into/templates"
for game in "$@"; do
    cp -R "games/$game" "$into/games/$game"
    cat >"$into/$game.mcp.json" <<MCP
{"mcpServers": {"rawframe-$game": {"command": "./rawframe-author$exe", "args": ["mcp", "games/$game/$game.game"]}}}
MCP
done
if [ -n "$exe" ]; then
    for program in "$into"/*.exe; do
        for dll in $(llvm-objdump -p "$program" | sed -n 's/^ *DLL Name: //p' | tr -d '\r'); do
            [ -e "$into/$dll" ] && continue
            found="$(find "$tree" -name "$dll" -print -quit)"
            [ -z "$found" ] && [ -e "/c/Windows/System32/$dll" ] && case "${dll,,}" in
            vcruntime* | msvcp* | concrt*) found="/c/Windows/System32/$dll" ;;
            esac
            [ -n "$found" ] && cp "$found" "$into/"
        done
    done
fi
ls "$into"
