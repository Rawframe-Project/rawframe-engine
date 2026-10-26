# Glossary

The words the code and these pages use, with one meaning each.

## Contents

- [Processes](#processes)
- [The World and Kest](#the-world-and-kest)
- [Networking](#networking)
- [Content and packaging](#content-and-packaging)
- [Authoring](#authoring)
- [Presentation](#presentation)

## Processes

- **Host:** the object a program's entry runs; it owns the clock, executors, configuration and files, and iterates the phases.
- **Composition:** the participants one program runs, chosen by a plan before anything starts.
- **Participant:** a unit a module provides to a composition, with its capabilities, roles, lifetime scope, executors and stop budget.
- **Capability:** a named thing a participant provides or needs, or a host lends; a plan picks one provider of each.
- **Role:** what a composition is built for: dedicated server, client, tool or test.
- **Host phase:** one of the fixed steps of a Host iteration, from `platform_poll` to `frame_end`.
- **Configuration:** a run's immutable `key = value` settings, read from `--config`.

## The World and Kest

- **World:** one authoritative ECS domain of entities and components, ticked at a fixed rate.
- **Tick:** one step of a World's simulation; a 64-bit count, never derived from seconds.
- **Registry:** the frozen set of component types a World binds.
- **Archetype:** the set of component types some entities share, and their storage.
- **System:** behavior over World data, declared with what it reads and writes and run in a World phase.
- **Barrier:** where command buffers apply their structural changes, between systems.
- **Command buffer:** a system's recorded creates, destroys, inserts and removes.
- **Game description:** the `.game` file that declares a game's components, systems and setup in lines the engine parses and never executes.
- **Machine:** a Kest program running with its own heap, stack and fuel.
- **Fuel:** the steps a Kest call may take before it is stopped.
- **Door:** an engine function a Kest program may call, marked safe for untrusted code or not.
- **Present system:** a client's Kest system over its mirror, writing presentation components.
- **Sample function:** a client's Kest function that turns committed actions into the player's input and commands.
- **Checkpoint:** a complete, canonical artifact of one committed World, which restores into a World that carries on as the original would.
- **Save:** a game's curated record of its persistent entities' declared components.
- **Persistent identity:** an entity's opt-in 128-bit identity, found again across saves.

## Networking

- **Provider:** a transport implementation (QUIC, WebTransport, loopback) behind the provider-neutral contract.
- **Session:** an admitted peer's connection with its control stream and lanes.
- **Admission:** the hello, compatibility check and admission rule before any gameplay.
- **Lane:** a declared one-way reliable stream for guaranteed events: messages, commands, terminations.
- **Message:** a value a server system sends one player, delivered after the tick commits.
- **Command:** a value a player's client sends the server, read on the next tick.
- **Mirror:** a client's World holding what the server replicated.
- **Interest:** which entities a connection is sent.
- **Prediction:** a client running its player's predicted systems ahead of the server and comparing.
- **Confirmed tick:** an input tick whose predicted state equals the server's.
- **Effect:** a presentation event a predicted system emits, delivered once.
- **Perception:** the moment a player saw when giving a command, for lag-compensated queries.
- **Tooling endpoint:** a running program's own protocol for inspecting, previewing and debugging it.

## Content and packaging

- **Resource:** one logical piece of content, named by a 128-bit identity stable across renames and revisions.
- **Representation:** how a resource's bytes are read, as a dotted identifier.
- **Sidecar:** the `.rfmeta` file beside a source naming its resource, importer and settings.
- **Importer:** what turns one kind of source into cooked artifacts.
- **Cook:** running every source's importer into artifacts, a manifest and a receipt.
- **Receipt:** the cook's proof of what it read and made, the only proof a Build accepts.
- **Build:** cooked content chunked, compressed and signed, named by its root hash.
- **Library:** the Builds one machine holds, in one content-addressed store.
- **Composition (content):** the exact Builds a process plays, named by the hash of its record. Context tells it from a program's composition.
- **Release:** a signed, immutable record binding a Composition to its artifacts' digests.
- **Channel:** a named pointer (`stable`, `beta`, `nightly`) to a subject's current Release.
- **Origin:** where an update fetches from: a mirror directory or an HTTP or HTTPS server, never trusted.

## Authoring

- **Scene:** a document of authored entities a World starts with or spawns.
- **Prefab:** a scene a program spawns whole while it runs.
- **District:** a scene's entities that stream into a connection's interest together.
- **`SourceEntityId`:** an authored entity's identity in its scene, stable across edits.
- **Layout mark:** the record of a component's layout a scene was authored against.
- **Operation:** one of the closed, typed verbs that change an authored document.
- **Delta:** one slot of a document before and after, the unit an operation stages.
- **Transaction:** deltas staged against one document generation, applied whole or refused.
- **Authoring session:** a game held open with its documents and histories, speaking records.

## Presentation

- **The device:** the process's one GPU device, owned by `render`.
- **View:** what one local player, or one eye, sees: a region and its cameras.
- **Render texture:** a picture a camera's view is drawn into for a material to read.
- **Shader container:** a shader compiled offline for every driver family, committed.
- **Blob material:** a material folded into parameters the scene's shaders read.
- **Program material:** a material with its own shader program, compiled by the cook.
