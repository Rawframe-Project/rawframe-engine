# Input

A game never reads keys or buttons. It declares actions, and a client turns whatever the player holds into those actions, then into the player's input component. How it works is in [input and platforms](../internals/input-and-platforms.md).

## The action set

A game's `actions` line names an `input.actions` document:

```json
{
  "kind": "input.actions",
  "formatVersion": 1,
  "actions": [
    {
      "actionId": "b5315e7c271553e1",
      "name": "move",
      "valueType": "axis2d",
      "displayName": "Move",
      "bindings": [
        { "device": "keyboard", "quad": { "up": "key_w", "down": "key_s", "left": "key_a", "right": "key_d" } },
        { "device": "gamepad", "control": "stick_left" }
      ]
    }
  ],
  "contexts": [
    { "contextId": "81cf6036baa5fdc0", "name": "moving", "actions": ["move"] }
  ]
}
```

- **Actions** have a durable identity, a name, and one value type: `bool`, `axis1d`, `axis2d`, or `pose` (a tracked controller's grip or aim).
- **Bindings** name a device class (`keyboard`, `mouse`, `gamepad`, `touch`, `controller`) and either one control, a `negative` and `positive` pair making an axis, or a `quad` making two axes. Keys are named by their place on the keyboard (`key_w` is where W is on a US layout), so bindings survive other layouts. Axes take deadzones; digital activation takes press and release thresholds; keyboard bindings may need modifiers held.
- **Contexts** group actions; a context of higher priority may claim its controls from those below it.
- **Haptics** are optional outputs (`haptics`), felt on the player's gamepads.

A touch screen is virtual controls: each half of the screen a floating stick and a button.

## Reading actions

The game's `sample` function runs on the client each tick and fills the player's input component from committed actions, by their identities:

```kest
import rawframe.input

const MOVE: u64 = 0xb5315e7c271553e1

fn sample(into: [controls.Stick]) {
    into[0].x = input.x(MOVE)
    into[0].z = -input.y(MOVE)
}
```

`rawframe.input` reads a button's state (`on`, `pressed`, `released`), an axis's values (`x`, `y`, `z`), a pose's turn (`turnX` to `turnW`) and whether it is tracked, and what the UI took (`uiPressed`, `uiTyped`, `uiSubmitted`). Pressed and released edges are counted once per tick, so a tap between two ticks is never lost.

## Players and devices

Local players are slots; which slot each device plays is decided as it connects (`input.pairing`). `input.gamepads = false` leaves the machine's gamepads out. A game with split-screen `layout` lines gives each local player their own view.

## Rebinding

A player's rebinding is an `input.overrides` document (`input.overrides`), a delta over the default bindings that never changes the action set; resetting a binding deletes its entry. Some controls are kept by the engine and cannot be bound. While the UI holds focus, the engine's navigation actions (arrow keys and the d-pad, Tab and the shoulders, Enter and the south button, Escape and the east) are the UI's, and no game action sees them.
