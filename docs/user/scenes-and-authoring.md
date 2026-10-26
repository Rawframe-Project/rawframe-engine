# Scenes and authoring

Everything a game is made of is a text document, and every change to one can be made from a terminal, a script or an agent through `rawframe-author`. Studio uses the same operations. How it works is in [authoring](../internals/authoring.md).

## Scenes

A scene (`*.scene`) is a document of entities: each with a stable id, a name and its components' field values, recorded against the layouts of the game's components.

```json
{
  "kind": "rawframe.scene",
  "formatVersion": 1,
  "schema": { "starter.ground": "680baa2755a44d15" },
  "entities": [
    { "id": "8a1c3f2e-6d4b-4e7a-9b15-2c7d0e5f9a31", "name": "ground",
      "components": { "starter.ground": { "side": 20 } } }
  ]
}
```

A game's `scene` lines name the scenes its World starts with; `prefab` lines name scenes a program spawns whole while it runs; `scatter` lines place copies of a prefab over an area. Scenes may hold instances of other scenes. An entity can be put on a layer (`rawframe.world.layer`), which an editor shows, hides and locks together. Each scene has a sidecar giving its identity.

You may edit a scene by hand. `rawframe-author` edits it for you, checked against the game, with undo.

## One request at a time

```sh
rawframe-author describe games/mygame/mygame.game          # every operation, and the game's components
rawframe-author apply games/mygame/mygame.game level.scene request.json [--dry-run]
rawframe-author read games/mygame/mygame.game level.scene queries.json
rawframe-author migrate games/mygame/mygame.game level.scene   # to the components' current layouts
```

A request is a batch of operations, applied together or not at all:

```json
{ "formatVersion": 1, "kind": "authoring.request", "batch": "atomic", "operations": [
  { "operation": "scene.create_entity", "entity": "6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30", "name": "crate", "place": 0 },
  { "operation": "scene.add_component", "entity": "6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30", "component": "<component type id>" },
  { "operation": "scene.set_field", "entity": "6a1f5c2e-0b7d-4e3a-9c41-5d2e8f7a1b30", "component": "<component type id>", "field": "width", "value": { "real": 2 } }
] }
```

`describe` lists every operation with its arguments, and the game's components by type id. Operations exist for scenes (create, rename, reparent and destroy entities; add and remove components; set fields; instances), the game description's lines, data documents (actions, mixers, strings), materials, UI documents and styles, terrains, tilemaps and more. Each outcome says the document's generation after it, whether it was written, and each operation's result or error. A request may name the generation it expects, so a change made by someone else in the meantime is refused rather than overwritten.

`migrate` brings scenes authored against older layouts of the game's components to the current ones, field by field. A scene that cannot be brought over is refused, naming what does not carry over, and left as it was.

## A session

`rawframe-author session <game> [<scene root>]` keeps the game open on standard input and output, one JSON record a line each way, with every scene's undo history:

```json
{"kind":"authoring.hello","id":1,"surfaceGeneration":1}
{"kind":"authoring.apply","id":2,"scene":"level.scene","request":{ ... }}
{"kind":"authoring.undo","id":3,"scene":"level.scene"}
{"kind":"authoring.end","id":4}
```

A session also cooks and imports with progress, lists and moves assets, keeps each scene's selection and view, and previews a scene in a running client. Every change is written to its file as it commits.

## From an agent

`rawframe-author mcp <game>` offers the same session as Model Context Protocol tools over standard input and output: each operation is a tool, with the session's undo, history, assets and cooking beside them. An agent configured with it can make and change a whole game without Studio:

```sh
claude mcp add rawframe -- rawframe-author mcp games/mygame/mygame.game
```

With `--join <endpoint> <pin file> <token file>` the agent joins a shared session instead, under its token's name and grants.

## New games

`rawframe-author new <template> <directory>` makes a game from a template, as Studio's start screen does. `rawframe-scene from-spawns <game> <scene>` turns a description's `spawn` lines into a scene.

## Working together

A session can be shared over QUIC:

```sh
rawframe-author host <game> --endpoint <host:port> --tokens <file> --fingerprint-file <file>
rawframe-author join <endpoint> <pin file> <token file>
```

The tokens file the host's owner writes gives each collaborator a name and what they may do (`read`, `edit`, `cook`, `play`, `claim`). Every collaborator sees each change as it commits, and undo takes back your own latest change, not someone else's. Removing a token from the file disconnects its holder at once. Studio joins a shared session too.
