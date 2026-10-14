#!/usr/bin/env bash
# A new game from a template (D780): two games made from the starter
# template share no identity, a game is not made over another or under a
# name that is not a Kest project's, and the new game plays: four bots move
# their boxes about its ground for five seconds. Run from the repository
# root.
#
#   new_game.sh <rawframe-author> <rawframe-arena>
set -euo pipefail

author="$1"
arena="$2"
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}
here="$(native "$PWD")"
work="$(native "$(mktemp -d)")"
trap 'rm -rf "$work"' EXIT

"$author" new "$here/templates/starter-3d" "$work/first"
"$author" new "$here/templates/starter-3d" "$work/second" >/dev/null
for each in first second; do
    [ -f "$work/$each/$each.game" ] || { echo "no $each.game"; exit 1; }
    grep -q "^project $each\$" "$work/$each/kest.project" || { echo "$each's project is not named so"; exit 1; }
done
# Nothing the template minted is in either game, nor one game's in the
# other's.
minted() {
    { grep -ho '"resourceId": "[0-9a-f]*"' "$1"/*.rfmeta; grep -o '^component [0-9a-f-]*' "$1"/*.game; } | sort
}
if [ -n "$(comm -12 <(minted "$work/first") <(minted "$work/second"))" ] ||
    [ -n "$(comm -12 <(minted "$work/first") <(minted "$here/templates/starter-3d"))" ]; then
    echo "an identity is shared"
    exit 1
fi
echo "identities: none shared"
if "$author" new "$here/templates/starter-3d" "$work/first" >"$work/again" ||
    ! grep -q '"conflict"\|AlreadyExists\|where nothing is' "$work/again"; then
    cat "$work/again"
    echo "made over a game"
    exit 1
fi
if "$author" new "$here/templates/starter-3d" "$work/Not a name" >"$work/named" || [ -e "$work/Not a name" ]; then
    cat "$work/named"
    echo "made under a name that is not a project's"
    exit 1
fi
echo "refused: over a game, and under a bad name"

cat >"$work/arena.conf" <<CONF
host.maximum_iterations = 600
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $work/first/first.game
network.loopback.latency_ms = 10
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
CONF
"$arena" --config "$work/arena.conf"
