# Changelog

What a user of the engine has to know between versions. Newest first.

## Unreleased

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
