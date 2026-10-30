# Development

How to set up a machine, build the engine, run one test, and regenerate what is generated.

## Contents

- [Toolchain](#toolchain)
- [Building](#building)
- [Running tests](#running-tests)
- [Playing a game from a build](#playing-a-game-from-a-build)
- [Generated files](#generated-files)
- [The shared machine](#the-shared-machine)

## Toolchain

The build needs CMake 3.28, Ninja and ccache, with GCC 14 and Clang 20 (`gcc-14`, `clang-20` and `clang-format-20` by those names, as the presets and the check call them). On Debian or Ubuntu, `apt-get install` each package below under the name given.

| For | Packages |
|---|---|
| The window module (Wayland and X11) | `libwayland-dev`, `wayland-protocols`, `libxkbcommon-dev`, `libxkbcommon-x11-dev`, `libxcb1-dev`, `libxcb-randr0-dev`, `libxcb-xkb-dev`, `libxcb-cursor-dev`, `libxcb-xinput-dev` |
| Maul Audio's devices (opened at run time; only the headers build) | `libpipewire-0.3-dev`, `libpulse-dev`, `libasound2-dev` |
| The web build | `libc++-20-dev-wasm32`, `libclang-rt-20-dev-wasm32`, `wasi-libc`, `lld-20`, Node.js 18 or later |
| The QUIC tests, which play a browser with an independent HTTP/3 implementation | `python3-aioquic` |
| The web download budget | `brotli` |
| Window tests | `xvfb` |
| Pixel tests on the CPU | `mesa-vulkan-drivers` (lavapipe) |

Optional, each turning a skipped test into a run one:

- **Browser tests:** Puppeteer and its browser under `/opt/webtest` (or `RAWFRAME_NODE_MODULES` and `PUPPETEER_CACHE_DIR`).
- **The shader toolchain:** the pinned Slang compiler at `~/.cache/rawframe/slang-2026.19/bin/slangc` (or `RAWFRAME_SLANGC`), DXC under `/opt/dxc/<version>/bin`, and `spirv-cross` and `spirv-val` on the path. With all of them present, a material that needs its own program is cooked through them and a test that finds no toolchain fails rather than skips.

The first configure of each machine builds MsQuic and OpenSSL once into `~/.cache/rawframe` (or `$RAWFRAME_DEPENDENCY_CACHE`), in about twenty seconds; see [dependencies.md](dependencies.md).

## Building

Every build is a preset of `CMakePresets.json`, built in `out/<preset>`:

```sh
cmake --preset clang-development
cmake --build out/clang-development
```

| Preset | What it is |
|---|---|
| `gcc-debug`, `clang-debug` | No optimization, every assertion on. |
| `gcc-development`, `clang-development` | Optimized with debug information and every assertion on: the tree for daily work. |
| `gcc-shipping`, `clang-shipping` | Optimized, internal assertions off; contract checks stay. The tick budget is measured here. |
| `clang-sanitize` | AddressSanitizer and UndefinedBehaviorSanitizer; the only tree that builds the fuzz targets. |
| `clang-thread` | ThreadSanitizer. |
| `wasm-development`, `wasm-shipping` | The web build (wasm32-wasi without threads): the modules `tools/web_modules.txt` lists and the web client. Tests run under Node. |
| `windows-development` | `clang-cl` on Windows, drawing through Direct3D 12. |
| `android-development` | arm64-v8a at API 29, with `RAWFRAME_ANDROID_NDK` naming NDK r29. |
| `ios-simulator` | The iOS simulator, against LLVM 20's libc++ built by `tools/build_libcxx_ios.sh`. |

`RAWFRAME_BUILD_TESTS=OFF` leaves the tests out. Every module is listed in the root `CMakeLists.txt` and in `tools/modules.txt`; nothing is discovered from the directory tree.

## Running tests

Each module's tests are one executable, `rawframe_<module>_tests`, registered with CTest under the module's name. Programs and scenarios are registered under their own names.

```sh
ctest --test-dir out/clang-development -R '^world$'          # one module's tests
out/clang-development/modules/world/rawframe_world_tests --list   # the cases in it
out/clang-development/modules/world/rawframe_world_tests Barrier  # the cases whose name contains Barrier
```

Tests read the source tree's files through `RAWFRAME_TEST_FILES` and start programs from the build tree. Window, browser, headset and played-game tests run under their own X server (`tools/xvfb_run.sh`) and carry the label `integration`; see [checks.md](checks.md).

## Playing a game from a build

`rawframe-export` makes a game into a folder that plays on its own: the game cooked and signed, the dedicated server, the client and the launcher beside it. From a built tree:

```sh
tree=out/clang-development
"$tree/hosts/export/rawframe-export" games/runners /tmp/runners \
  --cook "$tree/hosts/cook/rawframe-cook" --build "$tree/hosts/build/rawframe-build" \
  --server "$tree/hosts/dedicated_server/rawframe-server" \
  --client "$tree/hosts/client/rawframe-client" --play "$tree/launcher/play/rawframe-play"
/tmp/runners/runners
```

The launcher, named after the game's directory, starts the game's server on this machine and then a client pinned to it. [Exporting](../user/exporting.md) covers the web target and signing.

## Generated files

Some files are committed because CI cannot regenerate them. Never edit them by hand; change the generator or its input, run it, and commit both.

| Path | Generator | Command |
|---|---|---|
| `modules/*/src/generated/*.mrsc` | The shader containers, compiled from each module's `shaders/*.slang` for every driver family (SPIR-V, WGSL, MSL, DXIL) | `tools/gen_shaders.py`, with the shader toolchain above |
| `modules/render_scene_gpu/tests/generated/*.mrsc` | The test materials' containers, from `tests/materials/*.slang` | `tools/gen_shaders.py` |
| `modules/localization/src/generated/` | The CLDR tables | `tools/generate_cldr.py` |
| `modules/material/library/` | The material functions every graph may call | `tools/make_functions.py modules/material/library` |
| `modules/studio/icons/icons.rgba`, `modules/studio/src/icons.h` | Studio's icons, drawn by a headless Chrome | `tools/make_studio_icons.py` |
| `modules/build/tests/golden/` | The packing format's golden vectors | the build tests with `RAWFRAME_WRITE_GOLDEN=1` |
| Sounds and pictures of the sample games | Made from nothing but noise and sines, so the games carry no one else's recording | `tools/make_cannon.py`, `make_knock.py`, `make_music.py`, `make_sky.py`, `make_stone.py`, `make_waves.py`, each with its output path |

A build embeds only the shader container its driver reads, so the committed containers are the source of every shipped shader; no shipped program compiles one.

Beside each directory of containers, `containers.stamp` names the Maul RHI release whose tools cooked them, the flags DXC compiled their DXIL with, and each container's SHA-256; `tools/gen_shaders.py` writes it. The check runs `tools/gen_shaders.py --check`, which needs no shader tool and fails when a container is missing or not the bytes its stamp names, when the vendored Maul RHI is another release, when the DXIL was compiled without `-Gis` or with other flags than the vendored `mrhi_dxil.py` passes, when a module's `generated` directory holds a container no source gives, or when a module's CMake files (every `CMakeLists.txt` and `.cmake` under `modules/`) embed one `tools/gen_shaders.py` does not make. Without `-Gis` DXC folds the float math, so D3D12 would compute other than the source and the other drivers, and `tools/gen_shaders.py` refuses to cook, a game's material too, with a Maul RHI that passes no `-Gis`.

## The shared machine

The development server is shared by other sessions, the CI runner and unrelated projects. Run full builds and the `pr`, `full` and `nightly` tiers through the workspace's heavy-job wrapper, at eight jobs or fewer:

```sh
/home/rawframe-project/tools/heavy.sh tools/check.sh pr
```

Stop any server, client or emulator a run started when the run ends, by the PID it was started with.
