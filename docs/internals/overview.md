# Overview

Rawframe is a network-first engine for 2D and 3D games and real-time simulations. Its core is C++23; games are written in Kest and in text documents, and driven by headless tools. This page explains how the parts fit; each topic has a page of its own.

## Contents

- [What the engine never compromises on](#what-the-engine-never-compromises-on)
- [Processes](#processes)
- [Modules](#modules)
- [Programs](#programs)
- [Closures](#closures)
- [Reference games](#reference-games)

## What the engine never compromises on

- **A game is made without Studio.** A person or an agent writes Kest and text documents and drives the headless tools, and that path is complete on its own. Studio is a convenience over it: nothing exists only in Studio, and there is no visual scripting or other editor-only form of authoring. Every Studio surface stands on a headless operation (see [authoring.md](authoring.md)).
- **The server decides.** Every game is built around an authoritative server, also when played alone. A client predicts and presents; it never gains authority through a local shortcut. Embedded, loopback and dedicated hosting keep the same gameplay meaning.
- **Determinism.** A World's simulation gives the same result for the same inputs on every platform and compiler, which replication, prediction and checkpoints rely on ([determinism.md](determinism.md)).
- **One owner per concept.** Each concept (an asset's identity, an entity's lifetime, a connection, a configuration value) has one module that owns it, one name and one public path. There are no globals, singletons or service locators: what a part needs is handed to it.
- **Untrusted input.** Downloaded games, mods, scripts, assets and network peers are untrusted unless a verified policy says otherwise. Parsers are bounded and fuzzed, Kest runs on fuel, and contract violations are refused with errors in every build ([security.md](security.md)).

## Processes

Every program is a **Host** that runs one **composition**: a set of **participants**, each provided by a module, chosen and ordered by a plan the host builds before anything starts. A participant declares the capabilities it provides and needs, the roles and platforms it may run in, the executors and quota it runs under, and its stop budget. The plan refuses a composition with a missing or doubled capability, reporting every problem at once ([runtime.md](runtime.md)).

A composition is built for one **role**: `DedicatedServer`, `Client`, `Tool` or `Test`. The Host iterates a fixed schedule of phases, which no participant may reorder:

`platform_poll`, `ingress`, `advance_clocks`, `run_worlds`, `presentation_extract`, `present`, `egress`, `maintenance`, `frame_end`.

A dedicated server has no presentation phases at all. Worlds tick inside `run_worlds` at their own fixed rate, independent of how often the Host iterates.

A Host reads one configuration file (`--config`), whose `key = value` lines are the run's immutable configuration; nothing reads the environment. Its diagnostics go to standard output as NDJSON, and it ends with an exit code that names the reason's category ([../operations/debugging.md](../operations/debugging.md)).

## Modules

Code lives in modules under `modules/<name>/`, each with `include/rawframe/<name>/`, `src/` and `tests/`. `tools/modules.txt` lists each module with the modules it may depend on; the build refuses any other dependency and `tools/check_repo.py` refuses an `#include` across a boundary the graph does not allow. The graph is acyclic, with `base` its only leaf.

| Layer | Modules |
|---|---|
| Foundation | `base`, `result`, `diagnostics`, `execution`, `composition`, `host` |
| Data and documents | `schema`, `document`, `graph`, `signature` |
| The World | `world` (entities, queries, the schedule, time and random streams), `world_runtime`, `world_snapshot`, `world_save` |
| Kest | `kest`, `kest_library`, `world_kest`, `input_kest` |
| Networking | `network`, `network_loopback`, `network_quic`, `network_web`, `world_replication`, `world_tooling` |
| Simulation | `physics`, `physics2d`, `physics3d`, `navigation`, `animation`, `world_animation`, `particles` |
| Content | `content`, `assets`, `game_content`, `game_textures`, `cook` and the importers (`*_import`), `build`, `release`, `install`, `process` |
| Presentation | `view`, `render`, `render_canvas`, `render_canvas_gpu`, `render_scene`, `render_scene_gpu`, `particles_gpu`, `material`, `mesh`, `texture`, `ui`, `world_ui`, `window`, `window_host`, `input`, `input_window`, `audio`, `world_audio`, `localization`, `world_localization`, `xr` |
| Authored worlds | `scene`, `scene_bake`, `blockout`, `terrain`, `tilemap`, `scatter`, `spline`, `sequence` |
| Authoring | `authoring`, `authoring_session`, `studio` |

A module's public headers are its API; everything under `src/` is private to it. Vendored libraries stay behind the module that owns them: no public header includes one, and only the owning module may include it (Maul RHI in the rendering modules, Maul Window in `window`, Maul UI in `ui`, Maul Nav in `navigation`, Maul Audio in `audio`).

## Programs

Each program is a host under `hosts/<name>/`, a module-like unit with its own line in the graph.

| Program | Role | What it does |
|---|---|---|
| `rawframe-server` | dedicated server | Runs a game's authoritative World and serves players over QUIC and WebTransport. |
| `rawframe-client` | client | Plays a game from a window on the desktop, or in a headset. |
| `rawframe-web-client` | client | The same in a browser, built for WebAssembly. |
| `rawframe` (Android), `rawframe-client` (iOS) | client | The phone clients. |
| `rawframe-bots` | tool | Headless bots playing a server over QUIC, for load and tests. |
| `rawframe-arena` | tool | A server and bots in one process over the loopback transport. |
| `rawframe-cook` | tool | Cooks a game's sources into content. |
| `rawframe-build` | tool | Packs cooked content into a signed Build, makes publisher keys, publishes Releases to channels. |
| `rawframe-export` | tool | Makes a game into a folder that plays on its own, natively or on the web. |
| `rawframe-author` | tool | The authoring operations from a command line, as a session, or as MCP tools. |
| `rawframe-studio` | tool | Studio, the editor over an authoring session. |
| `rawframe-debug` | tool | A Debug Adapter Protocol server for Kest, over a game's tooling endpoint. |
| `rawframe-bake`, `rawframe-thumbnail` | tool | Bake reflection probes; draw asset thumbnails. |
| `rawframe-import`, `rawframe-animation`, `rawframe-scene` | tool | Authoring help: sounds, animations from glTF, scenes from spawn lines. |

The launcher's programs live under `launcher/`, outside every engine closure: `rawframe-play` starts an exported game's server and client, and `rawframe-install` installs and updates a game's library from a mirror or over HTTPS ([packaging.md](packaging.md)).

## Closures

Some rules span many modules and are enforced by `tools/check_repo.py`, not left to review:

- **The dedicated server's closure** reaches no presentation, client, test-transport or toolchain module: no rendering, window, UI or audio, no localization, no authoring, no client input, no loopback or browser transport, no import, cook or build tooling.
- **No runtime reaches authoring.** The command layer, the session and Studio are a tool's, never a game's, a client's or a server's.
- **The launcher stands apart.** No module or host depends on a launcher unit, and only launcher sources include OpenSSL's TLS.
- **The web closure** is the modules `tools/web_modules.txt` lists, built for wasm32-wasi without threads.

## Reference games

The games under `games/` are architecture tests as much as samples: a core system is not done while it works only in a unit test or for one game.

| Game | What it proves |
|---|---|
| Runners (`runners`) | A small 2D platformer: prediction, lag-compensated shots, UI, sound, split screen. |
| Plaza (`plaza`) | The 3D sample: skinned, animated strollers, lights and materials. |
| Stalls (`tycoon`) | A small 3D tycoon: navigation, UI with text fields, commands, styles. |
| Gale (`gale`) | Ships on a rolling sea: buoyancy, moving physical bodies, competitive networking. |
| Quarry (`quarry`) | A voxel-like land of thousands of block entities, dug and built on. |
| Reach (`reach`) | VR hands at a table: pose input, grabbing and throwing. |
| Lighting (`lighting`) | The lighting fixture: materials, lights, shadows, probes and post-processing. |
| `arena`, `crowd`, `crates`, `ledges` and the variants | Fixtures for budgets and tests. |

`templates/` holds the games a new game is made from, by `rawframe-author new` or Studio's start screen.
