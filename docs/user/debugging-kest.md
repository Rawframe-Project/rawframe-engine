# Debugging Kest

A running game's Kest code can be stopped at breakpoints and stepped from VS Code. `rawframe-debug` speaks the Debug Adapter Protocol and attaches to the game's tooling endpoint; the extension in `tools/vscode` starts it. Kest's own extension gives the language: highlighting and `kest lsp`.

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
