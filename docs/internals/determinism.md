# Determinism

A World's simulation gives the same result for the same inputs on every platform and compiler. Prediction compares a client's state with the server's by checksum, checkpoints restore a World that carries on as the original would, and replays and bots rely on the same thing. This page lists what holds it together; breaking any of it shows up as divergence, not as a crash.

## What determinism rests on

- **Fixed ticks.** Simulation advances by whole ticks at an exact rational rate, never by a measured frame time, and no tick is ever skipped or merged into a larger step ([world.md](world.md#ticks)).
- **Ordered iteration.** Queries visit archetypes in creation order and rows in row order, both of which follow from the World's history alone. Systems run in the order the schedule compiles from their declarations, and structural changes apply at barriers in recorded order. Code must never let a hash container's iteration order reach a result.
- **Floating point without surprises.** Every target builds with floating-point contraction off (`-ffp-contract=off`, and its MSVC and clang-cl spellings), so no compiler fuses a multiply and an add where another would not. The engine is built without fast-math.
- **Seeded randomness.** Simulation randomness never comes from the operating system. A World has one root seed, chosen once by the authority, replicated as plain data and recorded in checkpoints. Every stream derives from it by identity: a system's identity and the stream's name, hashed with FNV-1a and mixed with SplitMix64, seed a PCG32 (XSH-RR) generator. The algorithms together are one versioned identity, pinned by golden vectors in `modules/world/tests/random_test.cpp`; changing any of them is a new identity, never a silent swap. A system declares the streams it draws from, and draws only from those.
- **Physics.** Maul2D and Maul3D are deterministic for the same inputs and step on the World's tick ([physics-and-navigation.md](physics-and-navigation.md)).
- **Kest.** Kest's integers and floating point behave the same on every target, and a script reaches the World only through doors that read and stage in declared order ([kest.md](kest.md)).
- **Bytes, not memory.** Anything that leaves a process (replication, checkpoints, saves, checksums) is written field by field in a declared order and byte order, never as a struct's memory, so padding and layout never reach it.

Secure randomness (`rawframe/base/secure_random.h`) is for values a remote peer must not predict, such as session nonces and epochs; it never reaches a World.

## Where it is checked

- Prediction: a predicting client checksums its player's whole predicted scope at a confirmed tick, and the server checksums the same scope at the same tick with a pinned algorithm (`rawframe.prediction.checksum.v1`). A mismatch is a real divergence and is logged ([prediction.md](prediction.md)).
- Golden values: the random generator's vectors, the navmesh fingerprint (a hash of every baked tile's bytes), the packing format's vectors and the cook's outputs pin promised bytes. A deliberate change regenerates them in the same pull request.
- Every tier runs the tests under GCC and Clang, optimized and not, and under sanitizers; the web build runs its tests under Node, and the extended nightly run on Windows, macOS and arm64.

## Traps

- A value computed from wall time, an unordered container's iteration, a pointer's address or a thread's timing must never reach simulation state.
- Presentation may differ between machines; simulation may not. Code that runs on a client only (present systems, interpolation, effects) may use what it likes, but must never write a replicated or predicted component.
- A predicted system may not draw from a World stream: a client does not have the World's random state.
