# Packaging and updates

How cooked content becomes something a player installs, verifies and updates without trusting where it came from. The modules are `signature`, `build`, `content` (Builds, libraries, Compositions), `release` and `install`; the programs are `rawframe-build`, `rawframe-export`, and the launcher's `rawframe-play` and `rawframe-install`.

## Contents

- [Trust](#trust)
- [Builds](#builds)
- [Libraries](#libraries)
- [Compositions](#compositions)
- [Releases and channels](#releases-and-channels)
- [Updates](#updates)
- [Export](#export)

## Trust

Everything that reaches a player's machine is verified by content, never by where it came from. A publisher holds Ed25519 keys (`rawframe-build key <publisher> <directory>` writes the secret, readable by its owner only, and the publisher key set others pin). Signatures are detached `{kid, sig}` envelopes over a record's exact bytes, with one algorithm and no algorithm field anywhere. A key set's typed key states decide a verdict, and a key the set does not list, or lists as revoked, fails closed. A signed record's bytes are verified before a byte of them is parsed.

## Builds

`rawframe-build <cooked> <output> <subject> <version> ...` packs a cook's output into a **Build**:

- The cook's receipt is the only proof accepted; every artifact is checked against it and the manifest it names.
- Each resource is cut into chunks by FastCDC, a pure function of its bytes with a gear table fixed by the container generation, so the same bytes always cut the same way.
- Each chunk worth compressing becomes one Zstandard frame under pinned parameters, and is stored by its digest.
- The BuildManifest lists every resource with its chunks and names the Build by the hash of its identity section, the **root hash**. Its exact bytes are signed with the publisher's key.

The packing format's bytes are golden (`modules/build/tests/golden`): moving Zstandard, the chunker or any parameter is a new packer generation, never a silent change.

## Libraries

A **library** is the Builds one machine holds. Each Build's manifest and signature are under `builds/<root>/`; the blobs of every Build share one content-addressed store, `sha256/<2>/<62>`, so a blob two Builds share is held once; the key set pinned for each publisher is `keys/<publisher>.keys`. An installed library also keeps the Compositions it has installed under `compositions/` and names the active one and those retained for rollback.

## Compositions

A **Composition** is the one exact playable closure a process runs: a game's Build and the exact Packages and Mods beside it, each named by its root hash, under one profile. Its identity is the SHA-256 of its canonical bytes. It carries no signature: its trust is its hash and the publisher signatures of the Builds it names. Whether a game takes the mods a Composition names is decided when it is opened, under the game's mod policy.

A server and a client play the library's active Composition from `content.library` alone.

## Releases and channels

A **Release** binds a subject's Composition to its artifacts' exact digests in an immutable record; its identity is the SHA-256 of the record's bytes, and no name or channel is ever its authority. A **channel pointer** says which Release a channel (`stable`, `beta`, `nightly`) of a subject points at now, with a sequence number. Both are signed by the subject's publisher.

`rawframe-build release <mirror> ...` publishes a Composition to a mirror as a Release and points the channel at it with the next sequence; `rawframe-build point` points a channel at a Release the mirror holds. A rollback is a higher sequence naming an older Release. A client refuses a pointer whose sequence is not past the one it holds, so a replayed pointer cannot take it backwards.

## Updates

`rawframe-install` updates a library from an **origin**: a mirror directory, or one served over HTTP or HTTPS (`--authorities <PEM file>` to trust other than the system's certificate authorities). Nothing an origin serves is trusted: a manifest is read only once a pinned key set verifies its signature, and a blob only once it hashes to what that manifest says.

An update is a chunk difference. The update plan is exactly the blobs the target Builds need that the store lacks, a pure function of the manifests and the store's inventory; it names digests only and is recomputed wherever it is carried out. The blobs are fetched and verified, every target Build is verified whole (blobs already held are verified again before they are reused), and only then does the installed pointer move, in one atomic write. Nothing installed is changed in place: new files are written under `staging/` first, so a library interrupted at any moment is either the old one or the new one.

`rawframe-install follow <library> <subject> <channel> <origin>` checks the channel, verifies every record against the publisher's pinned keys, refuses a replayed pointer and installs the Release's Composition.

## Export

`rawframe-export` is a headless operation over the same model, with a receipt. It runs the cook and the build tool as any Build is made, installs the Build into the output folder's library (which pins the publisher's key set), names it by a Composition, and copies the dedicated server, the client and the launcher beside it with their configurations. Without `--key` a key is made for this export alone and its secret deleted once the Build is signed. `export.receipt` lists every file written with its SHA-256, the Build's root and the Composition.

- A **native** folder's launcher, named after the game, starts the server on this machine's loopback, waits for its fingerprint, and starts the client pinned to it. The port is taken from the game's resource identity (20000 to 29999) unless given, so two exported games run at once. With `--follow <origin>`, the launcher runs `rawframe-install follow` before each play.
- A **web** export is a static site (`web/`: the page, the WebAssembly client, the Composition, the library and `play.json`) and a dedicated server that listens for browsers over WebTransport and writes its certificate's fingerprint into the site.
- **Android** and **iOS** exports package the phone clients with the game.

The launcher lives under `launcher/`, outside every engine closure, and is the only code that includes OpenSSL's TLS.
