# Exporting

`rawframe-export` makes a game into something players run: a native folder, a website, or a phone package, each with its dedicated server. It cooks the game, packs it into a signed Build, installs it, and copies the programs beside it, writing `export.receipt` with every file's SHA-256. How it works is in [packaging](../internals/packaging.md).

```sh
rawframe-export <game directory> <output directory> [options]
```

The game is `<directory name>.game` unless `--game` names it. The tools (`rawframe-cook`, `rawframe-build`, `rawframe-server`, `rawframe-client`, `rawframe-play`) are found beside `rawframe-export` unless `--tools <directory>` or `--cook`, `--build`, `--server`, `--client` and `--play` name them. The output directory must not exist or be empty.

## A native folder

The default. Running the folder's launcher, named after the game, starts the game's server on this machine's loopback and then a client pinned to the certificate the server makes. The port is taken from the game's identity (20000 to 29999), so two exported games run at once; `--port` names one. `--title` sets the window's title and `--icon <image>` its icon (else the game's own `icon.png`).

## Signing

Without a key, the export makes one for this export alone and deletes its secret once the Build is signed. To sign as a publisher, make a key once and keep its secret:

```sh
rawframe-build key <publisher> <directory>     # writes <kid>.key, the secret, and <publisher>.keys
rawframe-export games/mygame out/mygame --key <directory>/<kid>.key --publisher <publisher>
```

The folder's library pins the publisher's key set; the client plays only Builds signed by it.

## The web

```sh
rawframe-export games/mygame out/mygame-web --target web
```

The folder holds `web/`, a static site, and `server/`, its dedicated server. Serve `web/` as it is from any static file server, and run on the same host:

```sh
server/rawframe-server --config server/server.conf
```

It listens for browsers over WebTransport on every address at the port, and writes the fingerprint of the certificate it makes into `web/`, where the page reads it. A browser trusts such a certificate for at most fourteen days, so the server makes a new one a day before its own ends and writes the new fingerprint: a page loaded after that pins the new one, while players already in keep playing. To use a certificate for the host's name instead, set `network.quic.certificate_file` and `network.quic.private_key_file` in place of `network.quic.self_signed`, and drop `network.quic.fingerprint_file`.

The web client and its page files are found under `<tools>/web/` unless named (`--web-client`, `--maul-window`, `--maul-rhi`, `--maul-ui`, `--page`).

## Android and iOS

- `--target android --address <host>` packs the game for Android: `android/game/` is what the client's package carries, pinned to a server identity made once, and `server/` the server a phone reaches at `--address`. With `--android-client <librawframe_client.so> --packager tools/android_apk.sh` the export makes the package itself; with `--android-keystore` and `--android-key-alias` it signs a release package, reading the keystore's password from `RAWFRAME_ANDROID_KEYSTORE_PASSWORD`.
- `--target ios --address <host>` does the same for iOS; with `--ios-client <rawframe-client.app>` it makes the application, for the simulator.

## Updates

A folder can follow a release channel, so players get new versions without exporting again:

```sh
rawframe-build release <mirror> <composition record> <version> stable <key>   # publish a Release and point stable at it
rawframe-build point <mirror> <subject> stable <release> <key>                 # point a channel at another Release; an older one rolls back
rawframe-export games/mygame out/mygame --follow <origin> [--channel beta]
```

The launcher of a following folder runs `rawframe-install follow` before each play: it checks the channel, verifies every record against the publisher's pinned keys, refuses a replayed pointer, and installs the Release's Composition, fetching only the chunks it lacks. The origin is a mirror directory or an `http` or `https` URL; `--authorities <PEM file>` trusts other certificate authorities than the system's. Nothing an origin serves is trusted until it verifies.
