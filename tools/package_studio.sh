#!/usr/bin/env bash
# Packages Studio for an author with no build of their own: Studio,
# the cook tool, the dedicated server, the client, the authoring tool, the
# thumbnail tool, the debug adapter with its VS Code extension, and
# what Export runs (the export and build tools, the launcher, the install
# tool, and the web player) from one build tree, with the shader
# toolchain where the machine has one, beside the templates and the
# sources of the games named, which Studio's start screen offers as
# samples. Studio opens as an application does, with no configuration and
# no script: double-clicked, it shows its start screen. A Windows
# tree's programs take the DLLs they import from the tree, and the C++
# runtime's from the system. Each game also has an MCP configuration giving
# an agent the game's authoring tools without Studio:
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
# scene's renderer.
thumbnail="$tree/hosts/thumbnail/rawframe-thumbnail$exe"
[ -e "$thumbnail" ] && cp "$thumbnail" "$into/"
# The debug adapter and the VS Code extension that starts it, so a game's
# Kest is debugged from an editor without Studio.
cp "$tree/hosts/debug/rawframe-debug$exe" "$into/"
mkdir -p "$into/vscode"
cp tools/vscode/* "$into/vscode/"
# What Studio's Export runs: the export tool finds the others beside it.
cp "$tree/hosts/export/rawframe-export$exe" "$tree/hosts/build/rawframe-build$exe" \
    "$tree/launcher/play/rawframe-play$exe" "$tree/launcher/install/rawframe-install$exe" "$into/"
# The shader toolchain, where this machine has one (RAWFRAME_SLANGC or
# slangc, dxc, spirv-cross, spirv-val, spirv-dis and spirv-as on the path),
# so the packaged cook builds a material graph the blob cannot fold:
# the generator and the sources it reads laid out as the engine's
# tree lays them, the programs in its bin, their libraries beside them on
# Windows and in its lib elsewhere, dxc's without their debugging symbols.
# Python is the machine's own.
slangc="${RAWFRAME_SLANGC:-$(command -v slangc || true)}"
dxc="$(command -v dxc || true)"
cross="$(command -v spirv-cross || true)"
validator="$(command -v spirv-val || true)"
disassembler="$(command -v spirv-dis || true)"
assembler="$(command -v spirv-as || true)"
if [ -n "$slangc" ] && [ -n "$dxc" ] && [ -n "$cross" ] && [ -n "$validator" ] && [ -n "$disassembler" ] &&
    [ -n "$assembler" ]; then
    shaders="$into/shader-tools"
    mkdir -p "$shaders/tools" "$shaders/third_party/maul-rhi/tools" "$shaders/third_party/maul-rhi/contract" \
        "$shaders/modules/render_scene_gpu/shaders" "$shaders/bin" "$shaders/lib"
    cp tools/gen_shaders.py "$shaders/tools/"
    cp third_party/maul-rhi/tools/*.py "$shaders/third_party/maul-rhi/tools/"
    cp third_party/maul-rhi/contract/mrhi.json "$shaders/third_party/maul-rhi/contract/"
    cp modules/render_scene_gpu/shaders/* "$shaders/modules/render_scene_gpu/shaders/"
    cp "$slangc" "$dxc" "$cross" "$validator" "$disassembler" "$assembler" "$shaders/bin/"
    if [ -n "$exe" ]; then
        cp "$(dirname "$slangc")"/*.dll "$(dirname "$dxc")"/*.dll "$shaders/bin/"
    else
        cp -R "$(dirname "$slangc")/../lib/." "$shaders/lib/"
        rm -rf "$shaders/lib/pkgconfig"
        for library in libdxcompiler.so libdxil.so; do
            strip --strip-unneeded -o "$shaders/lib/$library" "$(dirname "$dxc")/../lib/$library"
        done
    fi
else
    echo "package_studio.sh: no shader toolchain here; the packaged cook refuses a material graph the blob cannot fold" >&2
fi
# The web player a web export lays out, the same for every system:
# from the WebAssembly tree RAWFRAME_WEB_TREE names, else the shipping one
# beside the tree packaged, and the page from the sources.
web="${RAWFRAME_WEB_TREE:-$(dirname "$tree")/wasm-shipping}"
if [ -e "$web/hosts/web_client/rawframe-web-client.wasm" ]; then
    mkdir -p "$into/web/page"
    cp "$web/hosts/web_client/rawframe-web-client.wasm" "$web/third_party/maul-window/maul-window.mjs" \
        "$web/third_party/maul-rhi/maul-rhi.mjs" "$web/third_party/maul-ui/maul-ui.mjs" "$into/web/"
    cp hosts/web_client/page/* "$into/web/page/"
else
    echo "package_studio.sh: no web player at $web; the packaged Studio's web export says so" >&2
fi
# The templates a new game is made from.
cp -R templates "$into/templates"
for game in "$@"; do
    cp -R "games/$game" "$into/games/$game"
    cat >"$into/$game.mcp.json" <<MCP
{"mcpServers": {"rawframe-$game": {"command": "./rawframe-author$exe", "args": ["mcp", "games/$game/$game.game"]}}}
MCP
done
# On Linux, its icon and a desktop entry beside it, double-clicked from
# the folder or copied to ~/.local/share/applications to sit in the
# desktop's menu, naming where the folder is now.
if [ -z "$exe" ]; then
    cp hosts/studio/resources/rawframe-studio.png "$into/"
    at="$(cd "$into" && pwd)"
    cat >"$into/rawframe-studio.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Rawframe Studio
Comment=Make games with the Rawframe engine
Exec="$at/rawframe-studio"
Path=$at
Icon=$at/rawframe-studio.png
Terminal=false
Categories=Development;IDE;
StartupWMClass=rawframe-studio
DESKTOP
    chmod +x "$into/rawframe-studio.desktop"
fi
if [ -n "$exe" ]; then
    # Each program's beside it: the toolchain's in its bin too.
    for program in "$into"/*.exe "$into"/shader-tools/bin/*.exe; do
        [ -e "$program" ] || continue
        beside="$(dirname "$program")"
        for dll in $(llvm-objdump -p "$program" | sed -n 's/^ *DLL Name: //p' | tr -d '\r'); do
            [ -e "$beside/$dll" ] && continue
            found="$(find "$tree" -name "$dll" -print -quit)"
            [ -z "$found" ] && [ -e "/c/Windows/System32/$dll" ] && case "${dll,,}" in
            vcruntime* | msvcp* | concrt*) found="/c/Windows/System32/$dll" ;;
            esac
            [ -n "$found" ] && cp "$found" "$beside/"
        done
    done
fi
ls "$into"
