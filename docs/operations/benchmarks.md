# Benchmarks

The engine promises a server tick budget and a web download budget. The nightly tier checks both; these scripts measure them by hand too.

## The tick budget

`tools/bench.sh` plays each sample game (`arena`, `runners`, `plaza`, `crowd`) with 64 bots for six seconds in one arena process built with `clang-shipping`, and reads the server World's tick durations from its summary. The crowd is the canonical workload: 10,000 entities, 2,048 of them walking.

```sh
tools/bench.sh          # measure, and add a row per game to out/bench/results.tsv
tools/bench.sh check    # measure and judge, recording nothing
```

A check fails on a tick past the ceilings (p50 5 ms, p95 8.33 ms, p99 12.5 ms), on Kest time per tick past a p95 of 4 ms, or on a p95 past twice this machine's recorded one (the median of its last three rows) and half a millisecond. The recorded rows are the machine's own history under `out/`; they are never committed.

The crowd is then served over QUIC to 64 bots in four processes of their own, under the canonical profile in `bench/canonical_profile.conf` (at most two catch-up ticks an iteration, degraded 100 ms behind, MsQuic on two processors, state sent 20 times a second). The server is held to its memory (ready within 128 MiB, peak within 512 MiB), its start and shutdown deadlines, an average under 1.5 CPUs while active, and never degrading. A checkpoint of the crowd must be captured within 10 s and restored within 15 s, its artifact within 128 MiB, with the World held for the capture's safe point within 5 ms. A Kest system that never ends is stopped by its fuel within the 8 ms the tick may lose to it.

## The soak

`tools/soak.sh [minutes] [warm-up minutes] [round seconds] [settings]` serves the crowd for 30 minutes by default while bots join and leave in rounds, sampling the server's resident memory every five seconds. After the warm-up, growth past 8 MiB or 2% of the warm baseline, whichever is larger, fails. It is too long for any tier; run it by hand, on Linux, with `clang-shipping` built.

## The web download budget

`tools/web_budget.sh` measures the shipping web client: the module stripped, then compressed as a server would send it (gzip and brotli), and, under V8, the time to compile, instantiate and start a game, and a server World's ticks with sixteen bots, all in WebAssembly.

```sh
tools/web_budget.sh         # measure, and add a row to out/bench/web.tsv
tools/web_budget.sh check   # measure and judge, recording nothing
```

A check fails on a brotli module past 4 MiB, or past 110% of this machine's recorded one and 64 KiB, or on compiling, instantiating and starting past 250 ms. It needs `out/wasm-shipping` built.
