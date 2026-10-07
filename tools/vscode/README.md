# Rawframe for VS Code

Debugs a running game's Kest scripts (ADR-0066's debugger bridge, D461). VS Code starts `rawframe-debug`, which speaks the Debug Adapter Protocol and attaches to the game's tooling endpoint. Kest's own extension (`kest/editors/vscode`) gives the language: highlighting and `kest lsp`.

## Use

1. Install this folder as an extension: copy or link it into `~/.vscode/extensions/rawframe`.
2. In your user settings (never a workspace's), set `rawframe.debugPath` to the built `rawframe-debug` if it is not on the path.
3. Start the game's dedicated server with a tooling endpoint that grants debug:

   ```
   tooling.endpoint = 127.0.0.1:7801
   tooling.token_file = tooling.token
   tooling.grants = inspect debug
   network.quic.fingerprint_file = server.fingerprint
   ```

4. Add an attach configuration naming the same three things, and start it:

   ```json
   {
     "type": "rawframe",
     "request": "attach",
     "name": "Rawframe: attach to the game",
     "endpoint": "127.0.0.1:7801",
     "pinFile": "${workspaceFolder}/server.fingerprint",
     "tokenFile": "${workspaceFolder}/tooling.token"
   }
   ```

5. Add function breakpoints in the Breakpoints view, by name, with the module or without it (`plaza.stroll` or `stroll`).

When one is hit, the server waits inside its tick. The call stack shows the functions, and each frame its arguments. Continue lets the game run to the next one, and disconnecting takes the breakpoints out and lets the game run.

## Not yet

Line breakpoints, stepping, source positions, and locals other than arguments wait for Kest's public header to say where code was written and which locals a stop has written (`/home/rawframe/kest-debugger-requests.md`).
