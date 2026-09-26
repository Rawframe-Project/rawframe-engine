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
#                           12.5 ms), on Kest time per tick past its p95 of
#                           4 ms, or on a p95 past twice this machine's
#                           recorded one (the median of its last three rows)
#                           and half a millisecond
#
# Needs out/clang-shipping built; the full check builds it first. Last, the
# crowd is served over QUIC to bots in processes of their own, and the
# server's memory and tick are held to SPEC-0013 there too, and a checkpoint
# of the crowd is captured and restored within SPEC-0013's deadlines.
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
[ -f "$results" ] || printf 'date\tcommit\tmachine\tgame\tbots\tticks\tp50_ms\tp95_ms\tp99_ms\tkest_p95_ms\n' >"$results"

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
    "$build/hosts/arena/rawframe-arena" --config "$work/$game.conf" >"$work/$game.log" 2>&1 || true
    summary="$(grep '"code":"tick_summary"' "$work/$game.log" | tail -1 || true)"
    kest="$(grep '"code":"kest_summary"' "$work/$game.log" | tail -1 || true)"
    if [ -z "$summary" ]; then
        printf 'bench %s: no tick summary\n' "$game"; failures=$((failures + 1)); continue
    fi
    # ticks, then p50, p95, and p99 in milliseconds.
    read -r ticks p50 p95 p99 < <(python3 -c '
import json, sys
f = json.loads(sys.argv[1])["fields"]
print(f["ticks"], *("%.3f" % (f[k] / 1000) for k in ("p50", "p95", "p99")))' "$summary")
    # Kest time per tick, SPEC-0013's aggregate script time (D210).
    kest95="$(python3 -c '
import json, sys
print("%.3f" % (json.loads(sys.argv[1])["fields"]["p95"] / 1000) if sys.argv[1] else "none")' "$kest")"
    printf 'bench %s: %s ticks, p50 %s ms, p95 %s ms, p99 %s ms; Kest p95 %s ms\n' "$game" "$ticks" "$p50" "$p95" "$p99" \
        "$kest95"
    if [ "$mode" = check ]; then
        verdict="$(python3 -c '
import statistics, sys
path, machine, game, p50, p95, p99, kest95 = sys.argv[1:8]
p50, p95, p99 = float(p50), float(p95), float(p99)
if p50 > 5 or p95 > 8.33 or p99 > 12.5:
    print("past SPEC-0013 ceilings")
    sys.exit()
if kest95 == "none" or float(kest95) > 4:
    print("Kest time per tick past SPEC-0013 p95 4 ms, or not reported")
    sys.exit()
rows = [line.rstrip("\n").split("\t") for line in open(path)][1:]
mine = [float(r[7]) for r in rows if r[2] == machine and r[3] == game][-3:]
if mine and p95 > 2 * statistics.median(mine) + 0.5:
    print("p95 past twice the recorded %.3f ms" % statistics.median(mine))' "$results" "$machine" "$game" "$p50" "$p95" "$p99" "$kest95")"
        if [ -n "$verdict" ]; then
            printf 'bench %s: %s\n' "$game" "$verdict"; failures=$((failures + 1))
        fi
    else
        printf '%s\t%s\t%s\t%s\t64\t%s\t%s\t%s\t%s\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$commit" "$machine" \
            "$game" "$ticks" "$p50" "$p95" "$p99" "$kest95" >>"$results"
    fi
done

# The crowd as SPEC-0013 deploys it: the dedicated server over QUIC, and 64
# bots in four processes of their own (D211), under SPEC-0013's overload
# thresholds (bench/canonical_profile.conf, D212). What is held here is the
# server's memory (ready at most 128 MiB, peak at most 512 MiB), that it was
# never degraded (100 ms behind), its start and shutdown within their hard
# values (5 s, 8 s), its configured normal shutdown within 8 s too (D230),
# and its average processor use while active within 1.5 CPUs (D213). Its
# tick is reported, not held: four bots processes with their own MsQuic
# threads share this machine, and their load shows in its tail
# (D215); the arena's runs above hold the tick.
play="$(hosts/bots/tests/play.sh "$build/hosts/dedicated_server/rawframe-server" "$build/hosts/bots/rawframe-bots" \
    16 4 1440 games/crowd/crowd.game "$PWD/bench/canonical_profile.conf" 2>&1 || true)"
verdict="$(python3 -c '
import json, sys
logs = [json.loads(line) for line in sys.argv[1].splitlines() if line.startswith("{")]
# The server logs first, to its stop.
logs = logs[:next((i + 1 for i, l in enumerate(logs) if l.get("code") == "stopped"), len(logs))]
degraded = [l for l in logs if l.get("code") == "health" and l["fields"]["health"] != "healthy"]
def first(code):
    found = [l["fields"] for l in logs if l.get("code") == code]
    return found[0] if found else None
started, stopped, tick = first("started"), first("stopped"), first("tick_summary")
if not (started and stopped and tick):
    print("FAIL no server summary")
    sys.exit()
ready, peak = started["residentBytes"] / 2**20, stopped["peakResidentBytes"] / 2**20
active = max(stopped["runMs"] - started["readyMs"], 1)
cpus = (stopped["cpuMs"] - started["cpuMs"]) / active
line = "ready %.1f MiB, peak %.1f MiB; tick p50 %.3f ms, p95 %.3f ms, p99 %.3f ms; " % (
    ready, peak, tick["p50"] / 1000, tick["p95"] / 1000, tick["p99"] / 1000)
line += "ready in %d ms, shutdown %d ms, %.2f CPUs active" % (started["readyMs"], stopped["shutdownMs"], cpus)
# What the participants account for, and what they do not (D216).
summary = first("memory_summary")
if summary:
    line += "; attributed %.1f MiB, files %.1f MiB, unattributed %.1f MiB" % (
        summary["attributedBytes"] / 2**20, summary["fileBackedBytes"] / 2**20, summary["unattributedBytes"] / 2**20)
    # Of the unattributed, as the allocator counts it (D233).
    line += " (heap unclaimed %.1f, allocator free %.1f, outside the heap %.1f)" % (
        summary["heapUnclaimedBytes"] / 2**20, summary["allocatorFreeBytes"] / 2**20,
        summary["outsideHeapBytes"] / 2**20)
if degraded:
    line += "; degraded %d times" % len(degraded)
if ready > 128 or peak > 512 or degraded or stopped["exit"] != "clean_stop" or \
        started["readyMs"] > 5000 or stopped["shutdownMs"] > 8000 or cpus > 1.5 or \
        started["normalShutdownBoundMs"] > 8000:
    line = "FAIL past SPEC-0013: " + line
print(line)' "$play")"
printf 'bench crowd over QUIC: %s\n' "$verdict"
if [ "$mode" = check ] && [[ "$verdict" == FAIL* ]]; then
    failures=$((failures + 1))
fi
# A checkpoint of the crowd captured and restored (D215): capture within
# 10 s, restore within 15 s, the artifact within 128 MiB, and the World held
# for the capture's safe point within 5 ms (SPEC-0013's snapshot ceilings;
# D227).
common="host.iteration_rate = 1000
world.tick_rate = 1000
kest.game = $PWD/games/crowd/crowd.game"
printf '%s\nhost.maximum_iterations = 200\ncheckpoint.capture_ticks = 100\ncheckpoint.capture_prefix = %s/a-\n' \
    "$common" "$work" >"$work/capture.conf"
printf '%s\nhost.maximum_iterations = 50\ncheckpoint.restore = %s/a-100.rfsn\n' "$common" "$work" >"$work/restore.conf"
logs="$("$build/hosts/dedicated_server/rawframe-server" --config "$work/capture.conf" 2>&1 || true)
$("$build/hosts/dedicated_server/rawframe-server" --config "$work/restore.conf" 2>&1 || true)"
verdict="$(python3 -c '
import json, sys
logs = [json.loads(line) for line in sys.argv[1].splitlines() if line.startswith("{")]
captured = [l["fields"] for l in logs if l.get("code") == "checkpoint_captured"]
restored = [l["fields"] for l in logs if l.get("code") == "checkpoint_restored"]
if not captured or not restored:
    print("FAIL no capture or no restore")
    sys.exit()
c, r = captured[0], restored[0]
line = "%d entities, %.1f KiB, World held %.2f ms, captured in %d ms, restored in %d ms" % (
    r["entities"], c["bytes"] / 1024, c["pauseUs"] / 1000, c["captureMs"], r["restoreMs"])
if c["captureMs"] > 10000 or r["restoreMs"] > 15000 or c["bytes"] > 128 * 2**20 or c["digest"] != r["digest"] or \
        c["pauseUs"] > 5000:
    line = "FAIL past SPEC-0013: " + line
print(line)' "$logs")"
printf 'bench crowd checkpoint: %s\n' "$verdict"
if [ "$mode" = check ] && [[ "$verdict" == FAIL* ]]; then
    failures=$((failures + 1))
fi
# A Kest system that never ends (D228): its fuel stops it every tick, and
# the tick it spoils stays within SPEC-0013's 8 ms emergency containment.
printf 'world.tick_rate = 60\nhost.iteration_rate = 60\nhost.maximum_iterations = 60\nkest.game = %s\n' \
    "$PWD/modules/world_kest/tests/game/runaway.game" >"$work/runaway.conf"
logs="$("$build/hosts/dedicated_server/rawframe-server" --config "$work/runaway.conf" 2>&1 || true)"
verdict="$(python3 -c '
import json, sys
logs = [json.loads(line) for line in sys.argv[1].splitlines() if line.startswith("{")]
failed = [l for l in logs if l.get("code") == "system_failed"]
tick = [l["fields"] for l in logs if l.get("code") == "tick_summary"]
if not failed or not tick:
    print("FAIL the runaway system was not stopped, or no tick summary")
    sys.exit()
line = "stopped %d times, tick p50 %.3f ms, p99 %.3f ms" % (len(failed), tick[0]["p50"] / 1000, tick[0]["p99"] / 1000)
if tick[0]["p99"] > 8000:
    line = "FAIL past SPEC-0013: " + line
print(line)' "$logs")"
printf 'bench runaway Kest: %s\n' "$verdict"
if [ "$mode" = check ] && [[ "$verdict" == FAIL* ]]; then
    failures=$((failures + 1))
fi
exit $((failures > 0))
