#!/usr/bin/env bash
# A server and bots over QUIC agree on a Composition before play: runners'
# cooked content is packed, signed, and installed in a library, two
# CompositionRecords name the same Build under two profiles, and bots
# naming the server's Composition are admitted while bots naming the other
# are not. The server reads the game from its directory and the bots from
# the Composition's cooked description, and the two are one game to
# admission. Prints the bots' summaries, the agreeing run's first. Run from
# the repository root.
#
#   composition.sh <rawframe-server> <rawframe-bots> <rawframe-build> <cooked content> <work directory>
set -euo pipefail

server=$1
bots=$2
build=$3
cooked=$4
work=$5
play="$(dirname "$0")/play.sh"

rm -rf "$work"
mkdir -p "$work"
library=$work/library
kid=$("$build" key rawframe "$library/keys" | cut -d' ' -f2)
"$build" "$cooked" "$work/build" rawframe/runners 0.1.0 linux x86_64 client build.development tool \
    "$library/keys/$kid.key" >/dev/null
root=$("$build" install "$work/build" "$library" | cut -d' ' -f2)
"$build" compose "$library" "$root" tool "$work/tool.composition" >/dev/null
"$build" compose "$library" "$root" other "$work/other.composition" >/dev/null

settings() {
    printf 'content.composition = %s\ncontent.library = %s\n' "$1" "$library" >"$2"
}
settings "$work/tool.composition" "$work/server.settings"
settings "$work/tool.composition" "$work/agree.settings"
settings "$work/other.composition" "$work/differ.settings"
# The bots name the game by its cooked description's identity.
resource=$(sed -n 's/.*"resourceId": "\([0-9a-f]*\)".*/\1/p' games/runners/runners.game.rfmeta)
printf 'kest.game_resource = %s\n' "$resource" | tee -a "$work/agree.settings" >>"$work/differ.settings"

game=games/runners/runners.game
"$play" "$server" "$bots" 2 1 240 "$game" "$work/server.settings" "$work/agree.settings" "" |
    grep -o '"code":"bots_summary".*"admitted":[0-9]*'
"$play" "$server" "$bots" 2 1 240 "$game" "$work/server.settings" "$work/differ.settings" "" |
    grep -o '"code":"bots_summary".*"admitted":[0-9]*'
