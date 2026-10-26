# Rawframe Engine

Rawframe is a network-first engine for 2D and 3D games and real-time simulations. Its core is C++23; games are written in Kest and text documents, driven by headless tools, with Studio as an optional editor over the same operations. [docs/internals/overview.md](docs/internals/overview.md) explains how the parts fit.

It never compromises on:

1. **A game is made without Studio.** Every authoring change is a session operation an agent or a script can run; Studio only calls them. A Studio feature lands as a headless operation first. There is no visual scripting or other editor-only form.
2. **The server decides.** Clients send input and commands, never state. Local play still runs a server, replication and admission.
3. **Determinism.** Simulation gives the same bits on every platform and compiler; prediction, checkpoints and bots depend on it.
4. **One owner per concept.** One module, one name, one public path. No globals, singletons, service locators or catch-all modules.
5. **Untrusted input.** Files, network data, scripts and mods are validated and refused with errors in every build, bounded, and fuzzed.

## Commands

```sh
cmake --preset clang-development && cmake --build out/clang-development   # the daily tree
tools/check.sh changed        # before every push: rules, docs, format, reached tests (< 2 min)
tools/check.sh pr             # what the pull request check runs (< 10 min)
tools/check.sh full           # what main runs: every tree, the web, browsers, integration tests (< 20 min)
ctest --test-dir out/clang-development -R '^world$'                 # one module's tests
out/clang-development/modules/world/rawframe_world_tests Barrier    # the cases whose name contains Barrier
tools/gen_shaders.py          # rebuild the committed shader containers (needs the shader toolchain)
```

Run `pr`, `full` and any full build through the workspace's heavy-job wrapper, at eight jobs or fewer: `/home/rawframe-project/tools/heavy.sh tools/check.sh pr`. `git config core.hooksPath tools/hooks` makes the pre-push hook run `changed`. Presets are in `CMakePresets.json`; [docs/operations/development.md](docs/operations/development.md) lists them and the toolchain.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path. Tests start `rawframe-server`, `rawframe-client`, Xvfb and Monado, and other sessions and the CI runner on this machine run the same programs. Kill only a PID you started.
- **Starving the machine.** The machine is shared with other sessions and the self-hosted CI runner. Builds of several trees and the `pr`, `full` and `nightly` tiers go through `heavy.sh`; never run them bare or with more than eight jobs.
- **Editing generated files.** Shader containers (`modules/*/src/generated/*.mrsc`, `modules/render_scene_gpu/tests/generated/`), the CLDR tables (`modules/localization/src/generated/`), the material functions (`modules/material/library/`), Studio's icons (`modules/studio/icons/icons.rgba`, `modules/studio/src/icons.h`) and the packing golden vectors (`modules/build/tests/golden/`) are written by tools. Change the generator or its input and regenerate; [docs/operations/development.md](docs/operations/development.md#generated-files) names each command.
- **Editing vendored code.** `third_party/` is upstream's. Move a pin with its update script ([docs/operations/dependencies.md](docs/operations/dependencies.md)), never by hand.
- **Running fork code on the runner.** The self-hosted runner runs this repository's branches only. Never add a job on it that a fork's pull request can reach, and never check out a pull request's code under `pull_request_target`.
- **Trusting a skip.** Without lavapipe or the shader toolchain, pixel and material tests skip. `tools/check.sh` makes them fail instead where the tools are installed; a rendering change is not checked until they ran.
- **Deleting shared caches.** `~/.cache/rawframe` holds the MsQuic and OpenSSL builds and the pinned Slang compiler; `out/<preset>` trees take many minutes to rebuild. Leave them.

## Invariants

- **The module graph.** `tools/modules.txt` lists each module and host with what it may depend on; the build refuses any other dependency, and `tools/check_repo.py` refuses an `#include` across a boundary the graph does not allow. A new dependency needs a line there, and the graph stays acyclic.
- **Closures.** The dedicated server reaches no rendering, window, UI, audio, localization, authoring, client input, loopback or browser transport, or import, cook and build tooling. No runtime reaches `authoring`, `authoring_session` or `studio`. Nothing depends on `launcher/`, and only the launcher includes OpenSSL's TLS. The web build is the modules in `tools/web_modules.txt`. `check_repo.py` enforces all four.
- **Vendored libraries stay behind their module.** No public header includes one; Maul RHI only in the rendering modules, Maul Window in `window`, Maul UI in `ui`, Maul Nav in `navigation`, Maul Audio in `audio`.
- **No exceptions, no RTTI.** Failures are `result::Result<T>` or `result::Status`, `[[nodiscard]]`, built with `fail(...)` and passed on with `RAWFRAME_TRY` and `RAWFRAME_TRY_ASSIGN`. Never call `.value()` on a Result; the check refuses it.
- **Assertions are for invariants.** `RAWFRAME_ASSERT` states what must hold inside the engine, compiled out of shipping, so its condition has no needed effect. `RAWFRAME_CHECK` and `RAWFRAME_PANIC` stay in every build. Never assert on input from outside the process; validate it and return an error.
- **No hidden state.** No globals, singletons or static registration in production code; modules register participants through registrars a host calls. What a part needs is handed to it.
- **Determinism.** Nothing from wall time, thread timing, addresses or a hash container's iteration order reaches simulation state. Simulation randomness comes only from declared World streams. Floating-point contraction stays off on every target.
- **Kest stages, never mutates.** Scripts change the World through command buffers applied at barriers. Every new door says whether untrusted code may call it; a door that writes or reaches outside the World is not safe for untrusted code.
- **Bytes, not memory.** Anything that leaves a process is written field by field in a declared order, never as a struct. Formats accept one canonical spelling; a change to the wire, a checkpoint, a save, a Build or a cooked format is a new generation with its golden values regenerated in the same pull request.
- **Bounds have one home.** The execution layer's capacities live in `rawframe/execution/bounds.h` alone; parsers and protocols bound everything they hold.
- **Shaders.** A fragment shader reads every input it declares (Direct3D 12 links stages by place). No shipped program compiles a shader; containers are compiled offline and committed.
- **Studio surfaces.** Every surface in `modules/studio/src/surfaces.h` names the headless path it stands on, within the table's budgets; the check refuses one without.
- **Size.** Handwritten source fails the check at 1,500 lines and is reported from 1,000; split along an ownership seam.
- **The version** is written once, in the root `CMakeLists.txt`, and only release-please changes it.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- A module's tests are `RAWFRAME_TEST(name)` functions in its `tests/`, checking with `RAWFRAME_EXPECT`, which reports and continues. Add a case to the module's existing test file; cross-module scenarios go in `tests/scenarios/`. The fatal path is tested in a child process with the harness's runner.
- Test through public headers. Do not test private state or call order.
- Window, browser, headset and played-game tests carry one of the integration labels (`window`, `headset`, `alone`, `gesture`, `studio`, `heavy`). Keep them to a few end-to-end scenarios; never add one for a cosmetic change.
- Studio's behavior is tested through the authoring session (`hosts/author/tests`, `modules/authoring_session/tests`), not by driving its window.
- A new reader of untrusted input gets a fuzz target (`rawframe_module_fuzz` in the module's `CMakeLists.txt`) with seed inputs, and a fuzz finding becomes a test in its module's suite.
- Pixel tests draw on lavapipe and compare exact or bounded values; a device test that finds no adapter fails where `RAWFRAME_REQUIRE_GPU` is set.
- Unit tests never sleep, read the wall clock or retry; deterministic time is `execution::ManualClock`, and the loopback transport takes seeded latency and loss.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change to a game description line, a configuration key, a command line or a document format updates the page of `docs/user/` that names it. A change to how a part works across modules updates its page of `docs/internals/`. Source comments say why, never cite a record.

## Pull requests

Titles are conventional commits. Scopes: `base`, `execution`, `composition`, `host`, `world`, `kest`, `network`, `replication`, `physics`, `navigation`, `content`, `cook`, `packaging`, `launcher`, `render`, `material`, `ui`, `input`, `window`, `audio`, `animation`, `localization`, `scene`, `authoring`, `studio`, `tooling`, `xr`, `web`, `android`, `ios`, `games`, `deps`, `build`, `check`. Example: `fix(replication): keep an entity mapped while its district streams in`.

A change of results (a golden value, a wire or file format, a cooked output) says so in its body and names who sees it.
