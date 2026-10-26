# Troubleshooting

Every program logs one JSON record a line on standard output, and stops with a record saying why. Read the log first: most problems name themselves there.

## A game does not start

- **`kest` errors when the program loads.** The program did not compile, or an entry does not take the columns its `system` line lists in that order, or a Kest type does not have its component's size and alignment. The record names the file, line and entry.
- **A door is refused.** The program asks for a door its machine does not offer: a component's lookup a system did not declare with `lookup`, a stream not declared with `random`, or, for a mod, a door not marked safe for untrusted code. The record names the door.
- **A setting is refused.** A configuration line is malformed, repeated, or out of range; the server stops before it starts and says which.
- **No catalog.** The game's string tables and translations do not make a catalog: a translation of a key the table lacks, or a message reading an argument its source does not.
- **Content does not verify.** A Build, library or record failed its signature or digest. The installation is untouched; reinstall or update from the origin.

## Players cannot connect

- **The client is refused at admission.** The client and server are of different builds or games: compatibility is exact, with no downgrade. Export both from the same tree.
- **The certificate does not match.** A client pins the server's fingerprint; a server that made a new certificate wrote a new fingerprint file. Point the client at the current file, or give the server a certificate for its host name.
- **Browsers cannot reach the server.** The page reaches the server on the host it came from, at the port in `play.json`, over UDP. Open that UDP port, and serve the page from the same host.
- **A player is dropped.** The record says why: idle past the timeout, protocol strikes from malformed input, or guaranteed events that could not be delivered in time.

## The player jumps back

The client mispredicted: its predicted systems gave a result other than the server's. Look for `prediction_divergence` in the server's log, and see [multiplayer](multiplayer.md#responding-at-once-prediction) for what a predicted system may and may not do. In development, `replication.divergence_capture` and `bots.divergence_capture` keep both sides of a divergence to compare.

## The server falls behind

The server's `tick_summary` gives its tick durations. A World that falls behind catches up a few ticks an iteration, then closes admission (degraded) and finally reports itself overloaded. Systems that run out of fuel are named in the log; Studio's Profiler shows each system's time.

## Nothing is drawn or heard

- **No device.** A client with no GPU and no software rasterizer logs `device_unavailable`. On Linux, Mesa's lavapipe (`mesa-vulkan-drivers`) is enough to play.
- **No sound.** Playback is on only with `audio.play`; a game that declares no mixer plays in silence.
- **A material is missing.** A material that needs a shader of its own is refused by a cook without the shader toolchain; the cook's report names it.

## Reporting a problem

Open an issue with the program, the platform, what you did, and the log's last records. Security problems go through GitHub's private vulnerability reporting, never an issue.
