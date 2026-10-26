# Gameplay in Kest

A game's behavior is Kest: component types are Kest structs, and systems are Kest functions the World calls each tick. This page shows how they meet the engine. [Kest's own documentation](https://github.com/Rawframe-Project/kest) covers the language.

## Components

A component is a plain Kest struct, named by a `component` line with a durable identity:

```kest
struct Mover {
    x: f64
    z: f64
}
```

```
component 0ddf8849-b458-4e59-a943-cf645990ec84 starter.mover Mover
```

The engine's own components (physics bodies, models, sprites, cameras, sounds, UI nodes) have Kest types in its library modules: `rawframe.physics2d`, `rawframe.physics3d`, `rawframe.model`, `rawframe.canvas`, `rawframe.sound`, `rawframe.ui` and the others. A game names them with `component` lines of its own, under its own names, as the starter does with `component ... starter.lamp rawframe.model.PointLight`.

## Systems

A system takes the number of matched entities and one array per column, in the order its `system` line lists them:

```kest
fn move(count: i32, sticks: [controls.Stick], movers: [Mover]) {
    let i = 0
    while i < count {
        movers[i].x = movers[i].x + f64(sticks[i].x) * SPEED
        i = i + 1
    }
}
```

```
system starter.move simulation move read starter.stick write starter.mover
```

It runs once for each group of entities that hold the same components. A `read` column is the World's value; a `write` column is a copy that replaces the World's only when the system succeeds that tick, so a system that fails or runs out of fuel changes nothing. Systems order themselves with `before` and `after`; gameplay that moves physics bodies runs `before rawframe.physics2d.step` (or `physics3d`), and gameplay that reads where they went runs after.

## Doors

A door is an engine function a program calls through an `extern fn` its library module declares:

| What | Doors |
|---|---|
| Entities | `world.create()`, `world.destroy(entity)`, `world.tick()`; for each component type, `<Type>.insert(entity, value)` and `<Type>.remove(entity)`. |
| Other entities | With `lookup <component>` on the system's line: `<Type>.get(entity)`, `<Type>.has(entity)`, `<Type>.count()` and `<Type>.entity(index)`. |
| Randomness | With `random <stream>` on the line: `random.below(stream, bound)` and `random.unit(stream)`, the stream by its place in the line's list. |
| Prefabs | `scene.spawn(identity)` spawns a `prefab` line's scene whole. |
| Players | `replication.player()`: on a client, its own player. |
| Messages | `Messages.<name>(to, value)` on the server; `ReceivedCount.<name>()` and `Received.<name>(index)` in a client's present systems. |
| Commands | `Commands.<name>(value)` in a client's sample function; `CommandCount.<name>()`, `Command.<name>(index)` and `CommandFrom.<name>(index)` in the server's systems on the next tick. |
| Effects | `Effects.<name>(entity)` from a predicted system with `emits <name>`. |
| Physics | Rays, sweeps and overlaps in `rawframe.physics2d` and `rawframe.physics3d`, and `castRayAt` cast back to the moment a player saw. |
| Navigation | `navigation.next(fromX, fromY, fromZ, toX, toY, toZ)` asks the next step of the way. |

Creating, destroying, inserting and removing take effect at the next barrier, after the system ends: a system never sees its own structural changes in the same run.

## Randomness

Never use a clock or the platform for anything that decides gameplay. Draw from a World random stream the system declares: the same seed and inputs give the same draws on every machine, so a server, a checkpoint restore and a predicting client agree. A predicted system may not draw from a World stream.

## Limits

Each system has a fuel budget per tick (`kest.fuel_per_system`) and the program a heap (`kest.heap_bytes`). A system that exhausts its fuel is stopped for the tick and changes nothing; the server logs it and goes on.

## On clients

- **Present systems** (`present` lines) run on a client that draws, over its mirror of the server's World, and write only presentation components: where a sprite or model is drawn, what a camera looks at, which sounds an emitter plays. They never reach the server.
- **The sample function** (`sample` line) turns the player's actions into their input component each tick, and may send commands. It reads actions through `rawframe.input` (`input.x(action)`, `input.y(action)` and the others), by the action's identity in the action set.

## Changing code while it runs

A server with `kest.reload_every` reloads the program when its sources change, between ticks, keeping the World. A program whose component types changed shape is refused, and the old one keeps running.
