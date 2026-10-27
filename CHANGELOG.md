# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

## [0.0.1](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.0.1...v0.0.1) (2026-10-27)


### Added

* **render:** report what frames take of each device limit ([#37](https://github.com/Rawframe-Project/rawframe-engine/issues/37)) ([fa5f13f](https://github.com/Rawframe-Project/rawframe-engine/commit/fa5f13fc525fa20a04e8f01100863596841b6222))


### Fixed

* **render:** give a frame room for a preview split four ways ([#26](https://github.com/Rawframe-Project/rawframe-engine/issues/26)) ([3c80345](https://github.com/Rawframe-Project/rawframe-engine/commit/3c80345965fdfcae4ab3cfe959cb5f2fbac620b2)), closes [#20](https://github.com/Rawframe-Project/rawframe-engine/issues/20)
* **render:** make a split-screen view when its region is first placed ([#28](https://github.com/Rawframe-Project/rawframe-engine/issues/28)) ([bbc67c4](https://github.com/Rawframe-Project/rawframe-engine/commit/bbc67c46434aadc95694c201ffb3f4b15248a25f)), closes [#21](https://github.com/Rawframe-Project/rawframe-engine/issues/21)
* **render:** report the outcome Maul RHI gave when a frame fails ([#27](https://github.com/Rawframe-Project/rawframe-engine/issues/27)) ([debfc9f](https://github.com/Rawframe-Project/rawframe-engine/commit/debfc9f945382479257bbacb8ca5e2f5dc21efb3)), closes [#25](https://github.com/Rawframe-Project/rawframe-engine/issues/25)


### Chores

* move the repository to the organization standard ([#3](https://github.com/Rawframe-Project/rawframe-engine/issues/3)) ([057e8fc](https://github.com/Rawframe-Project/rawframe-engine/commit/057e8fc6b841025aac0afc9b64d5c00e3a54b0d0)), closes [#2](https://github.com/Rawframe-Project/rawframe-engine/issues/2)

## 0.0.1

The first release.

### Added

- Games written in Kest and a game description: components, systems in fixed World phases, scenes, prefabs and scatters, seeded random streams, and reloading the program and scenes while a server runs.
- Networked play through an authoritative server over QUIC, and WebTransport for browsers: replication with spatial interest and districts, client prediction with exact resimulation and predicted effects, interpolation, lag-compensated rays, guaranteed messages and player commands, a game's own admission rule, and headless bots.
- 2D and 3D physics through Maul2D and Maul3D, with characters, joints, attachments, collision classes and queries; navmeshes and agent avoidance through Maul Nav.
- A 2D canvas of sprites, tilemaps, particles and trails, and a 3D renderer with physically based materials, cascaded and atlas shadows, clustered lights, image-lit skies, reflection probes, ambient occlusion, screen-space reflections, decals, bloom, depth of field, motion blur, anti-aliasing, tonemapping and grading, render textures, split screen and render scale; Vulkan, Direct3D 12, Metal and WebGPU.
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
