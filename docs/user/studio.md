# Studio

Studio is an editor over an authoring session. It is optional: everything it does is a session record or a tooling request that `rawframe-author` and an agent can send too, and no part of a game exists only in Studio. Use it when you want to see and arrange a game; use the headless tools when you want to script or automate it.

```sh
rawframe-studio [--config <file>] [<game description>]
```

With no game it opens on its start screen: a new game from a template, the sample games, and the games opened last. `tools/package_studio.sh` packages Studio with everything it runs for someone without a build ([platforms](../operations/platforms.md#studio-packages)).

## What it holds

- **The scene:** the Hierarchy of entities as a tree, which you reparent by dragging; the Inspector of the chosen entities' components, with fields typed by the game's `field` hints and asset pickers; a viewport you fly through with the right mouse button held and W A S D Q E, with grid, sky and overlays; layers to hide, lock and choose together.
- **Documents:** editors for materials as graphs, animation clips on a timeline and skeletons as bone trees, action sets, mixers, string tables and translations, UI documents and styles, blockouts, terrains, tilemaps, scatters and sequences, and the game description's own lines.
- **Assets:** the game's sources by folder and kind, with their importers, import settings, cook state and thumbnails; files moved, renamed and dropped in.
- **Playing:** Play runs the game's dedicated server and one or more clients, in the viewport or in windows of their own, with network conditions, device emulation, pause and step. Edits to a scene reach the playing game as you make them. The Profiler, Network and Live World panels read the running game.
- **Around it:** a command palette and menus, undo and redo with the History, the Console, version control through git in the Changes tab, Export over `rawframe-export`, and joining a shared session.

Commands are in the menus and the command palette, with shortcuts you can change; panels can be docked or moved to windows of their own.

## Extensions

Studio extensions are Kest programs (`rawframe.studio`) that add commands. The session runs them untrusted, with only the doors marked safe for untrusted code.

## Settings

Studio keeps its preferences (panels, shortcuts, recent games, interface scale) in a preferences file of its own (`studio.preferences`), never in the game. A game's own settings are the game description's lines, which Studio edits as any other document.
