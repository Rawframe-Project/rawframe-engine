# Changelog

What a user of the engine has to know between versions. Newest first.

## Unreleased

- Deterministic randomness: PCG32 XSH-RR and SplitMix64 checked against their published vectors, identity-derived streams, indexed draws, Lemire bounded integers, 53-bit doubles, 24-bit floats, Fisher-Yates shuffle, and weighted choice, all one pinned identity. A World has a root seed and owns stream state; systems declare the streams they draw from.
- World schedule and time: the eight closed phases, `System` and `SystemDeclaration`, `Schedule::compile` (refuses duplicates, unknown or cross-phase edges, cycles, and access outside the registry) with deterministic order, `runTick` with the commit barrier and per-system failure isolation, `TickIndex`, exact rational `TickRate`, and `TickPacer` for bounded catch-up with measured debt.
- World command buffers: `CommandBuffer` records create, destroy, insert, and remove within fixed limits, and `World::apply` applies them in order at the barrier, all or nothing on capacity, skipping and counting commands on entities already gone.
- World queries: `Query<Read<T>, Write<U>, With<V>, Without<W>>` resolves against the registry, caches matching archetypes, iterates in history order, and reports the components it reads and writes.
- `rawframe.world`: 8-byte `EntityHandle` with generations and slot retirement, archetype storage in aligned columns, and direct structural operations (`create`, `destroy`, `insert`, `remove`, `get`) that refuse while the structure is locked.
- `rawframe.schema`: `ComponentTypeId` from canonical UUID text at compile time, the `Component` concept, component descriptors, and `RegistryBuilder`/`SchemaRegistry` with dense runtime IDs in stable-ID order and typed `ComponentKey<T>`.
- `RateLimitedSink` limits log records per identity per window and reports what it held back as a `suppressed` field on the next record it passes.
- `rawframe.composition`: participant declarations, `ParticipantRegistrar` and registrar entries listed by each host, `compose`, which runs registrars in module-ID order and reports every SPEC-0005 validation problem before anything is constructed, and `Composition`, which constructs, starts, quiesces, stops, and destroys participants in plan order with exact reverse rollback. Capabilities are typed without RTTI.
- `Executor::retireOwner` withdraws a quota so the owner can be admitted again.
- `rawframe.execution`: monotonic time types and clocks, the cancellation tree with deadlines and failure policy, `TaskOutcome` with explicit cancellation mapping, bounded CPU and blocking-I/O executors with owner quotas and background promotion, scoped jobs that run inline when the queue is full, and async operations owned and joined by their scope. SPEC-0048 values live in `bounds.h` only, and the repository check enforces it.
- A `clang-thread` preset runs every test under ThreadSanitizer in the full check. `RAWFRAME_SANITIZE` is now `address`, `thread`, or empty.
- `rawframe.diagnostics`: compile-time checked event identities and field keys, the record, a router that owns time and correlation and hands out `Emitter`s, and `NdjsonSink`, which writes SPEC-0047 records with truncation, secret removal, clearance refusal, and a bounded buffer drained by its owner.
- Omitting a defaulted member from an initializer no longer warns (`-Wno-missing-field-initializers`).
- `rawframe.result`: `Error` (16 bytes, bounded description, frames, context, and cause chain), `Result<T>` and `Status` as `std::expected` aliases, `fail`, `RAWFRAME_TRY`, `RAWFRAME_TRY_ASSIGN`, and allocation-free `formatError`.
- Clang 19 is the oldest supported Clang: Clang 18 cannot use libstdc++'s `std::expected`.
- The repository starts: build presets, the check, and `rawframe.base` (assertions, the fatal path, `Bits128`, byte vocabulary, platform detection for x86-64, arm64, and wasm32 under GCC, Clang, and MSVC).
