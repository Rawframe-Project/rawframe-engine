# Checks

What verifies a change, locally and in CI, and the time each may take.

## Contents

- [Locally](#locally)
- [In CI](#in-ci)
- [Tests](#tests)
- [Fuzzing](#fuzzing)
- [Budgets](#budgets)

## Locally

`tools/check.sh` runs the checks in four tiers; each includes the one before it. Its builds are the presets of `CMakePresets.json`, in `out/<preset>`, reused from run to run, each with its logs beside it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The repository rules (`tools/check_repo.py`), the organization's documentation check, `clang-format-20` on the changed C++ files, `clang-development` built, and the tests the change reaches but the integration ones. | under 2 minutes |
| `pr` | Every rule on every file; `clang-development` and `clang-sanitize` built side by side, each running the tests the change reaches; then the integration tests the change reaches, in `clang-development`, a few at a time. | under 10 minutes |
| `full` | Every test in `clang-development`, `clang-sanitize`, `gcc-debug` and `clang-thread`, the web build with its tests under Node, the web page against the dedicated server over WebTransport, every engine shader compiled by a browser's WebGPU, a real browser playing the sample games, and every integration test. | under 20 minutes |
| `nightly` | Also `clang-shipping` and `wasm-shipping`, the tick budget and the web download budget measured in them. | extended |

Run `changed` before every push; `git config core.hooksPath tools/hooks` makes the pre-push hook run it. Run the other tiers through the workspace's heavy-job wrapper. `JOBS` (default 8) bounds the jobs of the whole check, shared between the trees that build at once.

A test reached by a change is one whose module or host is changed, or depends on a changed module through `tools/modules.txt`, however indirectly; `tools/affected.py` decides. The cross-module scenarios run whenever any module is reached. A change the module graph cannot place (the root build files, a vendored library, a game, a template, the test harness) reaches every test.

The documentation check skips the paths in `tools/docs-check-exclude.txt`: vendored code, and the areas whose source comments still cite records of the old documentation. The list shrinks as those comments are rewritten; it ends with `third_party` alone.

## In CI

`.github/workflows/ci.yml` runs on every pull request, on every push to `main` and every night.

| Job | What it guards |
|---|---|
| `changes` | Whether a pull request changes anything beyond documentation; when it does not, `check` is skipped. |
| `docs` | The organization's documentation check, from the pinned commit of `.github`, with the same exclusions as the local tier. |
| `zizmor` | The workflows: injection, persisted credentials, permissions, unpinned actions. |
| `check` | `tools/check.sh` on the self-hosted runner: `pr` on a pull request, `full` on `main`, `nightly` at night and by hand. The runner is the one machine with every toolchain the engine checks against (the pinned shader compilers, lavapipe, the web toolchain, a browser), and it keeps its build trees from run to run. |
| `required` | The one check the ruleset requires with the title check: every job passed, or was skipped for a documentation-only change. |

The self-hosted runner runs code from this repository's own branches only. A pull request from a fork never reaches it, and `required` fails for one that changes code: the engine takes no outside code, and `pr-gate.yml` closes such a pull request.

`pr-title.yml` checks that the title is a conventional commit with a scope from `AGENTS.md`. `codeql.yml` runs CodeQL on every pull request and on `main`, reading the sources without a build on a hosted runner. `scorecard.yml` grades the repository's security practices weekly. Dependabot proposes updates of the pinned actions once a week.

`extended.yml` is the extended nightly run, which is not a required check: macOS, Linux on arm64, Android in its emulator, Windows with Direct3D 12 (with the sample games exported, played and read through UI Automation, and Studio packaged), and every fuzz target for five minutes. `ios.yml` builds for the iOS simulator and plays there, on request only.

## Tests

Each module's tests are one executable registered under the module's name; scenarios across modules are in `tests/scenarios`, and each host program's tests beside it. The harness (`tests/harness`) registers named test functions with one failure macro, `RAWFRAME_EXPECT`, which reports and continues, and runs the fatal path in a child process.

Tests that drive windows, browsers, headsets or played games carry one of the labels `window`, `headset`, `alone`, `gesture`, `studio` or `heavy`, which the check treats as integration tests: they stay out of the `changed` tier and run a few at a time. A failing test is never run again: it fails the check.

Where lavapipe is installed, a device test that finds no adapter fails instead of skipping, and the loader sees lavapipe alone, so every run draws on the same rasterizer.

## Fuzzing

The fuzz targets (`tests/fuzz_*.cpp` beside each module that reads untrusted input) are built only in `clang-sanitize`. As tests, each runs a fixed 50,000 inputs from a fixed seed and its committed seed inputs, so a run is the same each time. CI's logs are public, so a failing fuzz target's output stops before libFuzzer prints the input; the finding is reproduced by running the target locally.

The extended nightly run fuzzes every target for five minutes through `tools/fuzz_target.sh`, which reports a finding by its target, kind and sanitizer report only. Each shard's corpus carries over in an Actions cache entry, minimized with libFuzzer's merge before it is saved; a shard that found something saves nothing. A crash becomes a regression test in its module's suite.

## Budgets

Over a budget, tests are pruned or merged before anything else is added. Integration tests are the expensive part: each plays a server, a client and a GPU on the processor, so they are kept to a few end-to-end scenarios. [benchmarks.md](benchmarks.md) covers the tick and download budgets the nightly tier measures.
