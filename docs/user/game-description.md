# The game description

A game is a Kest program and a game description, `<name>.game`: lines the engine parses and never executes, saying what Kest cannot say yet. Paths are relative to the description. A `#` starts a comment that runs to the end of its line. The parser's own documentation, with every rule, is in `modules/world_kest/include/rawframe/world_kest/game.h`.

## Contents

- [A small game](#a-small-game)
- [Program and components](#program-and-components)
- [Systems](#systems)
- [What the World starts with](#what-the-world-starts-with)
- [Networked play](#networked-play)
- [Physics and navigation](#physics-and-navigation)
- [Input](#input)
- [Presentation](#presentation)
- [Assets](#assets)
- [Text and UI](#text-and-ui)
- [Saves, admission and mods](#saves-admission-and-mods)

## A small game

```
program movers.kest
component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 game.position Position
component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 game.velocity Velocity
system game.integrate simulation integrate write game.position read game.velocity
spawn 3 game.position x=0 y=0 game.velocity dx=1 dy=2
```

## Program and components

| Line | Meaning |
|---|---|
| `program <file.kest>` | The game's Kest program, compiled through the `kest.project` beside it. |
| `component <uuid> <name> <Kest type>` | A component: its durable identity, its name, and the Kest type that lays it out. Every component may be inserted and removed by the program. |
| `entity <component> <field>` | A field that holds a `rawframe.world.Entity`, written by checkpoints and saves as a reference. |
| `field <component> <field> ...` | Hints for editors and agents: `range <least> <most>`, `unit <unit>`, `color`, `labels ...`, `asset <kind>`. Hints only: any value of the field's type stays valid. |

## Systems

```
system <identity> <phase> <function> <columns and options>...
```

The phase is one of the World's (`simulation` for most gameplay). Columns follow in the function's order: `read`, `write`, `with` or `without` before a component, or `entities` for the matched entities. Options: `after` or `before` another system of the phase, `random <stream>` for each random stream it draws from, `lookup <component>` to read that component on any entity, `scan <component>` to read every holder at once, `predicted` to run on predicting clients too, and `emits <effect>`.

## What the World starts with

| Line | Meaning |
|---|---|
| `spawn <count> <component> <field>=<value>... ...` | Entities with the listed components; a field not given is zero. |
| `scene <file>` | A scene document whose entities the World starts with. |
| `prefab <16 hex digits> <file>` | A scene the program may spawn whole while it runs, with `scene.spawn(<identity>)`. |
| `scatter <file>` | Copies of a prefab placed over an area. |

## Networked play

| Line | Meaning |
|---|---|
| `replicate <component>...` | The components the server sends clients. |
| `player <component>...` | The components each connected player's entity starts with. |
| `input <component> [perceived]` | The player component a player's input is written into; `perceived` also records the moment the player saw, for lag-compensated queries. |
| `predict <component>...` | The player's components a client predicts from its own input. |
| `nearby <component>...` | Other entities' components a predicting client puts beside its player. |
| `interpolate <component>...` | Components shown between received states on every entity but the player's. |
| `own <component>...` | Replicated components sent only to the connection whose player holds them. |
| `interest <component> <x> [<y> [<z>]] within <meters>` | Sends each connection only what lies within the radius of its player. |
| `mapped <count>` | Entities one connection may hold at once, past the default 4,096. |
| `message <name> <Kest type>` | A guaranteed message a server system sends one player. |
| `command <name> <Kest type>` | A value a player's client sends the server, at most 1,024 bytes. |
| `effect <name> predicted\|confirmed_only [sound <id>] [felt <output> <amplitude> <hertz> <ms>]` | A presentation effect a predicted system emits. |

See [multiplayer](multiplayer.md).

## Physics and navigation

| Line | Meaning |
|---|---|
| `physics2d gravity <x> <y> [substeps <n>]` | 2D physics: the engine's `rawframe.physics2d` components and its step. |
| `physics3d gravity <x> <y> <z>` | 3D physics, y up. A game has one physics line at most. |
| `collision class <name> <16 hex digits>` | A collision class. |
| `collision rule <class> <class> collide\|trigger\|ignore` | The rule between two classes. |
| `collision default collide\|trigger\|ignore` | The rule for every pair not listed. |
| `navigation [radius <m>] [height <m>] [step <m>] [slope <degrees>] [cell <m>]` | A navmesh for one agent, baked on the server from the static bodies. Needs `physics3d`. |

## Input

| Line | Meaning |
|---|---|
| `actions <file>` | The game's action set ([input](input.md)). |
| `sample <program> <function>` | The client's sample function, which fills the input component from committed actions. Needs `actions` and `input`. |

## Presentation

| Line | Meaning |
|---|---|
| `presentation <component>... on <component>\|player` | Components a drawing client attaches to each entity it mirrors that has the `on` component (or to its own player), written only by present systems. |
| `present <identity> <function> <columns>...` | A present system, run by a drawing client over its mirror once a game tick. |
| `layout <players> (<x> <y> <width> <height>)...` | Split-screen regions for that many local players, as fractions of the window. |
| `aspect <width> <height> [<red> <green> <blue>]` | Every view kept to that shape, between bars of the color. |
| `rendertexture <16 hex digits> <width> <height> [every_frame\|on_demand]` | A picture a camera's view is drawn into, which a material reads. |
| `animator <16 hex digits> <graph> [parameters <component>] [subset <mask>]` | An animation graph entities play ([animation](animation.md)). |

## Assets

Each names a source beside the description by the identity components use for it; the cook makes the resource.

| Line | Source |
|---|---|
| `texture <16 hex digits> <file>` | An image a sprite or material shows. |
| `mesh <16 hex digits> <file>` | A glTF, a blockout or a terrain, for a model or a physics body. |
| `material <16 hex digits> <file>` | A material document. |
| `tilemap <16 hex digits> <file>` | A tilemap document. |
| `sequence <16 hex digits> <file>` | A timeline a sequence player plays. |
| `font <16 hex digits> <file>` | A TrueType or OpenType font for UI text. |
| `mixer <file>` | The mixer layout ([audio](audio.md)). |
| `sound <16 hex digits> <file>` | A sound declaration. Needs `mixer`. |

## Text and UI

| Line | Meaning |
|---|---|
| `text <file>` | A string table or its translations ([localization](localization.md)). |
| `locale <tag>` | The project's default locale. |
| `label <16 hex digits> <table> <key> [<argument>]` | Words a UI node shows, by key. |
| `ui <file>` | The UI document: which nodes nest in which, how they start and what they show ([UI](ui.md)). |
| `styles <file>` | The UI's style classes. |

## Saves, admission and mods

| Line | Meaning |
|---|---|
| `save <document> <component>...` | The game's save: what it keeps of every persistent entity. |
| `save player <document> <component>...` | What each player keeps of their entity. |
| `admission <function>` | The program's admission rule, asked about each client the server would admit. Needs `replicate`. |
| `mods closed\|curated\|open` | Who may mod the game ([mods](mods.md)); closed without one. |
| `modapi <namespace> <version>` | The game's Mod API namespace and version. |
| `approve <publisher/name>` | A mod a curated game approves. |
| `extension <name> data\|event\|service\|replacement ...` | An extension point. |
| `prefer <point> <publisher/name>...` | Which mod keeps an exclusive point when several claim it. |
