# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

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
