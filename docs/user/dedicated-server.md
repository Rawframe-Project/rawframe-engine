# Running a server

`rawframe-server` runs a game's authoritative World and serves players over QUIC, and browsers over WebTransport. It links no rendering, window, UI or audio code, so it runs on a machine without a GPU or a display. An exported folder's `server/` holds one with its configuration.

```sh
rawframe-server --config server.conf
```

## Configuration

The configuration is `key = value` lines; `#` starts a comment, and relative paths are under the file's own directory. Nothing is read from the environment. A malformed line, a value out of range or a repeated key stops the server before it starts, saying why.

| Setting | Meaning |
|---|---|
| `kest.game` | The game description, in development. |
| `content.root`, `content.manifest` | Cooked content to read the game from. |
| `content.library` | An installed library, whose active Composition the server plays. |
| `world.tick_rate` | Ticks a second. |
| `world.maximum_ticks_per_iteration`, `world.degraded_ms`, `world.debt_limit_ms`, `world.overload_ms` | How far behind the World may fall: it catches up at most so many ticks an iteration, is degraded (admission closed) so far behind, and overloaded past the debt limit or after degrading so long. |
| `replication.endpoint` | Where players connect; replication is off without it. |
| `replication.maximum_connections`, `replication.state_rate`, `replication.egress_bytes_per_second` | How many players, how often each is sent state, and how many bytes a second. |
| `replication.mapping_bytes_per_publish` | How many bytes of entity declarations and retirements a player is sent each time it is sent state, 64 bytes to 128 KiB; 16 KiB by default. A player joining in view of more entities is told of them over several publishes, and of more removed at once likewise: until told, it still shows them where they were last sent. |
| `network.quic.self_signed`, `network.quic.fingerprint_file`, `network.quic.renew_ms` | A certificate the server makes as it starts, the file it writes its fingerprint to for clients to pin, and how soon it renews. |
| `network.quic.certificate_file`, `network.quic.private_key_file` | A certificate for the host's name instead. |
| `network.quic.webtransport` | Listen for browsers too. |
| `network.quic.idle_timeout_ms`, `network.quic.keep_alive_ms`, `network.quic.processors` | Connection timing, and the processors QUIC may use. |
| `kest.fuel_per_system`, `kest.heap_bytes` | Each system's steps a tick, and the program's heap. |
| `kest.reload_every` | Reload the program and scenes when their sources change. |
| `save.directory`, `save.slot`, `save.every_seconds`, `save.namespace` | Where and how often the game's save is kept. |
| `tooling.endpoint`, `tooling.token_file`, `tooling.grants` | A tooling endpoint for inspecting and debugging the running game. |
| `host.iteration_rate`, `host.maximum_iterations` | How often the Host iterates, and a bound for test runs. |
| `host.drain_ms`, `host.shutdown_budget_ms` | How long a stop may wait for players, and for the server to stop. |
| `diagnostics.minimum_severity` | The least severe record logged. |

## Logs and stopping

The server logs newline-delimited JSON on standard output: one record a line with a code and fields. At its stop it logs summaries, among them `tick_summary` with its tick count and tick durations (p50, p95, p99 and maximum).

SIGINT or SIGTERM (a console's control events on Windows) starts an orderly stop: the server stops admitting players, drains the connections it serves up to `host.drain_ms`, then stops. Its exit code says why it ended in a portable category: a clean stop, invoked wrongly, a bad setting, content that did not verify, and the others in `rawframe/host/host.h`.

## Inspecting a running server

With a tooling endpoint configured, `rawframe-author connect <endpoint> <pin file> <token file>` reads the World as it runs: its status, its entities a page at a time, and any entity's components by field name. [Debugging Kest](debugging-kest.md) attaches a debugger to the same endpoint.
