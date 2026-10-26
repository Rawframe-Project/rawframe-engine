# Multiplayer

Every Rawframe game is played through an authoritative server, also when one person plays it alone: the same game runs embedded, on loopback or on a dedicated server with the same meaning. This page covers what a game declares to be played over a network. How it works is in [networking](../internals/networking.md) and [prediction](../internals/prediction.md).

## Players and input

```
replicate starter.mover starter.ground
player starter.stick starter.mover
input starter.stick
actions starter.actions
sample sample.kest sample
```

Each connected player gets an entity with the `player` components. The client's sample function fills the `input` component from the player's actions each tick; the server writes it into the player's entity, and the game's systems read it like any other component. The server decides everything; a client only sends its input and commands.

Only `replicate` components reach clients. A client mirrors them in its own World and draws that mirror; what it draws (models, sprites, cameras) are presentation components its present systems write, which never reach the server.

## Responding at once: prediction

Without prediction a player sees their own actions a round trip late. To have a client respond at once:

```
predict starter.mover
system starter.move simulation move read starter.stick write starter.mover predicted
```

A predicting client runs the `predicted` systems over its own player, from its own input, ahead of the server, and compares with the server's state when it arrives. When they differ the player goes back to the server's values and the later inputs are simulated again. For that to be rare:

- a predicted system must give exactly the server's result: no clock, no presentation state, no World random stream;
- what the player meets of other entities is listed with `nearby` (a physics game lists its body components);
- the player should not move dynamic bodies, which are not predicted.

Effects that should be felt at once, such as a jump's sound, are `effect <name> predicted` lines emitted by a predicted system; `confirmed_only` waits for the server.

## Others, smoothly

`interpolate` components of other entities are shown between received states, slightly in the past, rather than jumping as each arrives. Physics games usually interpolate their body poses.

## Shooting what you saw

With `input <component> perceived`, each command records the moment its player saw, and `castRayAt` in `rawframe.physics2d` or `rawframe.physics3d` casts back to it: a shot hits what the shooter saw, only among what the shooter's client had been sent.

## Sending less

- `interest <component> <x> <y> within <meters>` sends each connection only what lies near its player.
- `own <component>` sends a component only to the player who holds it.
- `mapped <count>` raises how many entities one connection may hold, for a game whose every client sees a large World.

## Messages and commands

- A **message** (`message <name> <Kest type>`) is a guaranteed value a server system sends one player with `Messages.<name>(to, value)`, delivered after the tick commits.
- A **command** (`command <name> <Kest type>`) is a value a player's client sends the server with `Commands.<name>(value)`, which the server's systems read on the next tick, with its sender. Commands are untrusted and checked: at most 1,024 bytes, eight sent a tick, and one of the wrong kind or size strikes the connection.

Neither may hold an entity: an entity names something in one World only.

## Admission

An `admission <function>` line asks a Kest function of the program about each client the server would admit; a game without one admits every client the engine does.

## Testing with bots

`rawframe-arena` plays a server and headless bots in one process over a simulated network: `network.loopback.latency_ms`, `.jitter_ms`, `.loss_ppm`, `.duplicate_ppm` and `.seed` add one-way latency, jitter, loss and duplication, seeded so a run delivers the same way again (a QUIC link takes the same settings under `network.quic`); `rawframe-bots` plays bots against a dedicated server over QUIC. Both log what the server and each bot saw, which is how the engine's own tests check a game.
