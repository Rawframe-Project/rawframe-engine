# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

## [0.1.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.0.1...v0.1.0) (2026-10-30)


### ⚠ BREAKING CHANGES

* **deps:** `NavigationError::Capacity` is removed. A navmesh or avoidance that runs out of room, memory included, refuses with `NavigationError::Limit`, and a setting or input past a bound Maul Nav publishes refuses with `NavigationError::Invalid`.
* **replication:** mapping_declare, mapping_ack and mapping_retire carry runs of IDs, and a client acknowledges each declaration whole and in turn. Clients and servers built before this change are refused at admission.
* **replication:** the replication wire no longer has mapping_retire_ack, and a client sends no acknowledgement for a retirement. Clients and servers built before this change are refused at admission.

### Fixed

* **authoring:** wait for a link's last records to arrive, not an answer ([#48](https://github.com/Rawframe-Project/rawframe-engine/issues/48)) ([e5b9da6](https://github.com/Rawframe-Project/rawframe-engine/commit/e5b9da68004de1caf6b5f7c49e1cf5e1bf1a3aa5)), closes [#47](https://github.com/Rawframe-Project/rawframe-engine/issues/47)
* **build:** count the dependency builds' jobs in macOS's bash too ([#41](https://github.com/Rawframe-Project/rawframe-engine/issues/41)) ([ccc5a18](https://github.com/Rawframe-Project/rawframe-engine/commit/ccc5a18d31696a106324bc321184a98c3997a849)), closes [#23](https://github.com/Rawframe-Project/rawframe-engine/issues/23)
* **check:** compile inline Python in workflows and read quoted quotes ([#102](https://github.com/Rawframe-Project/rawframe-engine/issues/102)) ([3e85b3f](https://github.com/Rawframe-Project/rawframe-engine/commit/3e85b3f6346bdbb22b5195700261ba3370aebfcc)), closes [#101](https://github.com/Rawframe-Project/rawframe-engine/issues/101)
* **check:** keep bench.sh's verdict whole and compile inline Python ([#100](https://github.com/Rawframe-Project/rawframe-engine/issues/100)) ([08474cc](https://github.com/Rawframe-Project/rawframe-engine/commit/08474cc3ccc06c23f0fe2ad822d41d1f898c97e7)), closes [#99](https://github.com/Rawframe-Project/rawframe-engine/issues/99)
* **check:** kill a test's orphan only once that test has ended ([#78](https://github.com/Rawframe-Project/rawframe-engine/issues/78)) ([daa966e](https://github.com/Rawframe-Project/rawframe-engine/commit/daa966eb04c7890c7de2c81c27aa022232063da3)), closes [#72](https://github.com/Rawframe-Project/rawframe-engine/issues/72)
* **check:** pass only main's release commit through attribution ([#38](https://github.com/Rawframe-Project/rawframe-engine/issues/38)) ([43b3981](https://github.com/Rawframe-Project/rawframe-engine/commit/43b3981c65709a46fea09555c25e46a358053603)), closes [#5](https://github.com/Rawframe-Project/rawframe-engine/issues/5)
* **check:** read attribution only from trailers and footers ([#40](https://github.com/Rawframe-Project/rawframe-engine/issues/40)) ([1ce4a08](https://github.com/Rawframe-Project/rawframe-engine/commit/1ce4a0897f01c53e9a4210ada2ef67911427bec2)), closes [#38](https://github.com/Rawframe-Project/rawframe-engine/issues/38)
* **host:** define window_host's framesOf only where it is used ([#77](https://github.com/Rawframe-Project/rawframe-engine/issues/77)) ([c8b94f9](https://github.com/Rawframe-Project/rawframe-engine/commit/c8b94f958cf3f1949158beb08d869249c27c70fa)), closes [#76](https://github.com/Rawframe-Project/rawframe-engine/issues/76)
* **network:** count event bytes as received only once acknowledged ([#51](https://github.com/Rawframe-Project/rawframe-engine/issues/51)) ([bcecfd1](https://github.com/Rawframe-Project/rawframe-engine/commit/bcecfd1e5d18cee63428bf94739bebe874452e52)), closes [#49](https://github.com/Rawframe-Project/rawframe-engine/issues/49)
* **render:** list a runtime's adapters as many as Maul RHI reports ([#88](https://github.com/Rawframe-Project/rawframe-engine/issues/88)) ([07a021c](https://github.com/Rawframe-Project/rawframe-engine/commit/07a021c0b7b43e6e6be8d76392e5ffeabe2c9bc4)), closes [#87](https://github.com/Rawframe-Project/rawframe-engine/issues/87)
* **replication:** declare a join's entities over several publishes ([#53](https://github.com/Rawframe-Project/rawframe-engine/issues/53)) ([75350c3](https://github.com/Rawframe-Project/rawframe-engine/commit/75350c38a64471b9f42d2347775d2d2edb4bdf92)), closes [#52](https://github.com/Rawframe-Project/rawframe-engine/issues/52)
* **replication:** refuse a mapping budget past 128 KiB ([#58](https://github.com/Rawframe-Project/rawframe-engine/issues/58)) ([cc322f9](https://github.com/Rawframe-Project/rawframe-engine/commit/cc322f9ecb5c6c439c9fd0e4406e8839f348db58)), closes [#54](https://github.com/Rawframe-Project/rawframe-engine/issues/54)
* **replication:** retire a mass removal over several publishes ([#59](https://github.com/Rawframe-Project/rawframe-engine/issues/59)) ([b77c28f](https://github.com/Rawframe-Project/rawframe-engine/commit/b77c28f4d231ce41fdc5acd2ae7464a816fe5dd6)), closes [#55](https://github.com/Rawframe-Project/rawframe-engine/issues/55)
* **replication:** stop acknowledging mapping retirements ([#60](https://github.com/Rawframe-Project/rawframe-engine/issues/60)) ([96adce2](https://github.com/Rawframe-Project/rawframe-engine/commit/96adce2fd194380be17d673846cdeceb6e82caf3))
* **studio:** keep the test's accessibility bus a child of its script ([#73](https://github.com/Rawframe-Project/rawframe-engine/issues/73)) ([f7bd383](https://github.com/Rawframe-Project/rawframe-engine/commit/f7bd383d9cef1316d22c91963bfafd9e1616c234)), closes [#70](https://github.com/Rawframe-Project/rawframe-engine/issues/70)
* **studio:** run the git test only where git is a program ([#42](https://github.com/Rawframe-Project/rawframe-engine/issues/42)) ([ac312f7](https://github.com/Rawframe-Project/rawframe-engine/commit/ac312f7eee1fed88ed1dd56ce0b4836a51a1c3b7)), closes [#7](https://github.com/Rawframe-Project/rawframe-engine/issues/7)
* **studio:** stop Studio when a click step fails instead of hanging ([#69](https://github.com/Rawframe-Project/rawframe-engine/issues/69)) ([85e937d](https://github.com/Rawframe-Project/rawframe-engine/commit/85e937d8da8f51985f4516935345e20a28d19c30)), closes [#67](https://github.com/Rawframe-Project/rawframe-engine/issues/67)
* **ui:** report a platform's refusal of an adapter as unavailable ([#96](https://github.com/Rawframe-Project/rawframe-engine/issues/96)) ([39eaaba](https://github.com/Rawframe-Project/rawframe-engine/commit/39eaabaa98f736da98e810b1a11f4af0033b28ca)), closes [#95](https://github.com/Rawframe-Project/rawframe-engine/issues/95)
* **window:** run one slow iteration a frame so input is read between ([#46](https://github.com/Rawframe-Project/rawframe-engine/issues/46)) ([53acdbc](https://github.com/Rawframe-Project/rawframe-engine/commit/53acdbc067273ed359508e7364e8140761f79c11)), closes [#45](https://github.com/Rawframe-Project/rawframe-engine/issues/45)


### Changed

* **network:** send a connection's control frames together ([#50](https://github.com/Rawframe-Project/rawframe-engine/issues/50)) ([21c9df1](https://github.com/Rawframe-Project/rawframe-engine/commit/21c9df1ba744db7d6ef54e0d36fb0f61e98276fe)), closes [#49](https://github.com/Rawframe-Project/rawframe-engine/issues/49)
* **replication:** send mappings as runs of IDs ([#61](https://github.com/Rawframe-Project/rawframe-engine/issues/61)) ([aa5b0e5](https://github.com/Rawframe-Project/rawframe-engine/commit/aa5b0e57e199bc8caade19238e71f2cf1e08bb3e))


### Build

* **deps:** move Maul Nav to 0.2.0 ([#65](https://github.com/Rawframe-Project/rawframe-engine/issues/65)) ([b9966b1](https://github.com/Rawframe-Project/rawframe-engine/commit/b9966b11466da8346bbe305b270631ab86bb0bb6))

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
