# UI

A game's UI is World data: its nodes are components of a player's entities, so the same program, replication and authoring that make the game make its UI. The modules are `ui` (the tree over Maul UI, documents, styles, text editing and accessibility) and `world_ui` (the World's nodes into each local player's tree). Studio's own panels are drawn by the same tree and renderer.

## Contents

- [Nodes as components](#nodes-as-components)
- [Layout and drawing](#layout-and-drawing)
- [Text](#text)
- [Input](#input)
- [Accessibility](#accessibility)

## Nodes as components

Every `rawframe.ui.Node` component on an entity a local player's client mirrors is a node of that player's UI tree, keyed by its entity. A game's `ui` line names its UI document (`ui.document`), which nests nodes inside others, says what each starts as, and which typed words it shows. Nodes are usually presentation components, which present systems write; a node may also replicate, as any component may. A game's `styles` line names a `ui.styles` document of classes: each a look for a node's base and for its hovered, pressed, focused and disabled states, only the parts given, with timed or spring transitions. A node names its class by `style` and keeps every part of its look it leaves at zero from it.

A dedicated server carries the node components as values and links none of this.

## Layout and drawing

`world_ui` mirrors the nodes into the tree in `presentation_extract`, lays each player's tree out in the player's view (their region of the game's layout, constrained to its aspect), and draws it. Layout is Scale+Offset sizing with flexbox rules; looks are fills, borders, images, nine slices, shadows and gradients, inside clips. The tree draws a draw-command list that `render_canvas_gpu` paints over the scene and the canvas, each box a rounded shape by its signed distance, smoothed over a pixel. A node or subtree that did not change is not laid out or painted again.

## Text

Text is shaped through HarfBuzz and rasterized through FreeType over Maul Unicode's segmentation and bidi, in fonts the cook rebuilt through the OpenType Sanitizer. A node's words are a key of the game's string tables, with up to two arguments its message may read ([localization.md](localization.md)). Glyph images live in the tree's glyph atlas; when it fills, the plot drawn least recently is emptied rather than the whole atlas.

Text fields (`EDIT_LINE`, `EDIT_MESSAGE`, `EDIT_LINES`) take the keyboard when pressed: caret and selection, editing by cluster, word and line, and an input method's composition inline at the caret, with the platform's candidate window placed by it. While a field holds the keyboard, keys reach no action of a context gated by text editing. Tab moves to the next field of its view in reading order.

## Input

The UI is the topmost routing node for a local player's pointers: a press that lands on a node whose `hit` is `HIT_SELF` is the UI's, and no game action sees it. Nodes may root activation, modal and overlay layers. A zeroed node passes every press on, so a game without UI behaves as before.

Without a pointer, the engine's navigation actions (arrow keys and the d-pad, Tab and the shoulders, Enter and the south button, Escape and the east) move focus to the nearest node in a direction or the next in reading order, activate it, and leave; no game action sees them meanwhile. The sample function reads what the UI took (`input.uiPressed()`, `input.uiTyped()`, `input.uiSubmitted()`), and present systems read the hovered node through `ui.hovered()`.

## Accessibility

The tree is also what assistive technology reads. From a root's subtree Maul UI builds updates of the nodes that changed, each whole, with the focus, and hands them to the platform's adapter: AT-SPI on Linux, UI Automation on Windows, the AX API on macOS, UIAccessibility on iOS, Android's accessibility framework, and ARIA in the browser. Nothing is built for a root until an access is made for it. A password field's value is never given to an adapter. `ui.accessibility = false` turns it off.

The extended nightly run reads a played game's UI through each desktop and mobile platform's own accessibility API, as a screen reader does.
