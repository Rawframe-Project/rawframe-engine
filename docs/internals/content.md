# Content

How authored sources become what a running game reads: identities, sidecars, the cook, the content store and the lifecycle of decoded assets. The modules are `content`, `cook` and the importers (`audio_import`, `texture_import`, `mesh_import`, `animation_import`, `font_import`), `assets`, `game_content` and `game_textures`. Packing cooked content into signed Builds is in [packaging.md](packaging.md).

## Contents

- [Identities](#identities)
- [Sidecars](#sidecars)
- [Cooking](#cooking)
- [Sources and the store](#sources-and-the-store)
- [Decoded assets](#decoded-assets)
- [Development and cooked play](#development-and-cooked-play)

## Identities

A **resource** is one logical piece of content, named by a 128-bit identity that is stable across renames, moves and new revisions of its bytes. Zero is no resource. Separately:

- its **type** says what it means (a texture, a mesh, a sound);
- its **representation** says how its bytes are read, as a lowercase dotted identifier such as `rawframe.audio.opus`;
- its **digest** (SHA-256) and length say which exact bytes.

None is interchangeable with another, or with a path. Code that resolves an authored name (a game's `scene` line, a scene's instance) goes through the name's sidecar to the resource; nothing at run time looks content up by path.

## Sidecars

Every source file has a sidecar beside it, `<source>.rfmeta`, a small JSON document naming the resource the source becomes, the importer that makes it, the importer's settings, and the map of subassets the source holds (the meshes of a glTF, the clips of an animation) to the resources they become:

```json
{ "schema": 1, "resourceId": "2cf4e6afa86830492ee14a9e8067986a", "importer": "rawframe.texture", "settings": { "exact": true } }
```

`rawframe-cook --map <sources>` gives every subasset a source holds and its sidecar does not map a new random identity, and writes the sidecar. A new game from a template gets fresh identities for every sidecar it copies.

## Cooking

`rawframe-cook <sources> <output> [<cache>]` reads every sidecar under a directory and runs its importer, which turns the source into immutable artifacts named by their digests. Each source is cooked twice and the two results compared: a cook of the same inputs must give the same bytes, and two that differ are an error. Artifacts go to `objects/<digest>` under the output, and only when nothing failed does the cook publish `content.manifest`, naming each resource with its type, representation, length, digest and place, and `cook.receipt`, which proves the cook. Two sidecars claiming one resource are an error. Every file an importer reads besides its source is recorded with a digest of what it saw; the derived-data cache reuses an artifact only after verifying it and every read that made it, so an unchanged source is not cooked again.

Importers, by name: `rawframe.texture` (PNG, JPEG, BMP, TGA and Radiance pictures, in linear light, BC7 or exact, into KTX 2.0), `rawframe.mesh` (glTF), `rawframe.audio` (WAVE, Ogg Vorbis and MP3 into the short-form tier or Opus), `rawframe.font` (TrueType and OpenType, rebuilt by the OpenType Sanitizer and kept only once the runtime's reader takes the result), `rawframe.material`, `rawframe.animation`, `rawframe.scene`, `rawframe.blockout`, `rawframe.terrain`, `rawframe.tilemap`, `rawframe.scatter`, `rawframe.sequence`, `rawframe.text` (string tables and translations), `rawframe.kest`, `rawframe.mod` and `rawframe.game`. The game importer cooks a game description with everything it names, so a process reads the game from content alone.

The cook is import tooling. It links the decoders of untrusted formats (glTF, MP3, Vorbis, fonts) and never enters a client or a server; a runtime reads only what a cook made. A material whose graph needs its own shader program is compiled through the pinned shader toolchain; without it such a material is refused and everything else cooks.

A stop request stops a cook before its next source and publishes nothing.

## Sources and the store

A **source** reads a validated locator's exact bytes and nothing else. There are three kinds, all built in `content` and nowhere else: a memory source, a directory source confined to its root, and a Build read chunk by chunk in its verification order. A locator is relative, `/`-separated, lowercase, with no empty, `.` or `..` segment.

The **catalog** combines every selected manifest into one immutable lookup of resources, each bound to the source that holds it, with a fingerprint of its meaning. The **store** is a Runtime's one catalog generation and its sources. A read returns bytes only once they are exactly the declared length with the declared digest, from the generation that resolved them. Reads run on the blocking-I/O executor; the store never reads synchronously or decodes.

## Decoded assets

`assets` owns the lifecycle of decoded forms; each asset family (textures, sounds, meshes) only decodes. Requests for one resource coalesce into one load, readiness is answered per requester, and a decoded form is held while anyone is interested and evicted in a deterministic order when a budget needs room. Handles carry a generation and fail with a typed error once what they named is gone. When the store publishes a new catalog, content is replaced in four phases, so a recooked texture or sound is drawn or heard at its new revision without a restart.

## Development and cooked play

In development a server or client reads a game from its source directory, finding each resource beside the description through its sidecar (`kest.game`). A cooked game is read from content alone (`content.root`, `content.manifest`), and an exported one from its library's active Composition (`content.library`). The same game runs the same way in all three.
