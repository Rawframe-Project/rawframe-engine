# Changelog

What a user of the engine has to know between versions. Newest first.

## Unreleased

- `rawframe.result`: `Error` (16 bytes, bounded description, frames, context, and cause chain), `Result<T>` and `Status` as `std::expected` aliases, `fail`, `RAWFRAME_TRY`, `RAWFRAME_TRY_ASSIGN`, and allocation-free `formatError`.
- Clang 19 is the oldest supported Clang: Clang 18 cannot use libstdc++'s `std::expected`.
- The repository starts: build presets, the check, and `rawframe.base` (assertions, the fatal path, `Bits128`, byte vocabulary, platform detection for x86-64, arm64, and wasm32 under GCC, Clang, and MSVC).
