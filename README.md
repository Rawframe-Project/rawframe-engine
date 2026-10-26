# Rawframe Engine

A network-first engine for 2D and 3D games, made with Kest and text documents, from a terminal or an editor.

[![CI](https://github.com/Rawframe-Project/rawframe-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/rawframe-engine/actions/workflows/ci.yml)
[![License: source-available](https://img.shields.io/badge/license-source--available-blue)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/rawframe-engine)](https://github.com/Rawframe-Project/rawframe-engine/releases)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/Rawframe-Project/rawframe-engine/badge)](https://scorecard.dev/viewer/?uri=github.com/Rawframe-Project/rawframe-engine)

Most engines treat multiplayer as something added later, and their editors as the only place a game can really be made. Rawframe starts from the other end:

- **Every game is networked.** An authoritative server runs the game even when one person plays alone, so a game made for one player already plays online. Clients predict their own player and are corrected by ordinary state, and shots hit what the shooter saw.
- **Deterministic simulation.** Worlds tick at a fixed rate and give the same bits on every platform and compiler, which prediction, checkpoints and bots rely on.
- **Made without an editor.** A game is a Kest program and text documents: scenes, materials, UI, sounds, actions and strings, each in one canonical form that diffs and merges. Every change is a typed operation that `rawframe-author`, a script or an AI agent over MCP can run. Studio, the editor, calls the same operations.
- **2D and 3D as equals.** A 2D canvas with sprites and tilemaps, and a 3D renderer with physically based lighting, shadows, probes and post-processing; physics through Maul2D and Maul3D; navigation, animation, UI with accessibility, spatial sound and localization.
- **Safe with downloaded content.** Scripts and mods run on fuel in a sandbox, every parser is bounded and fuzzed, and installed games are verified by signature and digest, never by where they came from.

## Quick start

On Linux with CMake 3.28, Ninja, ccache, GCC 14 and Clang 20, and the development packages listed in [development setup](docs/operations/development.md#toolchain):

```sh
git clone https://github.com/Rawframe-Project/rawframe-engine.git
cd rawframe-engine
cmake --preset clang-development
cmake --build out/clang-development
tree=out/clang-development
"$tree/hosts/export/rawframe-export" games/runners /tmp/runners \
  --cook "$tree/hosts/cook/rawframe-cook" --build "$tree/hosts/build/rawframe-build" \
  --server "$tree/hosts/dedicated_server/rawframe-server" \
  --client "$tree/hosts/client/rawframe-client" --play "$tree/launcher/play/rawframe-play"
/tmp/runners/runners
```

That exports Runners, the 2D sample, into a folder with its own server and client, and plays it. [Getting started](docs/user/getting-started.md) goes on to make a game of your own.

## Platforms

| Platform | Clients | Drawing |
|---|---|---|
| Linux (x86-64, arm64) | desktop, headsets | Vulkan |
| Windows | desktop | Direct3D 12 |
| macOS | desktop | Metal |
| Web | any browser with WebGPU and WebTransport | WebGPU |
| Android, iOS | phones | Vulkan, Metal |

The dedicated server runs on Linux, Windows and macOS without a GPU or a display. Linux is checked on every change, the others every night, and iOS on request.

## Status

The engine is at 0.x. Every release may change the API, the file formats and the wire protocol; there is no stability promise before 1.0.

## Documentation

- [Making games](docs/README.md#making-games): getting started, the game description, Kest, multiplayer, exporting.
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

## Contributing

Issues are welcome; pull requests are not accepted. See [CONTRIBUTING.md](CONTRIBUTING.md). The engine is developed by its maintainers with AI coding agents, directed and reviewed by them; see the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

Source-available under the Rawframe Source-Available License; see [LICENSE](LICENSE). It is not an open source license. Vendored libraries under `third_party/` keep their own licenses. "Rawframe", "Kest" and "Maul" are trademarks; see the [trademark policy](https://github.com/Rawframe-Project/.github/blob/main/TRADEMARK.md).
