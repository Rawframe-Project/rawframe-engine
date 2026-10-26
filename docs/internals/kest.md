# Kest in the engine

Kest is the one language for gameplay and tooling. A game is a Kest program plus a game description that says, in lines the engine parses and never executes, what Kest cannot say yet: component identities, which functions are systems, what replicates, and what the World starts with ([../user/game-description.md](../user/game-description.md)). The modules are `kest` (the machine and its doors), `kest_library` (the engine's own Kest modules), `world_kest` (games, systems and mods) and `input_kest` (a client's sample function).

## Contents

- [Machines](#machines)
- [Systems](#systems)
- [Doors](#doors)
- [Staging](#staging)
- [Clients](#clients)
- [Mods](#mods)
- [Reload](#reload)

## Machines

A program runs on a machine with its own stack and heap. A machine is thread-affine: one thread calls it at a time, and only `cancel` may come from another. Every machine has finite limits: its heap and its fuel are required, and its stack and call depth default to the least the program provably needs, or the start is refused when Kest cannot prove one.

Fuel is the number of steps a call may take. A system has one budget per tick, spent across every archetype it runs over (`kest.fuel_per_system`, `kest.heap_bytes`); a system that runs out is stopped and changes nothing that tick. A system that never ends therefore costs its fuel, not the server.

Who wrote the code decides what it may do. A game's own program is trusted. A mod's runs on a machine of its own under Kest's untrusted profile: only what the verifier proved runs, within its heap and fuel ceilings, and only doors marked safe for untrusted code bind.

## Systems

A Kest system is a function taking the row count and one array per data column:

```kest
fn integrate(count: i32, positions: [Position], velocities: [Velocity])
```

It runs once per matching archetype. Its game description line gives its identity, phase, the function, its columns in the function's order (`read`, `write`, `with`, `without`, or `entities` for the archetype's entities), its order against other systems of the phase, the random streams it draws from, the components it looks up on any entity (`lookup`) and those whose every holder it reads at once (`scan`). The engine checks each column's Kest type against the component's size and alignment when the registry is frozen, so a mismatched program fails before the World starts.

Read columns are lent in place. Write columns are lent as a journal copy that replaces the World's values only when every call of the system that tick succeeded: a refused, exhausted or failing system changes nothing. Per tick, the bytes all systems may journal and the commands they may record are bounded; a system that would pass either is refused for the tick.

Systems of one machine never run at the same time.

## Doors

A door is an engine function a program may call. Every door says whether untrusted code may call it; asking for one that is not marked so is refused when the program binds. The engine's own Kest modules under `modules/kest_library/kest/rawframe/` declare the types and doors a game sees: `world` (entities, create and destroy), `random`, `scene` (spawning prefabs), `physics2d`, `physics3d`, `navigation`, `animation`, `replication`, `admission`, `input`, `canvas`, `model`, `view`, `ui`, `sound`, `sequence`, `spline` and `studio`.

Each component a game declares also gets doors of its Kest type: `insert` and `remove`, and for a system that declares it, `get`, `has`, `count` and `entity` on any entity. A lookup is ordered as a read and is refused for a component the system writes.

A program asking for a door its machine does not offer is refused naming the door. Every machine that runs the game's program offers the same doors, empty where the program does not play authoritatively, so one program serves the server, its clients and its tools.

## Staging

Scripts never mutate the World directly. Structural changes (`World.create`, `World.destroy`, a component's `insert` and `remove`, `Scene.spawn`) go into the running system's command buffer and apply at the next barrier. What other doors stage beside them (messages, effects, sounds) is kept only when the run that staged it succeeds, under the same rule as the system's commands and writes.

## Clients

A client that draws runs two more kinds of Kest code over its mirror of the server's World:

- **Present systems** (`present` lines) write presentation components a client attaches to the entities it mirrors (`presentation` lines): looks, cameras, emitters. They are never replicated, predicted, or on a server.
- **The sample function** (`sample` line) reads committed input actions and fills the player's input component, and sends commands. A server never opens it.

A system marked `predicted` runs on predicting clients too, over the player alone ([prediction.md](prediction.md)).

## Mods

A mod targets one game and the Mod API versions it was written against, and contributes to the game's extension points: `data` points take scenes whose entities hold the point's component alone, read, checked and migrated as a game's own scenes are; `event` points take handlers, `service` points take providers, and a game may let a mod replace one of its systems. A mod with code names one program, run on its own machine under the untrusted profile with its own heap and fuel (`kest.mod_heap_bytes`, `kest.mod_fuel_per_handler`). The game's policy says who may mod it, and checkpoints record the mods a World ran with.

## Reload

A server watching its game's sources (`kest.reload_every`) reloads the program between ticks: a new machine is started, every entry is found and checked, and every Kest type the World holds must keep its shape before anything changes. The World, its systems and their schedule stay. On refusal the old program keeps running, and the server logs why. Scenes edited while the game runs reach the World as the difference between what they authored before and what they author now.
