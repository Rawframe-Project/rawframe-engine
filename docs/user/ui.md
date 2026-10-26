# UI

A game's UI is made of components: each `rawframe.ui.Node` on an entity a client mirrors is a node of that player's UI. Present systems write nodes as they write any presentation; the UI document arranges them; style classes give them their looks. How it works is in [UI internals](../internals/ui.md).

## Setting it up

```
component 043c72ce-e948-490a-80d6-e3a22fbdb695 tycoon.purse rawframe.ui.Node
component 9547d96d-27fb-4ec0-950c-a990145d2d97 tycoon.sign rawframe.ui.Node
presentation tycoon.purse tycoon.sign on player
present tycoon.purse purse read tycoon.owner write tycoon.purse
text tycoon.strings
font 8e12f827f47f7d51 Inter-Regular.ttf
styles tycoon.styles
ui tycoon.ui
label f1182d0420cc09a0 tycoon.strings hud.coins coins goal
```

- The **UI document** (`ui` line, `ui.document`) nests nodes inside others, says what each starts as, and which words it shows.
- **Style classes** (`styles` line, `ui.styles`) each give a look for a node's base and its hovered, pressed, focused and disabled states, only the parts given, with timed or spring transitions. A node names its class by `style` and keeps from it every part of its look it leaves at zero.
- **Words** come from string tables by key (`label` lines), with up to two arguments a node's `textValue` and `textSecond` give, so every word can be translated ([localization](localization.md)).

Nodes are laid out by flex rules with Scale+Offset sizes, and drawn with fills, borders, images, nine slices, shadows and gradients, over the scene and the canvas, in each local player's own view.

## Presses and text

- A node whose `hit` is `HIT_SELF` takes the mouse or touch presses that land on it; no game action sees them. The sample function reads which node was pressed with `input.uiPressed()`, and present systems read the node under the mouse with `ui.hovered()`, to light it.
- A node whose `edit` is `EDIT_LINE`, `EDIT_MESSAGE` or `EDIT_LINES` is a text field: pressed, it takes the keyboard, with a caret, selection, word and line editing and the platform's input methods. Enter gives a line's text to the sample function (`input.uiSubmitted()`, `input.uiTyped()`), which may send it to the server as a command. Tab moves to the next field.
- The UI document can make a node show the text of a `rawframe.ui.Typed` component in place of its label while it holds any. That component may replicate, so the server can show every player what one typed.
- Nodes may be activation, modal or overlay layers.

## Without a pointer

`input.uiNavigate()` puts focus on the first node that takes presses or text, ringed. The engine's navigation actions then move focus, activate and leave: the arrow keys and the d-pad, Tab and the shoulders, Enter, Space and the south button, Escape and the east. While the UI holds focus, those controls are its alone.

## Accessibility

Screen readers read the UI through the platform's own accessibility API (AT-SPI, UI Automation, the AX API, UIAccessibility, Android's framework, ARIA in the browser): each node with its role, name and state, the focus, and a text field's text, except a password's. Give every node that takes presses a label, as it is what a screen reader says. `ui.accessibility = false` turns it off.
