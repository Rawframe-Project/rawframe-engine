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
| `changed` | The repository rules (`tools/check_repo.py`), the shader containers' stamps (`tools/gen_shaders.py --check`), the organization's documentation check, `clang-format-20` on the changed C++ files, `clang-development` built, and the tests the change reaches but the integration ones. | under 2 minutes |
| `pr` | Every rule on every file; `clang-development` and `clang-sanitize` built side by side, each running the tests the change reaches; then the integration tests the change reaches, in `clang-development`, a few at a time. | under 10 minutes |
| `full` | Every test in `clang-development`, `clang-sanitize`, `gcc-debug` and `clang-thread`, the web build with its tests under Node, the web page against the dedicated server over WebTransport, every engine shader compiled by a browser's WebGPU, a real browser playing Runners and the plaza, and every integration test. | under 20 minutes |
| `nightly` | Also `clang-shipping` and `wasm-shipping`, the tick budget and the web download budget measured in them. | extended |

Run `changed` before every push; `git config core.hooksPath tools/hooks` makes the pre-push hook run it. Run the other tiers through the workspace's heavy-job wrapper. `JOBS` (default 8) bounds the jobs of the whole check, shared between the trees that build at once. When their cache misses, configuring a tree builds the vendored OpenSSL and MsQuic, natively or for the web, each with half of `JOBS`: the two may build at once, and trees waiting on the same build use no processor.

A test reached by a change is one whose module or host is changed, or depends on a changed module through `tools/modules.txt`, however indirectly; `tools/affected.py` decides. The cross-module scenarios run whenever any module is reached. A change the module graph cannot place (the root build files, a vendored library, a game, a template, the test harness) reaches every test.

The tests of every tier run under `tools/reap.py`, which on Linux takes over any process a test leaves behind as the test ends, and any helper a test forks away from itself, such as a daemon. Every test, and every process it starts, carries the test's name in `RAWFRAME_TEST_NAME` (the root `CMakeLists.txt` sets it), so a test's helper is left alone while the test runs. A server, client or helper still running a minute after its test ended, or ten seconds after the last test, is killed by its pid and named with its test, and the check fails: a stopped server ignores stop signals while its leak check runs, so one stuck there would otherwise outlive the run. A process that carries no test's name is killed a minute after it was handed over.

The documentation check skips the paths in `tools/docs-check-exclude.txt`: `third_party` alone, the vendored code, which follows its upstream's own documentation.

## In CI

`.github/workflows/ci.yml` runs on every pull request, on every push to `main` and every night.

| Job | What it guards |
|---|---|
| `changes` | Whether a pull request changes anything beyond documentation; when it does not, `check` is skipped. |
| `docs` | The organization's documentation check, from the pinned commit of `.github`, with the same exclusions as the local tier. |
| `zizmor` | The workflows: injection, persisted credentials, permissions, unpinned actions. |
| `check` | `tools/check.sh` on the self-hosted runner: `pr` on a pull request, `full` on `main`, `nightly` at night and by hand. The runner is the one machine with every toolchain the engine checks against (the pinned shader compilers, lavapipe, the web toolchain, a browser), and it keeps its build trees from run to run. The machine is shared, so the check runs with eight jobs at a lower priority. |
| `required` | The one check the ruleset requires with the title check: every job passed, or was skipped for a documentation-only change. |

The self-hosted runner runs code from this repository's own branches only. A pull request from a fork never reaches it, and `required` fails for one that changes code: the engine takes no outside code, and `pr-gate.yml` closes such a pull request.

The runner's workspace and build trees are shared between pull request runs and runs on `main`, so a branch of this repository can leave something a later run sees; only those with write access can push one. Nothing that ships comes from that workspace: release archives, their checksums, SBOMs and attestations are built by `release.yml` on a hosted runner from a clean checkout of the tag.

`pr-title.yml` checks that the title is a conventional commit with a scope from `AGENTS.md`. `codeql.yml` runs CodeQL on every pull request and on `main`, reading the sources without a build on a hosted runner. `scorecard.yml` grades the repository's security practices weekly. Dependabot proposes updates of the pinned actions once a week.

`extended.yml` is the extended nightly run, which is not a required check: macOS, Linux on arm64, Android in its emulator, Windows with Direct3D 12 (with the sample games exported, played and read through UI Automation, and Studio packaged), and every fuzz target for five minutes. `ios.yml` builds for the iOS simulator and plays there, on request only.

## Tests

Each module's tests are one executable registered under the module's name; scenarios across modules are in `tests/scenarios`, and each host program's tests beside it. The harness (`tests/harness`) registers named test functions with one failure macro, `RAWFRAME_EXPECT`, which reports and continues, and runs the fatal path in a child process.

Tests that drive windows, browsers, headsets or played games, or that hold a real-time bound, carry the label `integration`: they stay out of the `changed` tier, run in `clang-development` only, a few at a time, and are kept to one scenario for each path a player or an author takes that a module's own tests cannot reach. Studio has five: started, its command palette, a shared session joined, a game played in its viewport, and an export. A failing test is never run again: it fails the check.

Where lavapipe is installed, a device test that finds no adapter fails instead of skipping, and the loader sees lavapipe alone, so every run draws on the same rasterizer.

## Fuzzing

The fuzz targets (`tests/fuzz_*.cpp` beside each module that reads untrusted input) are built only in `clang-sanitize`. As tests, each runs a fixed 50,000 inputs from a fixed seed and its committed seed inputs, so a run is the same each time. CI's logs are public, so a failing fuzz target's output stops before libFuzzer prints the input; the finding is reproduced by running the target locally.

The extended nightly run fuzzes every target for five minutes through `tools/fuzz_target.sh`, which reports a finding by its target, kind and sanitizer report only. Each shard's corpus carries over in an Actions cache entry, minimized with libFuzzer's merge before it is saved; a shard that found something saves nothing. A crash becomes a regression test in its module's suite.

## Budgets

Over a budget, tests are pruned or merged before anything else is added. Integration tests are the expensive part: each plays a server, a client and a GPU on the processor, so they are kept to a few end-to-end scenarios. [benchmarks.md](benchmarks.md) covers the tick and download budgets the nightly tier measures.
