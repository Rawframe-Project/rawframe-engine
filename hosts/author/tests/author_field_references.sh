#!/usr/bin/env bash
# A source's references with the scenes that name it in a field:
# Runners cooked in a session; its tiles picture is used by the game's
# description, which declares it, and by the level and the platform, whose
# sprites hold its identity in their texture fields; the runner's picture,
# which no scene names, is used by the description alone.
#
# usage: author_field_references.sh <rawframe-author> <rawframe-cook> <repository> <work directory>
set -euo pipefail

author=$1
cook=$2
repository=$3
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
work="$(native "$4")"
trap 'echo "author_field_references: failed at line $LINENO"; grep -v "\"kind\":\"authoring.progress\"" "$work/replies" 2>/dev/null | cut -c1-600' ERR
rm -rf "$work"
mkdir -p "$work"
game="$(native "$repository")/games/runners/runners.game"
reply() { grep "\"id\":$1," "$work/replies"; }

{
    echo '{"kind":"authoring.hello","id":1,"surfaceGeneration":1}'
    echo '{"kind":"authoring.cook","id":2,"output":"'"$work/content"'","cache":null}'
    # Four minutes only bounds a cook on a sanitized tree under load.
    for _ in $(seq 2400); do
        grep -q '"id":2,"answer"\|"id":2,"error"' "$work/replies" 2>/dev/null && break
        sleep 0.1
    done
    echo '{"kind":"authoring.asset_references","id":3,"asset":"tiles.png"}'
    echo '{"kind":"authoring.asset_references","id":4,"asset":"runner.png"}'
    echo '{"kind":"authoring.end","id":5}'
} | "$author" session "$game" --cook "$cook" >"$work/replies" || true
reply 2 | grep -q '"kind":"authoring.cooked"'
reply 3 | grep -q '"asset":"tiles.png","uses":\[\],"usedBy":\["level.scene","platform.scene","runners.game"\]}'
reply 4 | grep -q '"asset":"runner.png","uses":\[\],"usedBy":\["runners.game"\]}'
echo "a source's references named its scenes' fields"
