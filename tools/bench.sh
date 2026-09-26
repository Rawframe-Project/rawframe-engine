#!/usr/bin/env bash
# The engine against SPEC-0013's tick budget: 64 bots play each sample game
# for six seconds in one arena process built for shipping, and the server
# World's tick durations are read from its summary. The crowd is SPEC-0013's
# canonical workload: 10,000 entities, 2,048 of them walking.
#
#   tools/bench.sh          measure, and add a dated, commit-stamped row for
#                           each game to bench/results.tsv
#   tools/bench.sh check    measure and record nothing; fail on a tick past
#                           SPEC-0013's ceilings (p50 5 ms, p95 8.33 ms, p99
#                           12.5 ms), or on a p95 past twice this machine's
#                           recorded one (the median of its last three rows)
#                           and half a millisecond
#
# Needs out/clang-shipping built; the full check builds it first.
set -euo pipefail
cd "$(dirname "$0")/.."

mode="${1:-record}"
build=out/clang-shipping
results=bench/results.tsv
machine="$(hostname)"
commit="$(git rev-parse --short=12 HEAD)"
# A measurement of a tree that is not the commit says so.
git diff --quiet HEAD || commit="$commit+"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The plaza's mound is a cooked mesh.
"$build/hosts/cook/rawframe-cook" games/plaza "$work/plaza" "$work/cache" >/dev/null

mkdir -p bench
[ -f "$results" ] || printf 'date\tcommit\tmachine\tgame\tbots\tticks\tp50_ms\tp95_ms\tp99_ms\n' >"$results"

failures=0
for game in arena runners plaza crowd; do
    {
        echo "host.maximum_iterations = 720"
        echo "host.iteration_rate = 120"
        echo "world.tick_rate = 60"
        echo "kest.game = $PWD/games/$game/$game.game"
        echo "network.loopback.latency_ms = 10"
        echo "replication.endpoint = arena"
        echo "bots.count = 64"
        echo "bots.endpoint = arena"
        if [ "$game" = plaza ]; then echo "content.root = $work/plaza"; fi
    } >"$work/$game.conf"
    summary="$("$build/hosts/arena/rawframe-arena" --config "$work/$game.conf" 2>&1 | grep '"code":"tick_summary"' | tail -1 || true)"
    if [ -z "$summary" ]; then
        printf 'bench %s: no tick summary\n' "$game"; failures=$((failures + 1)); continue
    fi
    # ticks, then p50, p95, and p99 in milliseconds.
    read -r ticks p50 p95 p99 < <(python3 -c '
import json, sys
f = json.loads(sys.argv[1])["fields"]
print(f["ticks"], *("%.3f" % (f[k] / 1000) for k in ("p50", "p95", "p99")))' "$summary")
    printf 'bench %s: %s ticks, p50 %s ms, p95 %s ms, p99 %s ms\n' "$game" "$ticks" "$p50" "$p95" "$p99"
    if [ "$mode" = check ]; then
        verdict="$(python3 -c '
import statistics, sys
path, machine, game, p50, p95, p99 = sys.argv[1:7]
p50, p95, p99 = float(p50), float(p95), float(p99)
if p50 > 5 or p95 > 8.33 or p99 > 12.5:
    print("past SPEC-0013 ceilings")
    sys.exit()
rows = [line.rstrip("\n").split("\t") for line in open(path)][1:]
mine = [float(r[7]) for r in rows if r[2] == machine and r[3] == game][-3:]
if mine and p95 > 2 * statistics.median(mine) + 0.5:
    print("p95 past twice the recorded %.3f ms" % statistics.median(mine))' "$results" "$machine" "$game" "$p50" "$p95" "$p99")"
        if [ -n "$verdict" ]; then
            printf 'bench %s: %s\n' "$game" "$verdict"; failures=$((failures + 1))
        fi
    else
        printf '%s\t%s\t%s\t%s\t64\t%s\t%s\t%s\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$commit" "$machine" "$game" \
            "$ticks" "$p50" "$p95" "$p99" >>"$results"
    fi
done
exit $((failures > 0))
