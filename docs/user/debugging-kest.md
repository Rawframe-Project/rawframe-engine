# Debugging Kest

A running game's Kest code can be stopped at breakpoints and stepped from VS Code. `rawframe-debug` speaks the Debug Adapter Protocol and attaches to the game's tooling endpoint; the extension in `tools/vscode` starts it. Kest's own extension gives the language: highlighting and `kest lsp`.

## Drawing what the game does

A client's present systems can draw lines, arrows, boxes and spheres over the scene with `rawframe.debug`, to show a ray a system cast, a target it chose or an area it checks:

```kest
debug.arrow(x, y, z, x + vx, y + vy, z + vz, u32(0xffd24aff), 0.0, u32(3))
debug.sphere(tx, ty, tz, 0.5, u32(0x40c0ffff), 2.0, u32(4))
```

A shape shows for its seconds: zero for the tick it is drawn on, so a system that draws it each tick shows it as long as it does; more for that long; `debug.FOREVER` until `debug.clear` clears its channel. Each shape is on a channel from 0 to 31, and the client's `scene.debug_channels` mask says which are shown. The first local player's present systems draw over that player's view. A development client also draws ready layers over the game, as Studio's preview does, when its `scene.debug_layers` names them: `colliders`, `lights` and `grid`. The calls draw nothing in a shipping build, on a server, in a simulation or prediction, or in the sample function, so a game can keep them; a mod may not call them.

## Setting up

1. Install `tools/vscode` as an extension: copy or link it into `~/.vscode/extensions/rawframe`.
2. In your user settings (never a workspace's), set `rawframe.debugPath` to the built `rawframe-debug` if it is not on the path.

## Attaching

To a game Studio plays: Studio's Play grants debugging and records where to attach, so attach by the game alone.

```json
{ "type": "rawframe", "request": "attach", "name": "Rawframe: attach to the game Studio plays",
  "game": "${workspaceFolder}/plaza.game" }
```

To a dedicated server you start yourself, give it a tooling endpoint that grants debugging:

```
tooling.endpoint = 127.0.0.1:7801
tooling.token_file = tooling.token
tooling.grants = inspect debug
network.quic.fingerprint_file = server.fingerprint
```

and attach naming the same three things:

```json
{ "type": "rawframe", "request": "attach", "name": "Rawframe: attach to the game",
  "endpoint": "127.0.0.1:7801", "pinFile": "${workspaceFolder}/server.fingerprint",
  "tokenFile": "${workspaceFolder}/tooling.token" }
```

The endpoint must be a loopback address on this machine, so a configuration a cloned workspace brings never sends a token elsewhere.

## Breakpoints and stepping

Click in the gutter of a `.kest` file for a line breakpoint, or add function breakpoints by name, with the module or without it (`plaza.stroll` or `stroll`). A line with no code is shown unverified. Attached by the game, a line is matched by its path under the game's directory; attached by an endpoint, by its file's name.

When a breakpoint is hit, the server waits inside its tick; the World owes no ticks for the time it stood still. The call stack shows each function, the line it stands at and its locals: numbers, truths and text by value, and `(not written yet)` for one the function has not reached. Continue runs to the next breakpoint. Step Over, Step Into and Step Out move a line: over a call, into the next line wherever it is, or out to the line the function returns to; a step past the last line of a tick's function runs to the next breakpoint. There is no Pause: a game stops at its breakpoints. Disconnecting removes the breakpoints and lets the game run.

When the game goes, the session ends and the debug console says how: "the game's tooling endpoint kept silent" when it stopped answering, as a game held by a native debugger does, "closed" when it stopped, and "sent a line past 4 MiB" when it sent more at once than its protocol allows. The adapter never reads what the game says unasked and keeps at most 64 MiB of it, the oldest lines let go.
