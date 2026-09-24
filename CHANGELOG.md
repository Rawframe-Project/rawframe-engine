# Changelog

What a user of the engine has to know between versions. Newest first.

## Unreleased

- `rawframe.diagnostics`: compile-time checked event identities and field keys, the record, a router that owns time and correlation and hands out `Emitter`s, and `NdjsonSink`, which writes SPEC-0047 records with truncation, secret removal, clearance refusal, and a bounded buffer drained by its owner.
- Omitting a defaulted member from an initializer no longer warns (`-Wno-missing-field-initializers`).
- `rawframe.result`: `Error` (16 bytes, bounded description, frames, context, and cause chain), `Result<T>` and `Status` as `std::expected` aliases, `fail`, `RAWFRAME_TRY`, `RAWFRAME_TRY_ASSIGN`, and allocation-free `formatError`.
- Clang 19 is the oldest supported Clang: Clang 18 cannot use libstdc++'s `std::expected`.
- The repository starts: build presets, the check, and `rawframe.base` (assertions, the fatal path, `Bits128`, byte vocabulary, platform detection for x86-64, arm64, and wasm32 under GCC, Clang, and MSVC).
