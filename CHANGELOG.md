# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any release may change the API, the file formats and the wire protocol.

## [0.5.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.4.0...v0.5.0) (2026-11-05)

This release moves the engine to Maul UI 1.3.2, Maul Window 0.12.0 and Maul Unicode 1.0.0, and the debug adapter now says how a debugged game went. No wire, save, checkpoint or cooked format changes.

### Changed

- **Breaking:** The engine is built on Maul UI 1.3.2, Maul Window 0.12.0 and Maul Unicode 1.0.0, each vendored from its release archive with its SHA-256 checked. Maul Tween 1.0.0 and Maul RHI 0.15.0 stay. Maul Unicode 1.0.0 has 0.13.0's API, data and results. Layouts, styles, draw lists, line breaks, shaped text, glyph images and the accessibility tree are unchanged. What users see change, from Maul Window 0.8.1 to 0.12.0 ([#212](https://github.com/Rawframe-Project/rawframe-engine/pull/212)):
  - In Studio, a file dialog choice that holds a file the platform cannot give as a path gives the other paths, and Studio says some of the files chosen were left out. Before, the whole dialog failed and Studio did nothing. Such a file is a path that is not UTF-8, a portal's choice that is no local file, or an item with no file system path on Windows.
  - On X11, a paste takes only the answer to its own clipboard read, and a drop only its own conversion's answer, never a late answer to an earlier one. A read whose answer does not come fails after five seconds, and a drop is then delivered truncated, which Studio says.
  - On X11, an IBus or Fcitx commit or composition is held up to the text limit, and one past it resets the input state instead of being dropped. On X11 and Wayland, a compose sequence that types 64 bytes or more types its text instead of nothing.
  - On Wayland, a drag that enters while a drop is still being read is refused.
  - On Windows, a composition's caret, selection or clause inside a surrogate pair ends before the pair. A generic HID gamepad takes its controls from the first 32 Button and Generic Desktop capabilities, whatever other pages it has, so a pad with many controls gets its buttons, axes and hats. A pad whose reports cannot be read, or that is left with no controls, is not added.
  - In the browser, text another script pushes and a typed key's character turn a lone surrogate into U+FFFD instead of being lost.
  - On Android, a drop of a document the game cannot be granted no longer ends the game, and a drop's copy no longer waits without end on a provider that sends nothing.
- **Breaking:** `window::Windows::dialogFiles` returns a `window::DialogFiles`: the paths, and `truncated` when the platform left chosen files out. `view::FileDialogs::Answer` carries `truncated` ([#212](https://github.com/Rawframe-Project/rawframe-engine/pull/212)).

### Fixed

- The debug adapter says how a debugged game went: "the game's tooling endpoint kept silent" for a game that stopped answering, as one held by a native debugger does, or "closed" for one that stopped. It says so in the response to the request that found it, and in an `output` event before the session's `terminated` event. Before, a request was answered "the game's tooling endpoint went" either way, and a poll that found the game gone ended the session with no reason at all ([#214](https://github.com/Rawframe-Project/rawframe-engine/pull/214)).
- `rawframe-author connect` says "the endpoint closed" for a record sent after the endpoint closed but before the close was seen. Before, it said "the record could not be sent" ([#214](https://github.com/Rawframe-Project/rawframe-engine/pull/214)).

## [0.4.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.3.0...v0.4.0) (2026-11-04)

### Changed

- **Breaking:** A QUIC connection that ends because nothing came from the peer for its idle timeout is reported with the new `network::CloseReason::Silent`, where it was `PeerGone`. Game sessions still end with `EndReason::PeerGone`, so no game result changes ([#209](https://github.com/Rawframe-Project/rawframe-engine/pull/209)).
- **Breaking:** An authoring tooling link opens its QUIC with a 30 s idle timeout instead of 10 s, so the endpoint's own idle timeout decides when silence ends the link. With the server's default `network.quic.idle_timeout_ms` of 10000 nothing changes. Against a server set above that, Studio's play preview, which sends and polls without waiting, notices a server gone silent after that server's timeout, up to 30 s, instead of after 10 s ([#209](https://github.com/Rawframe-Project/rawframe-engine/pull/209)).
- The engine is built on Maul UI 1.3.0, vendored from its release archive with its SHA-256 checked. It names the same Maul Window, Unicode, Tween and RHI releases as 1.2.0, so those stay. No engine result changes: Maul UI 1.3.0's fixes are to a password field the engine never makes and to accessibility updates of kinds the engine never sends ([#207](https://github.com/Rawframe-Project/rawframe-engine/pull/207)).

### Fixed

- `rawframe-author connect` says "the endpoint kept silent" for an endpoint that stops answering, whichever comes first: its 10 s wait for a reply or the connection's idle timeout. When the idle timeout came first, it said "the endpoint closed" ([#209](https://github.com/Rawframe-Project/rawframe-engine/pull/209)).

## [0.3.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.2.2...v0.3.0) (2026-11-04)

### Changed

- A browser's WebTransport connection is closed with H3_SETTINGS_ERROR (0x109) when its SETTINGS frame carries one of HTTP/2's setting identifiers (0x00 or 0x02 to 0x05) or names one identifier twice. Before, it opened a session. RFC 9114 requires the first, and the second keeps the frame to one form. Unknown identifiers, including the grease ones browsers send, are still passed over, and browsers send neither refused kind ([#203](https://github.com/Rawframe-Project/rawframe-engine/pull/203)).

## [0.2.2](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.2.1...v0.2.2) (2026-11-04)

### Fixed

- `rawframe-author connect` names what happened when a record gets no reply. "The endpoint closed" now means the endpoint closed the connection, and "the endpoint kept silent" means the connection stayed open but no reply came within ten seconds. A record that could not be sent, such as one past the stream's 4 MiB, now gets "the record could not be sent". Before, all three printed "the endpoint closed". Standard output and the exit status are unchanged ([#194](https://github.com/Rawframe-Project/rawframe-engine/pull/194)).

## [0.2.1](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.2.0...v0.2.1) (2026-11-04)

### Fixed

- A navmesh bake whose area is finite but far from the grid, such as one reaching 1e30 m, is refused as `Invalid` without first converting its tile places to an index, which was undefined behavior. A game's navigation bakes the area its static geometry changed, and a game's scripts place that geometry. An area that bakes today bakes the same tiles ([#190](https://github.com/Rawframe-Project/rawframe-engine/pull/190)).

## [0.2.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.1.2...v0.2.0) (2026-11-04)

This release moves the engine to Maul UI 1.2.0 and the Maul Window, Unicode, Tween and RHI releases it builds against. Word movement, a screen reader's words, a few text input details and one window API change with it. The tilemap, terrain, scatter and blockout writers now keep every digit a number needs. No wire, save or cooked format changes.

### Changed

- **Breaking:** The engine is built on Maul UI 1.2.0, Maul Window 0.8.0, Maul Unicode 0.13.0, Maul Tween 1.0.0 and Maul RHI 0.15.0, each vendored from its release archive with its SHA-256 checked. Layout, line breaks, shaping, draw lists, glyph images and the shader containers are unchanged. What users see change ([#176](https://github.com/Rawframe-Project/rawframe-engine/pull/176), [#179](https://github.com/Rawframe-Project/rawframe-engine/pull/179)):
  - Moving and deleting by word in Studio's and a game UI's text fields, and the words a screen reader steps through, follow Unicode's word properties, as listed in [Maul UI 1.2.0's release notes](https://github.com/Rawframe-Project/maul-ui/releases/tag/v1.2.0). Superscripts, fractions, enclosed digits and the other General_Category No code points (912 of 915) are no longer words on their own; circled and squared letters, modifier and tone letters, and some punctuation and symbols of other scripts now are. Text of ordinary letters and digits moves as before.
  - In a game UI's field of lines, one Enter adds one line break and Tab only moves focus on every platform. On the web, Wayland, IBus, Fcitx and with the Win32 input method, a line feed or tab also arrived as text, so one Enter could add two line breaks and Tab typed a tab before moving focus. No control character reaches typed text any more.
  - A triple click in an editable text selects a paragraph across U+2028, a vertical tab or a form feed, where it stopped there.
  - On macOS, iOS, Android and the web, an input method's caret or selection inside a surrogate pair ends before the pair.
- **Breaking:** `window::Windows::requestTextInput` no longer takes a caret: the new `requestTextInputCaret` sets it, and its completion carries `RequestKind::TextInputCaret`. The engine's window host asks for text input only as editing starts or stops, so moving the caret no longer resets an input method's composition on Wayland ([#176](https://github.com/Rawframe-Project/rawframe-engine/pull/176)).
- **Breaking:** The tilemap, terrain, scatter and blockout writers write a number that needs more than nine significant digits with as many digits as it needs, so such a document reads back to its own values. Before, an edit through the authoring session, which Studio's modes use, rounded every such number in the document, including ones the edit never touched. A number of nine digits or fewer is written as before ([#180](https://github.com/Rawframe-Project/rawframe-engine/pull/180)).
- **Breaking:** Dragging a solid across Studio's blockout plan writes the height it carries over exactly as it was, where a height of more than six significant digits was rounded to six. Snapped X and Z within the default 10,000 m extent, and shorter heights, are written as before; past that extent a snapped X or Z is now exact too, so 10000.75 is written 10000.75 where it was 10000.8 ([#185](https://github.com/Rawframe-Project/rawframe-engine/pull/185)).
- **Breaking:** `animation::ClipLimits::maximumKeys` is renamed `maximumTrackKeys`, and `ClipLimits` gains `maximumClipKeys`, 4,194,304 by default. A clip document or glTF animation of more keys than that is refused as `OverLimit` before its keys are read. A glTF of about 200 KB could make `rawframe-animation from-gltf` build about 22 GB of tracks within the old limits ([#167](https://github.com/Rawframe-Project/rawframe-engine/pull/167)).

### Fixed

- Loading a cooked texture no longer holds a second encoding of it to check its form: the header, index, descriptor and padding are compared where they lie. Loading an 8192 x 8192 RGBA8 texture with its levels peaked at about 1 GiB and now takes about 683 MiB ([#172](https://github.com/Rawframe-Project/rawframe-engine/pull/172)).
- A blockout cylinder whose `sides` is negative or past what a count holds is refused without first converting it to a count, which was undefined behavior ([#181](https://github.com/Rawframe-Project/rawframe-engine/pull/181)).
- A scatter with a spacing far below its area, such as 5e-45, is placed without overflowing the index of its lookup squares, which was undefined behavior. The Runtime, the cook and Studio place scatters from their source text. Every scatter's placements are the same as before ([#183](https://github.com/Rawframe-Project/rawframe-engine/pull/183)).

## [0.1.2](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.1.1...v0.1.2) (2026-11-03)

### Fixed

- Importing a damaged Ogg Vorbis sound no longer frees memory the importer never allocated, or reads and writes past the comment list when the file declares a very large number of comments ([#130](https://github.com/Rawframe-Project/rawframe-engine/pull/130), [#134](https://github.com/Rawframe-Project/rawframe-engine/pull/134)).
- A damaged Ogg Vorbis file of a few kilobytes can no longer make the sound importer spend seconds and close to 200 MB before refusing it: what the Vorbis decoder holds at once is now bounded ([#144](https://github.com/Rawframe-Project/rawframe-engine/pull/144)).
- Vorbis setup headers that made the decoder overflow an `int` are handled: a lattice codebook of many dimensions and a codeword of 31 bits decode, and a list codebook whose value count passes what an `int` holds is refused, where each was undefined behavior before ([#148](https://github.com/Rawframe-Project/rawframe-engine/pull/148), [#152](https://github.com/Rawframe-Project/rawframe-engine/pull/152)).
- A Vorbis comment header whose vendor string or comments run past their packet is refused before the decoder reads it ([#155](https://github.com/Rawframe-Project/rawframe-engine/pull/155)).
- An Ogg Vorbis sound with a very large number of comments no longer takes time in proportion to the square of their count to import: a source of a few megabytes could hold the importer for a minute or more ([#157](https://github.com/Rawframe-Project/rawframe-engine/pull/157)).
- A sound source that decodes to samples that are not finite numbers is refused instead of being imported and cooked ([#150](https://github.com/Rawframe-Project/rawframe-engine/pull/150)).
- A glTF sampler with a wrap mode or filter glTF does not define is refused instead of being cooked as something else ([#120](https://github.com/Rawframe-Project/rawframe-engine/pull/120)).
- A glTF accessor's index count is checked against the mesh's index limit before the importer allocates room for it, so a small source can no longer end the `cook` process or make it allocate gigabytes ([#122](https://github.com/Rawframe-Project/rawframe-engine/pull/122)).
- The mesh and animation importers refuse glTF accessors whose data is not aligned to their component type, as glTF 2.0 requires ([#136](https://github.com/Rawframe-Project/rawframe-engine/pull/136)).
- Importing a run-length encoded (RLE8 or RLE4) BMP that ends its last row and then the bitmap, as encoders commonly write it, no longer makes the image decoder pass a null pointer to `memset`, which is undefined behavior ([#165](https://github.com/Rawframe-Project/rawframe-engine/pull/165)).
- `rawframe-animation from-gltf` refuses a joint name that is not UTF-8 instead of writing a skeleton no reader takes back ([#126](https://github.com/Rawframe-Project/rawframe-engine/pull/126)).
- `rawframe-animation from-gltf` refuses a channel that drives a joint already driven the same way, or a track past the clip's limit, before reading its keys. glTF channels may share one sampler, so a glTF of a few kilobytes could make it build gigabytes of tracks before refusing it ([#166](https://github.com/Rawframe-Project/rawframe-engine/pull/166)).
- Before admission, a frame within a few bytes of the pre-admission bound gets the same outcome whether it arrives whole or split ([#117](https://github.com/Rawframe-Project/rawframe-engine/pull/117)).

## [0.1.1](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.1.0...v0.1.1) (2026-11-01)

### Fixed

- A connection no longer ends as a protocol violation when several of a game's messages or commands arrive together. Over QUIC, what a peer sent in a tick can arrive at once, and a game's message and command lanes, bounded by its largest message and command, refused it when it passed that size, whole records included. Only a record held waiting for the rest of it is bounded now ([#115](https://github.com/Rawframe-Project/rawframe-engine/pull/115)).

## [0.1.0](https://github.com/Rawframe-Project/rawframe-engine/compare/v0.0.1...v0.1.0) (2026-10-30)

This release changes the replication wire: clients and servers built before it cannot connect to ones built after, and both are refused at admission. No saved or cooked format changes.

### Added

- `replication.mapping_bytes_per_publish` (64 bytes to 128 KiB, 16 KiB by default) bounds the bytes of entity mappings a publish sends one connection. Retirements go first, then declarations ([#53](https://github.com/Rawframe-Project/rawframe-engine/pull/53), [#59](https://github.com/Rawframe-Project/rawframe-engine/pull/59)).
- `replication.mappings_per_publish` (1 to 16,384, 4,096 by default) bounds how many entities a publish declares to one connection. On a Ryzen 9 5900X a client takes 3.0 to 3.8 ms to take 4,001 declarations in one pump, in a shipping build; a phone may take several times as long, so a game that targets phones may want a lower value ([#61](https://github.com/Rawframe-Project/rawframe-engine/pull/61)).

### Changed

- **Breaking:** Replication sends entity mappings as runs of IDs, and a client acknowledges each declaration whole and in turn. A client or server built before 0.1.0 is refused at admission ([#61](https://github.com/Rawframe-Project/rawframe-engine/pull/61)).
- **Breaking:** A client no longer acknowledges a mapping retirement, and the replication wire no longer has `mapping_retire_ack`. A client or server built before 0.1.0 is refused at admission ([#60](https://github.com/Rawframe-Project/rawframe-engine/pull/60)).
- **Breaking:** `NavigationError::Capacity` is removed, and navigation's refusals follow Maul Nav 0.2.0: a navmesh or avoidance that runs out of room, memory included, refuses with `NavigationError::Limit`, and a setting or input past a bound Maul Nav publishes, such as a zero cell size, refuses with `NavigationError::Invalid` ([#65](https://github.com/Rawframe-Project/rawframe-engine/pull/65)).
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
