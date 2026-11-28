#!/usr/bin/env bash
# Plays one sample game in the arena (a server and headless bots in one
# process) with the tree as it is now, and prints what the run did: the
# records it logged at warning or above, and every summary record. Exits
# non-zero when the arena failed or logged an error.
#
#   verify_game.sh [options] <game>
#
#   <game>          a folder under games/ (runners, plaza, crates, ...); its
#                   description is <game>/<game>.game, or the folder's only
#                   one, or the one --set kest.game names
#   --tree DIR      the build tree (default out/clang-development)
#   --bots N        how many bots join (default 4)
#   --seconds S     how long the run plays, at 120 iterations a second (default 10)
#   --draw          also draw the first two bots' views offscreen, as two
#                   local players see them, and write the last frame as
#                   frame.png (needs Maul RHI and an adapter)
#   --set KEY=VALUE add a configuration line, or replace one written by
#                   default; may be repeated
#
# Run from the repository root. Builds and the run go through the
# workspace's heavy-job wrapper where it is installed. The work folder,
# with the configuration, the full log and the frame, is printed at the
# end and kept for the pull request's evidence.
set -euo pipefail

tree=out/clang-development
bots=4
seconds=10
draw=0
extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --tree) tree=$2; shift 2 ;;
        --bots) bots=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --draw) draw=1; shift ;;
        --set) extra+=("$2"); shift 2 ;;
        -*) echo "unknown option $1" >&2; exit 64 ;;
        *) break ;;
    esac
done
[ $# -eq 1 ] || { sed -n '7,19p' "$0" >&2; exit 64; }
game=$1

# Paths as the programs read them: Git's bash on Windows names D:/a as
# /d/a, which only its own tools understand.
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

root=$(native "$(git rev-parse --show-toplevel)")
cd "$root"
here=$(cd "$(dirname "$0")" && pwd)
# The game's own description, named after its folder, or the folder's only
# one; a folder of several with none named so is refused rather than one
# taken at random, unless --set kest.game names it.
named=""
for line in "${extra[@]+"${extra[@]}"}"; do
    [ "${line%%=*}" != kest.game ] || named=1
done
descriptions=()
while IFS= read -r found; do
    descriptions+=("$found")
done < <(find "games/$game" -maxdepth 1 -name '*.game' | sort)
if [ -f "games/$game/$game.game" ]; then
    description="games/$game/$game.game"
elif [ "${#descriptions[@]}" -eq 1 ]; then
    description=${descriptions[0]}
elif [ -n "$named" ] && [ -d "games/$game" ]; then
    description=""
elif [ "${#descriptions[@]}" -eq 0 ]; then
    echo "no games/$game/*.game" >&2
    exit 64
else
    echo "games/$game holds several descriptions and none is $game.game: ${descriptions[*]}; name one with --set kest.game=<path>" >&2
    exit 64
fi

# The workspace's heavy-job wrapper, found above the checkout.
wrapper=""
dir=$root
while [ "$dir" != "$(dirname "$dir")" ]; do
    dir=$(dirname "$dir")
    if [ -f "$dir/tools/heavy.sh" ]; then
        wrapper=$dir/tools/heavy.sh
        break
    fi
done
heavy() {
    if [ -n "$wrapper" ]; then "$wrapper" "$@"; else "$@"; fi
}

work=$(native "$(mktemp -d "${TMPDIR:-/tmp}/verify-$game.XXXXXX")")
echo "== build"
heavy cmake --build "$tree" --target rawframe_host_arena rawframe_host_cook_tool >"$work/build.log" 2>&1 ||
    { tail -n 40 "$work/build.log"; echo "build failed; full log: $work/build.log" >&2; exit 1; }

# The cache lives in the tree so a second run cooks only what changed.
echo "== cook"
case "$tree" in /* | [A-Za-z]:/*) cache="$tree" ;; *) cache="$root/$tree" ;; esac
cache="$cache/verify_cook_cache/$game"
mkdir -p "$cache"
heavy "$tree/hosts/cook/rawframe-cook" "games/$game" "$work/content" "$cache" >"$work/cook.log" 2>&1 ||
    { tail -n 40 "$work/cook.log"; echo "cook failed; full log: $work/cook.log" >&2; exit 1; }
grep -q 'failed 0' "$work/cook.log" ||
    { grep -v '^cooking ' "$work/cook.log" | tail -n 20; echo "cook refused sources; full log: $work/cook.log" >&2; exit 1; }

# A key given with --set replaces the default line for it: the
# configuration refuses a key written twice.
given=" "
for line in "${extra[@]+"${extra[@]}"}"; do
    given="$given${line%%=*} "
done
default() {
    case "$given" in *" $1 "*) ;; *) echo "$1 = $2" ;; esac
}

{
    default host.maximum_iterations "$((seconds * 120))"
    default host.iteration_rate "120"
    default world.tick_rate "60"
    [ -z "$description" ] || default kest.game "$root/$description"
    default content.root "$work/content"
    default network.loopback.latency_ms "10"
    default replication.endpoint "arena"
    default bots.count "$bots"
    default bots.endpoint "arena"
    # What is checked is what the run did, not how fast a shared machine
    # ticks: without these a loaded machine stops the run as overloaded.
    default world.degraded_ms "600000"
    default world.overload_ms "0"
    if [ "$draw" = 1 ]; then
        # What is drawn is a local player's view: two when there are two
        # bots to be them, as the drawn arena tests do.
        default bots.local_players "$((bots < 2 ? bots : 2))"
        default render.device "any"
        default render.offscreen "true"
        default render.width "640"
        default render.height "360"
        default render.capture "$work/frame.tga"
        default render.read_every "8"
    fi
    for line in "${extra[@]+"${extra[@]}"}"; do
        echo "${line%%=*} = ${line#*=}"
    done
} >"$work/arena.conf"

echo "== play $game: $bots bots, $seconds s"
status=0
heavy "$tree/hosts/arena/rawframe-arena" --config "$work/arena.conf" >"$work/run.ndjson" 2>"$work/run.err" || status=$?

frame=""
if [ "$draw" = 1 ]; then
    if [ -f "$work/frame.tga" ]; then
        python3 "$here/summarize.py" --png "$work/frame.tga" "$work/frame.png"
        frame="$work/frame.png"
    else
        echo "no frame was written: the tree draws nothing (no Maul RHI) or no adapter answered" >&2
        [ "$status" != 0 ] || status=1
    fi
fi

python3 "$here/summarize.py" --status "$status" --stderr "$work/run.err" "$work/run.ndjson" || status=$?
echo "== work folder: $work"
[ -z "$frame" ] || echo "== frame: $frame"
exit "$status"
