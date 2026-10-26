# Debugging

Where to look when a program, a test or a played game misbehaves.

## Logs

Every host program writes its diagnostics as newline-delimited JSON on standard output: one record a line, each with a domain, a code, a severity and typed fields. `diagnostics.minimum_severity` filters them. Records are bounded and redacted before any sink writes them; a full buffer drops the newest record and counts the loss in a record of its own. A program's summaries at its stop (`tick_summary`, `frame_summary`, `kest_summary` and others) say how its run went, and most tests read them.

A launch failure before diagnostics exist (an unknown argument, a configuration that does not parse) goes to standard error as plain text and exits with 64, or 65 for the configuration. Otherwise the exit code is the run's reason in its portable category, and the reason itself is logged once as the run ends.

## Crashes

A host program that crashes writes a bounded crash record to standard error before it ends as it would have: what happened, the code and address, the thread, the program's base address and up to 32 return addresses. Symbolize the addresses against the program's base with `llvm-symbolizer` from the same build. On Android and iOS standard error is the client's log.

Assertions (`RAWFRAME_ASSERT`) are on in the debug, development and sanitizer trees and compiled out of `shipping`; contract checks (`RAWFRAME_CHECK`, `RAWFRAME_PANIC`) stay in every build.

## Sanitizers

`clang-sanitize` runs every test under AddressSanitizer and UndefinedBehaviorSanitizer; `clang-thread` under ThreadSanitizer. Suppressions for other libraries' leaks and races are in `tools/libraries.lsan` and `tools/thread.tsan`, each entry with its reason. A sanitizer's report is always a finding: such a test is never run again to see whether it passes.

## A running game

A dedicated server or client configured with `tooling.endpoint` and `tooling.token_file` serves a tooling endpoint beside the game's own protocol (ALPN `rawframe-tooling-v1`), admitting clients by token for the grants `tooling.grants` names (`inspect`, `view`, `debug` and others). Through it:

- `rawframe-author connect <endpoint> <pin file> <token file>` reads the World: its status, its entities a page at a time, and one entity's components by field name.
- `rawframe-debug` speaks the Debug Adapter Protocol: line and function breakpoints in Kest, stepping, and each frame's locals while the server waits inside its tick. [Debugging Kest](../user/debugging-kest.md) shows how to attach from VS Code.
- Studio's Profiler and Network panels read each system's time and each connection's replication.

The endpoint must be on the loopback address of the same machine for the debugger.

## Prediction divergence

A predicting client checksums its player's confirmed state and the server compares it with its own; a mismatch is logged as `prediction_divergence`. In development builds:

- `replication.divergence_capture` on the server and `bots.divergence_capture` on a client keep the latest checksummed records with what they hashed, so both sides of a divergence can be set side by side.
- `bots.divergence_drill` flips one bit of every state a client hashes, so the server must find each record diverged: a check that divergence detection still works.

Neither exists in a shipping build.

## Window tests

Window tests run under an X server of their own (`tools/xvfb_run.sh`), so tests started at the same moment never share a display. To watch one, run its command from `ctest -N -V -R <name>` without the wrapper. A played-game test passes on records its programs log; its full output is in `out/<preset>/Testing/Temporary/LastTest.log`.
