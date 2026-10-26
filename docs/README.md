# Documentation

## Making games

- [Getting started](user/getting-started.md): build the engine, play a sample, make a game from the starter template.
- [The game description](user/game-description.md): every line a `.game` file may hold.
- [Gameplay in Kest](user/gameplay-in-kest.md): components, systems, doors, randomness, presentation.
- [Multiplayer](user/multiplayer.md): replication, prediction, interest, messages and commands, admission.
- [Input](user/input.md): action sets, bindings, the sample function, rebinding.
- [Scenes and authoring](user/scenes-and-authoring.md): scenes, `rawframe-author`, sessions, agents over MCP, sharing.
- [Assets](user/assets.md): sidecars, importers and cooking.
- [UI](user/ui.md), [Sound](user/audio.md), [Animation](user/animation.md), [Localization](user/localization.md).
- [Exporting](user/exporting.md): native folders, the web, Android and iOS, signing and updates.
- [Running a server](user/dedicated-server.md): configuration, logs, stopping, inspecting.
- [Studio](user/studio.md): the optional editor.
- [Mods](user/mods.md): extension points and mod descriptions.
- [Debugging Kest](user/debugging-kest.md): breakpoints and stepping from VS Code.
- [Troubleshooting](user/troubleshooting.md).

## How it works

- [Overview](internals/overview.md): what the engine never compromises on, processes, modules, programs, closures.
- [Glossary](internals/glossary.md).
- [Runtime](internals/runtime.md): failures, assertions, diagnostics, executors, composition, the Host.
- [The World](internals/world.md), [Determinism](internals/determinism.md), [Kest in the engine](internals/kest.md).
- [Networking](internals/networking.md), [Prediction and lag compensation](internals/prediction.md).
- [Content](internals/content.md), [Packaging and updates](internals/packaging.md).
- [Rendering](internals/rendering.md), [Materials](internals/materials.md), [UI](internals/ui.md), [Input and platforms](internals/input-and-platforms.md), [Audio](internals/audio.md), [Animation](internals/animation.md), [Physics and navigation](internals/physics-and-navigation.md), [Localization](internals/localization.md).
- [Authoring](internals/authoring.md): operations, sessions, agents, Studio.
- [Security](internals/security.md).

## Maintaining it

- [Development](operations/development.md): toolchain, presets, running tests, generated files.
- [Checks](operations/checks.md): the local tiers, the CI jobs, fuzzing and budgets.
- [Dependencies](operations/dependencies.md): what is vendored and how to move a pin.
- [Platforms](operations/platforms.md): Windows, macOS, Android, iOS, the web and headsets.
- [Benchmarks](operations/benchmarks.md): the tick, soak and web download budgets.
- [Debugging](operations/debugging.md): logs, crashes, sanitizers, running games, prediction divergence.
- [Release](operations/release.md).
- [Changelog](../CHANGELOG.md).
