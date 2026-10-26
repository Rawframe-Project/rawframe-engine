# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

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
