# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

## [0.1.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.0.1...v0.1.0) (2026-10-30)

This release changes the replication wire: clients and servers built before it cannot connect to ones built after, and both are refused at admission. No saved or cooked format changes.

### Breaking

- Replication sends entity mappings as runs of IDs, and a client acknowledges each declaration whole and in turn. A client or server built before 0.1.0 is refused at admission ([#61](https://github.com/Rawframe-Project/rawframe-engine/pull/61)).
- A client no longer acknowledges a mapping retirement, and the replication wire no longer has `mapping_retire_ack`. A client or server built before 0.1.0 is refused at admission ([#60](https://github.com/Rawframe-Project/rawframe-engine/pull/60)).
- `NavigationError::Capacity` is removed. A navmesh or avoidance that runs out of room, memory included, refuses with `NavigationError::Limit`, and a setting or input past a bound Maul Nav publishes, such as a zero cell size, refuses with `NavigationError::Invalid` ([#65](https://github.com/Rawframe-Project/rawframe-engine/pull/65)).

### Added

- `replication.mapping_bytes_per_publish` (64 bytes to 128 KiB, 16 KiB by default) bounds the bytes of entity mappings a publish sends one connection. Retirements go first, then declarations ([#53](https://github.com/Rawframe-Project/rawframe-engine/pull/53), [#59](https://github.com/Rawframe-Project/rawframe-engine/pull/59)).
- `replication.mappings_per_publish` (1 to 16,384, 4,096 by default) bounds how many entities a publish declares to one connection. On a Ryzen 9 5900X a client takes 3.0 to 3.8 ms to take 4,001 declarations in one pump, in a shipping build; a phone may take several times as long, so a game that targets phones may want a lower value ([#61](https://github.com/Rawframe-Project/rawframe-engine/pull/61)).

### Changed

- Entity mappings take far less traffic. A publish declares its entities as one run of IDs in a single frame, and a retirement costs a few bytes for what was declared together, or a byte or two an ID for what a removal scatters. Before, every mapping frame was 13 to 14.5 bytes ([#61](https://github.com/Rawframe-Project/rawframe-engine/pull/61)).
- A connection's control frames go to the transport together, in pieces of up to 1,024 bytes, instead of one stream send each. A frame still goes out in the Host iteration it was sent in, or at the latest at the next one ([#50](https://github.com/Rawframe-Project/rawframe-engine/pull/50)).
- A navmesh's fingerprint changes with Maul Nav 0.2.0's tile format; its paths do not. The engine bakes navmeshes in memory at game start and stores no tiles ([#65](https://github.com/Rawframe-Project/rawframe-engine/pull/65)).
- The engine is built on Maul2D 0.6.0, Maul Nav 0.2.0, Maul RHI 0.13.0, Maul Audio 0.4.0, Maul Window 0.7.0, Maul UI 0.4.0, Maul Unicode 0.8.0 and, new, Maul Tween 0.12.0, each vendored from its release archive with its SHA-256 checked. Maul3D stays at 0.7.0. 2D physics results are unchanged ([#64](https://github.com/Rawframe-Project/rawframe-engine/pull/64), [#65](https://github.com/Rawframe-Project/rawframe-engine/pull/65), [#68](https://github.com/Rawframe-Project/rawframe-engine/pull/68), [#74](https://github.com/Rawframe-Project/rawframe-engine/pull/74), [#75](https://github.com/Rawframe-Project/rawframe-engine/pull/75), [#81](https://github.com/Rawframe-Project/rawframe-engine/pull/81), [#82](https://github.com/Rawframe-Project/rawframe-engine/pull/82), [#83](https://github.com/Rawframe-Project/rawframe-engine/pull/83), [#84](https://github.com/Rawframe-Project/rawframe-engine/pull/84), [#85](https://github.com/Rawframe-Project/rawframe-engine/pull/85), [#86](https://github.com/Rawframe-Project/rawframe-engine/pull/86), [#91](https://github.com/Rawframe-Project/rawframe-engine/pull/91)).

### Fixed

- A player joining in view of many entities no longer loses its connection during the join. The server declares them over several publishes within the mapping budget, the player's own entity first ([#53](https://github.com/Rawframe-Project/rawframe-engine/pull/53)).
- Removing most of a connection's entities at once, destroyed or out of its interest, no longer ends the connection. Their retirements go over several publishes, and a removed entity stands still on the client until its retirement arrives ([#59](https://github.com/Rawframe-Project/rawframe-engine/pull/59)).
- Replication settings made in code refuse a mapping budget above 128 KiB, the bound the configuration key already had ([#58](https://github.com/Rawframe-Project/rawframe-engine/pull/58)).
- Over QUIC, a peer that stops acknowledging guaranteed events is ended within 2 s as exhausted, not only at the 10 s idle timeout. A stream send now stays pending until the peer has acknowledged it ([#51](https://github.com/Rawframe-Project/rawframe-engine/pull/51)).
- On Direct3D 12, the engine's shaders compute what their source says, as on Vulkan, Metal and WebGPU: DXC no longer treats their float math as fast and rearranges it. A material a game cooks gets the same strict DXIL ([#86](https://github.com/Rawframe-Project/rawframe-engine/pull/86)).
- On a slow Android device, a touch no longer waits long enough for Android to show "Application Not Responding". A window frame runs a further Host iteration only while it has taken less than 16 ms, so input is read between slow iterations ([#46](https://github.com/Rawframe-Project/rawframe-engine/pull/46)).
- Closing Studio during Play no longer waits up to 1.5 s for an answer it never used. On the Windows test machine it took 1.57 s instead of 3.02 s ([#48](https://github.com/Rawframe-Project/rawframe-engine/pull/48)).
- A platform's refusal of an accessibility adapter, such as UI Automation off a thread in a COM apartment, reaches the game as `UiError::Unavailable` instead of `UiError::Invalid` ([#96](https://github.com/Rawframe-Project/rawframe-engine/pull/96)).
- Configuring the iOS simulator tree on macOS no longer stops before MsQuic and OpenSSL are built: macOS's bash 3.2 refused how the build scripts wrote the job count ([#41](https://github.com/Rawframe-Project/rawframe-engine/pull/41)).

## 0.0.1

The first release. It is below 1.0.0, so any release may change the API, the file formats and the wire protocol.

Rawframe Engine is under the Rawframe Source-Available License 1.1, which is not an open source license. The libraries vendored under `third_party/` keep their own licenses, each named in `docs/operations/dependencies.md`: MIT (the Maul family, Kest, HarfBuzz, MsQuic, cgltf and others), Apache-2.0 (OpenSSL, the OpenXR SDK, Wuffs), BSD-3-Clause (Opus, Zstandard, OpenType Sanitizer), the FreeType License, Unicode-3.0 (the CLDR data and Maul Unicode's tables), and public domain or MIT-0 (miniaudio). "Rawframe", "Kest" and "Maul" are trademarks; the README links the policy.

Platforms (`docs/operations/platforms.md`): Linux x86-64 with Vulkan is checked on every tier, and the web with WebGPU in the full tier. Linux arm64, Windows (Direct3D 12, built with clang-cl), macOS (Metal) and Android (Vulkan, in an emulator) are built and played by the extended nightly. iOS runs in the simulator on request. OpenXR is checked against Monado, headless. The dedicated server builds wherever the engine does.

Building (`docs/operations/development.md`) takes CMake 3.28, Ninja and ccache with GCC 14 and Clang 20: `cmake --preset clang-development && cmake --build out/clang-development`. The first configure builds MsQuic and OpenSSL into `~/.cache/rawframe`. The shader containers are committed, so the Slang and DXC toolchain is needed only for materials with programs of their own.

The engine is built on Maul2D 0.5.0, Maul3D 0.7.0, Maul RHI 0.9.0, Maul Nav 0.0.4 and Maul Audio 0.0.1, from their release archives; Maul Unicode 0.3.0 at 45523494; Maul Window 0.13.0 and its main after it, at a0b6e441; Maul UI 0.5.11 and its main after it, at c680614c; and Kest at fa092f54, a main commit with no release (`docs/operations/dependencies.md`).

The release carries the tagged source archive, `SHA256SUMS`, an SPDX SBOM and a build provenance attestation. There are no prebuilt binaries.

### Added

- Games written in Kest and a game description: components, systems in fixed World phases, scenes, prefabs and scatters, seeded random streams, and reloading the program and scenes while a server runs.
- Networked play through an authoritative server over QUIC, and WebTransport for browsers: replication with spatial interest and districts, client prediction with exact resimulation and predicted effects, interpolation, lag-compensated rays, guaranteed messages and player commands, a game's own admission rule, and headless bots.
- 2D and 3D physics through Maul2D and Maul3D, with characters, joints, attachments, collision classes and queries; navmeshes and agent avoidance through Maul Nav.
- A 2D canvas of sprites, tilemaps, particles and trails, and a 3D renderer with physically based materials, cascaded and atlas shadows, clustered lights, image-lit skies, reflection probes, ambient occlusion, screen-space reflections, decals, bloom, depth of field, motion blur, anti-aliasing, tonemapping and grading, render textures, split screen and render scale; Vulkan, Direct3D 12, Metal and WebGPU.
- Previews in a running game and in Studio split up to four ways, each view made when its region is first placed ([#26](https://github.com/Rawframe-Project/rawframe-engine/pull/26), [#28](https://github.com/Rawframe-Project/rawframe-engine/pull/28)).
- A frame that cannot be made is reported in `frame_failed` with the outcome Maul RHI gave and, where one of the device's limits refused it, that limit; `frame_summary` reports the most any frame took of each frame limit and the limit the frames came closest to ([#27](https://github.com/Rawframe-Project/rawframe-engine/pull/27), [#37](https://github.com/Rawframe-Project/rawframe-engine/pull/37)).
- Materials as graph documents, with instances, canvas and post-process materials, and materials with programs of their own.
- Skeletal animation with graphs, state machines, blend spaces, masks, IK and root motion, posed on the server only where gameplay needs it.
- UI as World data: flex layout, style classes with transitions, shaped and translated text, text fields with input methods, navigation without a pointer, and screen readers on every platform.
- Input through action sets with contexts, rebinding, local players, gamepads, touch, haptics and tracked controllers and hands.
- Spatial sound through a declared mixer and sounds, with streaming Opus.
- Localization with string tables, translations, MessageFormat 2.0 messages, CLDR plurals and numbers, and pseudo-localization.
- Checkpoints and curated saves, migrated across layout changes.
- Cooking of textures, glTF meshes, sounds, fonts, materials, animation, scenes, blockouts, terrains, tilemaps and text, with receipts and a derived-data cache.
- Signed Builds, libraries updated by chunk difference, Compositions, Releases on channels, and export to a native folder, a website, Android and iOS.
- Mods with data, event, service and replacement points, run untrusted.
- Authoring without an editor: typed operations with undo, `rawframe-author` sessions, MCP tools for AI agents, shared sessions over QUIC, and previews in a running game.
- Studio, an editor over the same operations, with play in the viewport, profiling and network panels, and Kest extensions.
- Debugging Kest from VS Code with breakpoints and stepping, and a tooling endpoint for inspecting running games.
- OpenXR headsets, with flat and headset players in one game.
