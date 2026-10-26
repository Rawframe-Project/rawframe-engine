# Input and platforms

How a client turns a person's devices into a player's input, and how it meets each platform's windows, suspension and headsets. The modules are `input` (actions, bindings, routing and pairing), `input_window` (the window's events into input), `input_kest` (the sample function), `window` (over Maul Window), `window_host` (a client's window, UI and input together) and `xr` (OpenXR).

## Contents

- [Actions](#actions)
- [Routing and pairing](#routing-and-pairing)
- [From actions to the server](#from-actions-to-the-server)
- [Haptics](#haptics)
- [Windows and the platform](#windows-and-the-platform)
- [Headsets](#headsets)

## Actions

A game's `actions` line names its action set, an `input.actions` document: typed actions (a button, an axis, a 2D or 3D value, a tracked pose), their default bindings, and the contexts that route them. Bindings name physical controls by where they are: keys by their place on the keyboard, mouse buttons, wheel and motion, the standard gamepad by location, and a touch screen as virtual controls (each half of the screen a floating stick and a button). Every control has one name and one shape.

A user's rebinding is an `input.overrides` document (`input.overrides`): a delta over the default bindings, keyed by action, device class and slot, which never changes the set it overrides; resetting is deleting entries. Before a change is kept, the conflict query finds what would collide, shadow or be unreachable, by what would actually activate.

Tracked hands are a source of their own, both hands' 26 joints in OpenXR's order, never an action value and never bound.

## Routing and pairing

The mapper turns control events from paired devices into per-player action state, routed through the contexts each player has active, highest priority first. Press and release edges are accounted once to the simulation tick and once to the frame, so a fast tap is never lost between ticks. The engine's UI navigation actions are a context above every context a game declares; while the UI holds focus their controls are its alone ([ui.md](ui.md#input)).

Local players are slots. Which slot each device plays is an explicit pairing table: a device is paired as it connects, by the client's policy (`input.pairing`), and let go as it goes. Device identity is never player identity. `input.gamepads = false` leaves the machine's gamepads out.

## From actions to the server

Each tick, a client's sample function (Kest, the game's `sample` line) reads its player's committed actions and fills the player's input component, and may send commands. The input component travels to the server in redundant windows; the server writes it into the player's entity in `apply_inputs`, and the game's systems read it like any component ([networking.md](networking.md)). A server never opens the action set or the sample function.

## Haptics

A game's effects may be felt: an `effect` line's `felt` names an output of the action set, with its amplitude, frequency and duration, played on the player's gamepad motors when the effect is delivered.

## Windows and the platform

`window` owns the process's windows through Maul Window: Wayland and X11 on Linux, Win32 on Windows, AppKit on macOS, UIKit on iOS, NativeActivity on Android, and a canvas in the browser without Emscripten. Only `window` includes Maul Window. A window's events become input in `input_window`, in order, with every held control let go when focus leaves, so no key sticks.

`window_host` is a client's window participant: it opens the window (`window.*`), feeds input, lends the platform's clipboard, file dialogs and dropped files to an editor, and runs the UI's accessibility adapter. When the platform suspends the program, or a headset shows the person nothing, the client runs on but nothing reaches a person: frames, sound and input stop until it resumes.

## Headsets

`xr` opens an OpenXR runtime's head-mounted system (`xr.runtime`, never the loader's `XR_RUNTIME_JSON`), has the runtime make the Vulkan device, and runs a session whose frames each locate both eyes, drawn as views of their own. Controllers and hands are actions and tracked sources like any device. A flat client and a headset client play the same game on the same server: what a headset adds is input and views, never authority. Tests run against Monado's headless service (`tools/xr_run.sh`).
