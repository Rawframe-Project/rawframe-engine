# Audio

How a client hears its mirrored World. The modules are `audio` (decoding, the mixer, declared sounds, streaming and device output over Maul Audio) and `world_audio` (emitters and the listener in the World). A dedicated server carries the sound components as plain values and links none of this.

## Documents

- A game's **mixer** (`mixer` line) is an `audio.mixer` document: the one tree of buses with sends that routes all sound, and the concurrency sets sounds share voices in. Each bus carries a role from a closed set, so a player's volume settings bind to a role, never to a name.
- A **sound** (`sound` lines) is an `audio.sound` document: its variants and how one is picked, the ranges a play draws its volume and pitch from, how it loops, the bus it routes to, the voices it shares, and how it fades with distance. A sound is declared once and played by identity.

Both are parsed and checked, never executed.

## Playing

Each play of a declaration is an instance: it picks its variant, draws its volume and pitch, counts in its concurrency set, fades with its distance from the listener, and goes virtual rather than silent when it has no voice, so it comes back at the right place when one frees. An entity with a `rawframe.sound.Emitter` component plays its declared sounds from where its pose puts it; the one active listener is bound to the client's player. Present systems play sounds through emitters of their own, and a predicted effect may name a sound ([prediction.md](prediction.md#effects)).

## The mix

The mixer sums playing sounds through the layout's buses, their effects, faders and sends, into interleaved stereo. Two threads meet only through bounded queues:

- the owner thread plays, stops and changes things through a command queue;
- the mix thread renders without locks, allocation or blocking, and hands back what finished through a second queue, so every release happens on the owner's thread.

An output opens a device at its own rate through Maul Audio (PipeWire, PulseAudio or ALSA, each opened at run time on Linux; WASAPI on Windows; Core Audio on Apple's platforms; AAudio on Android), and its real-time thread renders a mixer made at that rate into every buffer the device asks for. In the browser, which plays sound from its own thread, the client renders into a sink once a frame and the page drains it onto an AudioContext.

## Clips and streaming

Clips are cooked into one of two forms: 16-bit PCM WAVE, the short-form tier, or Opus, the lossy tier, for long sounds. Every decoder is bounded and refuses what it does not fully understand. A streamed sound stays encoded in memory and is decoded a little ahead of where it plays, on an executor's worker, into a bounded ring the mix thread reads. The mix thread never decodes and never waits: a ring run dry is silence, counted. A game's sounds are read by resource identity and held while the loader lives, so nothing the mixer plays is evicted under it.

`audio.play` turns playback on for a client; `audio.record` records what a client heard to a file, which tests read instead of a device.
