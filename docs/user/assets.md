# Assets

Pictures, meshes, sounds, fonts and documents live as ordinary files beside the game description. Each has a sidecar, and the cook turns them into what a game reads. How it works is in [content](../internals/content.md).

## Sidecars

Every source file has a sidecar, `<file>.rfmeta`, naming the resource it becomes and the importer that makes it:

```json
{
  "schema": 1,
  "resourceId": "2cf4e6afa86830492ee14a9e8067986a",
  "importer": "rawframe.texture",
  "settings": { "exact": true }
}
```

The resource's identity stays the same when the file is renamed or moved, so nothing that names it breaks. A file that holds several things (the meshes and materials of a glTF, the clips of an animation) maps each to a resource of its own; `rawframe-cook --map <sources> [<source>]` gives every unmapped one a new identity and writes the sidecar. Never copy a sidecar to a new file without giving it a new identity: two sidecars claiming one resource are an error.

## Kinds

| Source | Importer | Becomes |
|---|---|---|
| PNG, JPEG, BMP, TGA, Radiance `.hdr` | `rawframe.texture` | A texture in linear light, compressed to BC7 or kept exact (`exact`), with its smaller levels |
| glTF (`.gltf`, `.glb`) | `rawframe.mesh` | Meshes, with skins; `rawframe-animation from-gltf` writes its animations as documents |
| WAVE, Ogg Vorbis, MP3 | `rawframe.audio` | A short-form clip or Opus, as the sidecar's `tier` says |
| TrueType, OpenType | `rawframe.font` | The font, rebuilt by the OpenType Sanitizer; WOFF is refused |
| `*.rfmaterial` | `rawframe.material` | A material ([materials](../internals/materials.md)) |
| `*.rfanim` | `rawframe.animation` | A skeleton, clip, graph or mask |
| `*.scene` | `rawframe.scene` | A scene |
| blockouts, terrains, tilemaps, scatters, sequences | their own importers | Meshes, tiles, placements and timelines |
| `*.strings`, `*.translations` | `rawframe.text` | String tables and translations |
| `kest.project` and its `.kest` sources | `rawframe.kest` | The compiled program |
| `*.game` | `rawframe.game` | The game, with everything it names |

A material that needs a shader of its own is compiled through the shader toolchain (see [development setup](../operations/development.md#toolchain)); without it, that material is refused and everything else cooks.

## Cooking

```sh
rawframe-cook <game directory> <output> [<cache>]
```

The cook reads every sidecar under the directory, runs each source's importer, and writes the artifacts, a `content.manifest` and a `cook.receipt` into the output, only when nothing failed. Each source is cooked twice and the results compared, so a cook that is not reproducible is caught. The cache keeps what earlier cooks made: an unchanged source, with every file it read unchanged, is not cooked again. A stopped cook publishes nothing.

You rarely run the cook yourself: `rawframe-export` cooks, and in development a server or client reads sources directly from the game directory. A recooked texture or sound reaches a running game at its new revision.

## Naming assets in a game

The game description names each asset by a 16-digit identity components use for it (`texture <id> <file>`, `mesh <id> <file>`, `sound <id> <file>`; see [the game description](game-description.md#assets)), and a component field holds that identity. A `field <component> <field> asset <kind>` line tells editors and agents which kind of asset the field holds.
