#!/usr/bin/env bash
# The web client against its download and start budget (ADR-0084, D175):
# the shipping client module stripped, then compressed as a server would
# send it (gzip -9 and brotli -q 11), and what tools/web_load.mjs measures
# of it under V8: compiling, instantiating, and starting a game, and a
# server World's ticks with sixteen bots, all in WebAssembly.
#
#   tools/web_budget.sh         measure, and add a dated, commit-stamped row
#                               to bench/web.tsv
#   tools/web_budget.sh check   measure and record nothing; fail on a brotli
#                               module past 4 MiB, or past 110% of this
#                               machine's recorded one (the median of its last
#                               three rows) and 64 KiB; or on compiling,
#                               instantiating, and starting past 250 ms
#
# Needs out/wasm-shipping built; the full check builds it first.
set -euo pipefail
cd "$(dirname "$0")/.."

mode="${1:-record}"
module=out/wasm-shipping/hosts/web_client/rawframe-web-client.wasm
results=bench/web.tsv
machine="$(hostname)"
commit="$(git rev-parse --short=12 HEAD)"
git diff --quiet HEAD || commit="$commit+"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

llvm-strip-20 -o "$work/client.wasm" "$module"
stripped=$(stat -c %s "$work/client.wasm")
gzipped=$(gzip -9 -c "$work/client.wasm" | wc -c)
brotlied=$(brotli -q 11 -c "$work/client.wasm" | wc -c)
load="$(node --no-warnings tools/web_load.mjs "$work/client.wasm" "$PWD")"
read -r compile instantiate start p50 p99 < <(python3 -c '
import json, sys
f = json.loads(sys.argv[1])
print(*("%.3f" % f[k] for k in ("compile", "instantiate", "start", "tickP50", "tickP99")))' "$load")
printf 'web: %s bytes stripped, %s gzip, %s brotli; compile %s ms, instantiate %s ms, start %s ms; server tick p50 %s ms, p99 %s ms\n' \
    "$stripped" "$gzipped" "$brotlied" "$compile" "$instantiate" "$start" "$p50" "$p99"

mkdir -p bench
[ -f "$results" ] || printf 'date\tcommit\tmachine\tstripped\tgzip\tbrotli\tcompile_ms\tinstantiate_ms\tstart_ms\ttick_p50_ms\ttick_p99_ms\n' >"$results"
if [ "$mode" = check ]; then
    verdict="$(python3 -c '
import statistics, sys
path, machine, brotli, compile, instantiate, start = sys.argv[1:7]
brotli = int(brotli)
if brotli > 4 * 1024 * 1024:
    print("the module is past 4 MiB compressed")
    sys.exit()
if float(compile) + float(instantiate) + float(start) > 250:
    print("compiling, instantiating, and starting took past 250 ms")
    sys.exit()
rows = [line.rstrip("\n").split("\t") for line in open(path)][1:]
mine = [int(r[5]) for r in rows if r[2] == machine][-3:]
if mine and brotli > 1.1 * statistics.median(mine) + 64 * 1024:
    print("the module grew past 110%% of the recorded %d bytes" % statistics.median(mine))' \
        "$results" "$machine" "$brotlied" "$compile" "$instantiate" "$start")"
    if [ -n "$verdict" ]; then
        printf 'web: %s\n' "$verdict"
        exit 1
    fi
else
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$commit" "$machine" \
        "$stripped" "$gzipped" "$brotlied" "$compile" "$instantiate" "$start" "$p50" "$p99" >>"$results"
fi
