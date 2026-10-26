# Security

Downloaded games, mods, scripts, assets, network peers and external data are untrusted unless a verified policy grants more. Security is part of how each part is built, not a wrapper around it. This page gathers the rules that span modules.

## Untrusted input

- **Validated, never asserted.** Input from outside the process (files, network data, scripts, API arguments) is validated and refused with an error in every build. Assertions check internal invariants only ([runtime.md](runtime.md#assertions-and-the-fatal-path)).
- **Bounded.** Every parser, decoder and protocol has named limits: document sizes, nesting, counts, lengths, queued bytes. Nothing grows without one, and a limit passed is a typed refusal, not a truncation the caller cannot see.
- **One spelling.** Wire formats, documents and records accept exactly one canonical encoding, so two parsers cannot disagree about what bytes mean and nothing is smuggled in an alternate form.
- **Fuzzed.** Every reader of untrusted or complex input has a libFuzzer target: the wire and WebTransport, content and Build manifests, signatures and release records, scenes, materials, animation and UI documents, styles, localization, action sets, sounds, textures and images, meshes, fonts, the authoring session and its journal, Kest sources and cooked games, and the launcher's HTTP responses ([../operations/checks.md](../operations/checks.md#fuzzing)).
- **Decoders stay in tools.** Decoders of complex external formats (glTF, MP3, Vorbis, fonts) are linked into the cook only; a runtime reads only what a cook made. Fonts are rebuilt by the OpenType Sanitizer, and WOFF is refused.

## Code

- **Kest on fuel.** Every Kest machine has a required heap and fuel; a script that loops forever costs its fuel, not the process. A mod's code runs on a machine of its own under Kest's untrusted profile, offered only doors marked safe for untrusted code ([kest.md](kest.md#machines)).
- **No native code from content.** A game, a mod or a package carries Kest and data, never native code.
- **Staged writes.** Scripts change the World only through command buffers applied at barriers, and a failing script's changes are discarded whole.

## Network

- **The server decides.** Clients send input and commands, never state; a server consumes each input once and checks every command's kind, size and rate before any system reads it ([networking.md](networking.md#lanes)).
- **Admission first.** Nothing reaches a game before a connection is admitted. Compatibility is exact, with no downgrade. A peer that breaks the protocol is closed, and repeated malformed input ends the connection.
- **Secrets compared in constant time.** Tokens (tooling endpoints, shared sessions) are compared in a time that depends only on their lengths. A tooling endpoint grants only what its configuration names, and the debugger attaches only on loopback.
- **Unpredictable values from the OS.** Session nonces and epochs come from the operating system's secure source; simulation randomness never does.

## Content and updates

Everything installed is verified by content: Ed25519 signatures over exact bytes, checked before parsing, against key sets the library pins, and SHA-256 digests for every blob. An origin is never trusted, a replayed channel pointer is refused, and an interrupted update leaves the old installation whole ([packaging.md](packaging.md)).

## Diagnostics

Errors and logs never carry secrets, credentials, private content, full local paths or personal data. Fields carry a clearance, and a `Secret` field is removed before any sink writes. Identities in records are built from constants only, so untrusted text never becomes an identity.

## Reporting

Security issues are reported through GitHub's private vulnerability reporting on the repository, never in a public issue.
