# Getting started

Build the engine, play a sample game, and make a game of your own from the starter template. Everything here works from a terminal; Studio is optional.

## Build

On Linux with the packages in [development setup](../operations/development.md#toolchain):

```sh
git clone https://github.com/Rawframe-Project/rawframe-engine.git
cd rawframe-engine
cmake --preset clang-development
cmake --build out/clang-development
```

The programs are under `out/clang-development/hosts/` and `out/clang-development/launcher/`. Windows, macOS, the web and the phones are covered in [platforms](../operations/platforms.md).

## Play a sample game

`rawframe-export` makes a game into a folder that plays on its own: its server, its client and a launcher.

```sh
tree=out/clang-development
"$tree/hosts/export/rawframe-export" games/runners /tmp/runners \
  --cook "$tree/hosts/cook/rawframe-cook" --build "$tree/hosts/build/rawframe-build" \
  --server "$tree/hosts/dedicated_server/rawframe-server" \
  --client "$tree/hosts/client/rawframe-client" --play "$tree/launcher/play/rawframe-play"
/tmp/runners/runners
```

The launcher starts the game's dedicated server on this machine, then a client pinned to it. Runners is a 2D platformer, played with the keyboard or a gamepad. The other samples under `games/` export the same way: `plaza` (3D), `tycoon` (Stalls, a small tycoon), `gale` (ships on a sea), `quarry`, `lighting`.

## Make a game

Make a new game from the starter template:

```sh
"$tree/hosts/author/rawframe-author" new templates/starter-3d games/mygame
```

It copies the template, names the game description and the Kest project after the folder, and makes every identity the template mints fresh, so two games made from one template never share one. The new folder holds:

| File | What it is |
|---|---|
| `mygame.game` | The [game description](game-description.md): components, systems, what replicates, what the World starts with. |
| `starter.kest`, `controls.kest` | The game's Kest program: its component types and systems. |
| `sample.kest` | The client's sample function: the player's input from their actions. |
| `starter.actions` | The [action set](input.md): what the player can do and the default bindings. |
| `level.scene` | The [scene](scenes-and-authoring.md) the World starts with. |
| `kest.project` | The Kest project the program is compiled from. |
| `*.rfmeta` | Each source's [sidecar](assets.md): the resource it becomes and the importer that makes it. |

Each player gets a box they move about the ground; the server moves it. Export and play it as above with `games/mygame`.

## Change it

Open `starter.kest` and change `SPEED`, or what `move` does. To see a change without exporting again, run the game in the arena, a server and headless bots in one process, and read what its systems did in the log:

```sh
cat > /tmp/mygame.conf <<EOF
host.maximum_iterations = 600
world.tick_rate = 60
kest.game = $PWD/games/mygame/mygame.game
replication.endpoint = arena
bots.count = 4
bots.endpoint = arena
EOF
"$tree/hosts/arena/rawframe-arena" --config /tmp/mygame.conf
```

A server given `kest.reload_every` watches the game's sources and reloads the program between ticks when they change, keeping the World.

## Next

- [Gameplay in Kest](gameplay-in-kest.md): systems, doors, randomness, commands and messages.
- [Multiplayer](multiplayer.md): what replicates, prediction and interest.
- [Scenes and authoring](scenes-and-authoring.md): editing scenes with `rawframe-author`, by hand or from an agent.
- [Exporting](exporting.md): signed folders, the web, the phones and updates.
- [Studio](studio.md): the editor, if you want one.
