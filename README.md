# Rawframe Engine

A performance-oriented, network-first engine for 2D and 3D games and real-time simulations. The core is C++23. Gameplay and tooling are written in [Kest](https://github.com/Rawframe-Project/kest), a deterministic, statically typed game language. Physics comes from [Maul2D](https://github.com/Rawframe-Project/maul2d) and [Maul3D](https://github.com/Rawframe-Project/maul3d), and navigation from [Maul Nav](https://github.com/Rawframe-Project/maul-nav).

Its first milestone, a headless, networked, Kest-scripted World with a dedicated server, is done. Clients play from a window on the desktop and from a canvas in a browser, drawn through [Maul RHI](https://github.com/Rawframe-Project/maul-rhi) (Vulkan, and WebGPU in the browser) in 2D and 3D, heard, and felt through a gamepad. The reference games are under `games/`: Runners (2D) and Stalls (a small 3D tycoon), with the plaza as the 3D sample. A game can be exported into a folder that plays on its own, or into a site that plays in a browser. There are no releases yet, and every surface is unstable.

## Building

Requirements: CMake 3.28, Ninja, and GCC 14 or Clang 19 or newer (MSVC 17.10 on Windows). On Linux, the window module's backends need the development files of Wayland and X11: `libwayland-dev`, `wayland-protocols`, `libxkbcommon-dev`, `libxkbcommon-x11-dev`, `libxcb1-dev`, `libxcb-randr0-dev`, `libxcb-xkb-dev`, `libxcb-cursor-dev`, and `libxcb-xinput-dev`; and Maul Audio's devices, the headers of PipeWire, PulseAudio, and ALSA: `libpipewire-0.3-dev`, `libpulse-dev`, and `libasound2-dev` (the libraries themselves are opened at run time).

```sh
cmake --preset clang-development
cmake --build out/clang-development
ctest --test-dir out/clang-development
```

Presets exist for `gcc` and `clang` in each of the three configurations (`debug`, `development`, `shipping`), plus `clang-sanitize` (AddressSanitizer and UndefinedBehaviorSanitizer).

## Checking

`tools/check.sh fast` runs the repository rules, the formatter, one build, and the tests. `tools/check.sh` runs everything: both compilers, every configuration, and the sanitizers. Install the pre-push hook with `git config core.hooksPath tools/hooks`.

The full check needs, beyond GCC and Clang 20 with `clang-format-20`:

- the web build's toolchain: `libc++-20-dev-wasm32`, `libclang-rt-20-dev-wasm32`, `wasi-libc`, `lld-20`, and Node.js 18 or later;
- Python 3 with `python3-aioquic`, an independent HTTP/3 and WebTransport implementation that plays the browser in `network_quic`'s tests;
- `brotli`, which compresses the web client as a server would send it for its download budget (`tools/web_budget.sh`);
- optionally `xvfb`, in whose display the desktop client plays a dedicated server (`client_plays_runners`), and Puppeteer with its browser under `/opt/webtest` (or `RAWFRAME_NODE_MODULES` and `PUPPETEER_CACHE_DIR`), with which a real browser plays from a canvas. Without them those tests are skipped.

On Debian or Ubuntu, `apt-get install` installs each of them under the name given.

## Exporting a game

`rawframe-export <game directory> <output directory>` cooks the game, packs and signs it, installs it into the folder's own library, and copies the dedicated server, the client, and the launcher beside it. Running the folder's `rawframe-play` starts the game's server on this machine and then its client. Pass `--key <secret key> --publisher <name>` to sign with a publisher key made by `rawframe-build key`; `export.receipt` lists what was written.

With `--target web`, the folder holds a site and its server instead. Serve `web/` as it is from any static file server, and run `server/rawframe-server --config server/server.conf` on the same host: it listens for browsers over WebTransport on every address at the port given, and writes the fingerprint of the certificate it makes as it starts into `web/`, where the page reads it. A browser trusts such a certificate for at most fourteen days, so restart the server within thirteen, or give it a certificate for the host's name (`network.quic.certificate_file` and `network.quic.private_key_file` in place of `network.quic.self_signed`, and no `network.quic.fingerprint_file`).

## Layout

| Path | Holds |
|---|---|
| `modules/<name>/` | One engine module: `include/rawframe/<name>/`, `src/`, `tests/`. |
| `tools/modules.txt` | The module graph: which module may depend on which. The build and the check enforce it. |
| `tests/harness/` | The test harness. |
| `cmake/` | Build policy: language, warnings, determinism, configurations. |

## License

Rawframe is source-available under the Rawframe Source-Available License; see [LICENSE.md](LICENSE.md). External code contributions are not open yet. Report security issues through GitHub's private vulnerability reporting.
