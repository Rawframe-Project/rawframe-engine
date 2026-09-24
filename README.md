# Rawframe Engine

A performance-oriented, network-first engine for 2D and 3D games and real-time simulations. The core is C++23. Gameplay and tooling are written in [Kest](https://github.com/Rawframe-Project/kest), a deterministic, statically typed game language. Physics comes from [Maul2D](https://github.com/Rawframe-Project/maul2d) and [Maul3D](https://github.com/Rawframe-Project/maul3d).

The engine is at the start of its first milestone, a headless, networked, Kest-scripted World with a dedicated server. There are no releases yet, and every surface is unstable.

## Building

Requirements: CMake 3.28, Ninja, and GCC 13 or Clang 19 or newer (MSVC 17.10 on Windows).

```sh
cmake --preset clang-development
cmake --build out/clang-development
ctest --test-dir out/clang-development
```

Presets exist for `gcc` and `clang` in each of the three configurations (`debug`, `development`, `shipping`), plus `clang-sanitize` (AddressSanitizer and UndefinedBehaviorSanitizer).

## Checking

`tools/check.sh fast` runs the repository rules, the formatter, one build, and the tests. `tools/check.sh` runs everything: both compilers, every configuration, and the sanitizers. Install the pre-push hook with `git config core.hooksPath tools/hooks`.

## Layout

| Path | Holds |
|---|---|
| `modules/<name>/` | One engine module: `include/rawframe/<name>/`, `src/`, `tests/`. |
| `tools/modules.txt` | The module graph: which module may depend on which. The build and the check enforce it. |
| `tests/harness/` | The test harness. |
| `cmake/` | Build policy: language, warnings, determinism, configurations. |

## License

Rawframe is source-available under the Rawframe Source-Available License; see [LICENSE.md](LICENSE.md). External code contributions are not open yet. Report security issues through GitHub's private vulnerability reporting.
