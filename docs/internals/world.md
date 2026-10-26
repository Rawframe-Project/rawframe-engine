# The World

A World is one authoritative ECS domain: entities, their components, and the systems that change them, ticked at a fixed rate. The modules are `schema`, `world`, `world_runtime`, `world_snapshot` and `world_save`.

## Contents

- [Components and the registry](#components-and-the-registry)
- [Entities](#entities)
- [Storage and queries](#storage-and-queries)
- [Systems, the schedule and barriers](#systems-the-schedule-and-barriers)
- [Ticks](#ticks)
- [Checkpoints](#checkpoints)
- [Saves](#saves)

## Components and the registry

A component type has a stable 128-bit identity (a game's `component` line gives it as a UUID) and a layout. The registry is built once from every type a World will hold, validated, frozen and shared; each World binds one explicitly, and there is no global registry. Within a frozen registry each type gets a dense runtime index, assigned in ascending order of stable identity, so the same types always give the same indices. A runtime index is never durable: it means nothing outside its registry.

Components are plain data. A component may hold an entity, which a checkpoint and a save write as a reference rather than as numbers; a game says which fields do with `entity` lines.

## Entities

An `EntityHandle` is a 32-bit slot and a 32-bit generation, valid only in the World that issued it. Generation 0 is never live, so the default handle is null. A destroyed entity's handle is stale at once, and so is every copy. Handles are never persisted, replicated or handed to Kest as numbers: replication maps entities to its own `NetEntityId`, checkpoints to their own entity ids, and saves to persistent identities.

An entity that must be found again across saves opts into a persistent identity, `rawframe.world.persistent`, as ordinary component data. An entity a scene brings is named from the scene's identity and its own id there, so the same level spawned again gives the same identities; an entity made while the World runs draws one from the World's random stream for it.

## Storage and queries

Components are stored by archetype: entities with the same set of component types share columns. A query names terms (`read`, `write`, `with`, `without`) and visits every entity whose archetype has all the required ones and none of the excluded. Iteration is in archetype creation order, then row order, both of which follow from the World's history alone, so the same history always iterates the same way. Matching archetypes are cached; only archetypes made since a query's last run are examined.

## Systems, the schedule and barriers

A system declares the components it reads and writes, the systems of its phase it runs before or after, and the random streams it draws from. The schedule compiles those declarations into an order; two systems that conflict on a component never run as if unordered. The World's phases are fixed and closed:

`begin_tick`, `apply_inputs`, `pre_simulation`, `simulation`, `post_simulation`, `commit`, `replication`, `end_tick`.

While systems run, the World's structure is locked. Creating and destroying entities, and inserting and removing components, are recorded into the system's command buffer and applied at a barrier. Recording never allocates: a buffer's capacity is reserved up front, and recording past it fails with `resource_exhausted`. A component value may name an entity created earlier in the same buffer; the barrier replaces it with the real handle. A system that returns an error has the commands it recorded that tick discarded and the error reported; the other systems still run.

Kest systems never write the World directly: they stage changes through the same buffers ([kest.md](kest.md)).

## Ticks

A tick is a 64-bit count, never derived from accumulated seconds and never skipped. The tick rate is an exact fraction, ticks per seconds. Ticks owed accumulate from monotonic time; at most a set number run in one Host iteration, and the rest is debt, measured and never folded into a larger step. Time a World stands still on purpose (paused, or held at a debugger's breakpoint) is not owed. See [runtime.md](runtime.md#the-host) for what an overloaded World does.

`world_runtime` holds the running World for the other participants: the simulation, its players, picking, the profile of its last ticks, and the replacement of the World between ticks after a restore. A participant that keeps entity handles watches the World's replacement count and lets them go when it changes.

## Checkpoints

A checkpoint is a complete, canonical, integrity-checked artifact of one committed World: a header, the entity directory, component rows by type in row groups, the World's random streams, the mods it ran with, a manifest, and a completion footer, with SHA-256 over every chunk and the whole. What it holds is the World's projection: the components that persist and their fields, each written field by field in a declared order, never as memory, so padding and layout never reach an artifact.

Restore reads a checkpoint into an empty candidate World built off to the side and publishes it in one exchange between ticks; it cannot fail once published. A checkpoint of another game, schema or profile, one past its limits, or one with a reference to nothing it holds is refused with a typed error, and the running World is untouched.

## Saves

A save is the game's curated record, not the World's: a game declares a save document as the components it keeps of its persistent entities (`save` lines), and a player's save as what each player keeps. Capture reads exactly that projection into canonical bytes. Reading checks every byte before anything happens; applying stages the result into the running World through one command buffer at a barrier, completely or not at all. A save written under an older declaration of the same document is migrated as it is read, by field name, without game code.

Where saves are kept is a store's business: a store keeps bytes under a slot name, never truncates, and never destroys the save before it until the new one is whole. Stores block, so they run on a blocking-I/O worker or the Host thread. The operator chooses the directory, the slot and how often (`save.*`).
