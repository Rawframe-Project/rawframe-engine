#!/usr/bin/env bash
# SPEC-0013's soak (D214): the dedicated server serves the crowd over QUIC
# under the canonical profile (bench/canonical_profile.conf) while bots come
# and go in rounds, and its resident memory is sampled every five seconds.
# After the warm-up, growth past max(8 MiB, 2% of the warm baseline) fails,
# as does a server that degraded or did not stop clean. Linux only: memory is
# read from /proc. Too long for the check; run it by hand or on a schedule.
#
#   tools/soak.sh [minutes] [warm-up minutes] [round seconds] [settings]
#
# Defaults: 30 minutes, 15 of them warm-up (SPEC-0013's measurement
# protocol), and rounds of 60 seconds. Four bots processes of 16 bots each
# play a round and are started again, staggered, so connections join and
# leave all the time. A settings file, when given, is appended to the
# server's configuration. Needs out/clang-shipping built.
set -euo pipefail
cd "$(dirname "$0")/.."

minutes="${1:-30}"
warmup="${2:-15}"
round="${3:-60}"
settings="${4:-/dev/null}"
build=out/clang-shipping
processes=4
count=16
if [ "$warmup" -ge "$minutes" ]; then
    echo "soak: the warm-up must be shorter than the soak" >&2
    exit 2
fi

work="$(mktemp -d)"
loops=()
server_pid=""
cleanup() {
    kill "${loops[@]}" 2>/dev/null || true
    pkill -P $$ 2>/dev/null || true
    [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

port="$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')"
cat >"$work/server.conf" <<CONF
host.iteration_rate = 120
world.tick_rate = 60
kest.game = $PWD/games/crowd/crowd.game
network.quic.self_signed = true
network.quic.fingerprint_file = $work/fingerprint
replication.endpoint = 127.0.0.1:$port
CONF
cat bench/canonical_profile.conf "$settings" >>"$work/server.conf"
cat >"$work/bots.conf" <<CONF
host.maximum_iterations = $((round * 120))
host.iteration_rate = 120
kest.plan_only = true
kest.game = $PWD/games/crowd/crowd.game
network.quic.pin_file = $work/fingerprint
bots.count = $count
bots.endpoint = 127.0.0.1:$port
CONF

"$build/hosts/dedicated_server/rawframe-server" --config "$work/server.conf" >"$work/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 100); do
    grep -q '"code":"listening"' "$work/server.log" 2>/dev/null && break
    sleep 0.05
done

start="$(date +%s)"
end=$((start + minutes * 60))
# Each bots process plays a round and starts again until the end, the
# processes staggered across a round.
for index in $(seq "$processes"); do
    (
        sleep $(((index - 1) * round / processes))
        rounds=0
        while [ "$(date +%s)" -lt "$end" ]; do
            "$build/hosts/bots/rawframe-bots" --config "$work/bots.conf" >>"$work/bots-$index.log" 2>&1 || true
            rounds=$((rounds + 1))
        done
        echo "$rounds" >"$work/rounds-$index"
    ) &
    loops+=($!)
done

# Resident memory, every five seconds.
: >"$work/samples"
while [ "$(date +%s)" -lt "$end" ]; do
    pages="$(awk '{print $2}' "/proc/$server_pid/statm" 2>/dev/null || echo 0)"
    printf '%s\t%s\n' "$(($(date +%s) - start))" "$((pages * $(getconf PAGESIZE)))" >>"$work/samples"
    sleep 5
done
for pid in "${loops[@]}"; do
    wait "$pid" || true
done
loops=()
kill -TERM "$server_pid"
wait "$server_pid" || true
server_pid=""

python3 - "$work" "$warmup" <<'PY'
import glob, json, statistics, sys

work, warmup = sys.argv[1], int(sys.argv[2]) * 60
samples = [tuple(int(v) for v in line.split()) for line in open(work + "/samples") if line.strip()]
warm = [rss for t, rss in samples if t >= warmup]
if len(warm) < 24:
    print("soak: too few samples after the warm-up")
    sys.exit(1)
# A minute of samples at each end, by median, so one sample is not the
# verdict.
baseline = statistics.median(warm[:12])
final = statistics.median(warm[-12:])
growth = final - baseline
allowed = max(8 * 2**20, 0.02 * baseline)
logs = [json.loads(l) for l in open(work + "/server.log") if l.startswith("{")]
fields = lambda code: [l["fields"] for l in logs if l.get("code") == code]
stopped = fields("stopped")
degraded = [f for f in fields("health") if f["health"] != "healthy"]
tick = fields("tick_summary")
admitted = sum(f["admitted"] for path in glob.glob(work + "/bots-*.log")
               for f in (json.loads(l)["fields"] for l in open(path) if '"code":"bots_summary"' in l))
rounds = sum(int(open(p).read()) for p in glob.glob(work + "/rounds-*"))
print("soak: %d samples, %d bots rounds, %d bots admitted; warm baseline %.1f MiB, final %.1f MiB, growth %+.2f MiB "
      "(allowed %.2f MiB)" % (len(samples), rounds, admitted, baseline / 2**20, final / 2**20, growth / 2**20,
                              allowed / 2**20))
# Resident memory minute by minute, for the shape of any growth.
minutes = {}
for t, rss in samples:
    minutes.setdefault(t // 60, []).append(rss)
print("soak: MiB by minute: " + " ".join("%.0f" % (statistics.median(v) / 2**20) for _, v in sorted(minutes.items())))
if tick:
    t = tick[-1]
    print("soak: tick p50 %.3f ms, p95 %.3f ms, p99 %.3f ms, %d ticks" % (
        t["p50"] / 1000, t["p95"] / 1000, t["p99"] / 1000, t["ticks"]))
failed = []
if growth > allowed:
    failed.append("memory grew past the tripwire")
if degraded:
    failed.append("degraded %d times" % len(degraded))
if not stopped or stopped[0]["exit"] != "clean_stop":
    failed.append("the server did not stop clean")
if admitted == 0:
    failed.append("no bot was admitted")
print("soak: " + ("; ".join(failed) if failed else "passed"))
sys.exit(1 if failed else 0)
PY
