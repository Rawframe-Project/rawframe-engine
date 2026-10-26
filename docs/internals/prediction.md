# Prediction and lag compensation

A client sees its own player respond at once, and a player's shot hits what that player saw, while the server stays the only authority. Both live in `world_replication`, with the rewinding queries in the physics modules.

## Contents

- [Prediction](#prediction)
- [Effects](#effects)
- [Checksums](#checksums)
- [Lag compensation](#lag-compensation)
- [Traps](#traps)

## Prediction

A game lists the player components a client predicts (`predict`), and marks the systems that run on predicting clients too (`predicted`). A predicting client runs those systems over its own player alone, from its own input, ahead of the server: each step writes the input into the input component, then runs every predicted system in schedule order, numbered with the tick the server will run it at, so a system that reads the tick reads the same.

When the server's state for an input tick arrives, the client compares what it predicted with it, bit for bit. Equal, the prediction is confirmed. Unequal, the player goes back to the server's values and every later input is simulated again. There is no correction message: corrections are the ordinary state records, and the dedicated server predicts nothing.

The client keeps a bounded window of predicted ticks. At the cap it stops predicting further ahead until the server catches up; it never extrapolates. The server paces each client's input lead with a signal of its own, so a client is ahead by about its latency and no more.

Other entities are not predicted. `nearby` components of the entities a client mirrors are handed to the predictor, as last heard, before prediction starts and before every resimulation, so the player's steps meet them (a game with physics lists its body components); they change what a client predicts, never what it compares.

## Effects

A predicted system may emit presentation effects (`effect` lines, `emits` on the system): a jump's sound, a hit's spark. An effect's identity is its kind, the system that emitted it, its place among the system's emissions, and the input tick, so an exact resimulation emits the same identities. A `predicted` effect is delivered at once and once, however many times resimulation emits it, and cancelled once if a resimulation no longer does; a `confirmed_only` effect is delivered only when the server's state confirms its tick, and never taken back. The server keeps nothing of either.

## Checksums

A client sends a checksum of its player's whole predicted scope for a confirmed tick, and the server checksums the same scope at the same tick with a pinned algorithm. A mismatch is a real divergence, logged as `prediction_divergence` with no automatic response: it means the client and server simulated the same inputs differently, which is a determinism bug ([determinism.md](determinism.md)). A rollback rate alarm reports a client resimulating more than it should. [Debugging](../operations/debugging.md#prediction-divergence) covers the development captures and the divergence drill.

## Lag compensation

For a game whose input is `perceived`, each command carries the moment its client showed: the server tick of the older of the two states it was showing between, and how far towards the next. The server writes it into the player's `rawframe.replication.perception` component with each command it applies. It clamps each claim to within a skew of the lag it measures for that connection, so a client cannot claim to have seen further back than it plausibly did.

A lag-compensated query (`physics2d.castRayAt` and its 3D sibling) rewinds bodies to that moment. It rewinds only what that connection was actually sent at the time: the server keeps, for 1,024 ticks, when each connection was first sent each entity, and an entity the connection had not been sent then is not hit.

## Traps

- A predicted system may write no replicated component that is not predicted, and may draw from no World random stream: the client has neither.
- A predicted system must give bit-identical results on client and server. Anything it reads that differs (wall time, presentation state, a component the client does not have) makes every tick a misprediction.
- Bodies other than the player are not predicted. A player whose steps move dynamic bodies mispredicts every such step and is corrected by the server, so a game that wants exact prediction keeps the player from moving them.
