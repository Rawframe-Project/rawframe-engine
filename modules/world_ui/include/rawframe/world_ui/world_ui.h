#pragma once

// The UI as World data (ADR-0034, D376): every `rawframe.ui.Node` component
// of the World a local player's client mirrors is a node of one UI tree,
// keyed by its entity, nested by the game's UI document, and laid out in the
// player's view. What it draws is SPEC-0032's draw-command list, for the
// device to draw over the scene and the canvas. Client only: a dedicated
// server carries the components as values and links none of this.

#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/ui/access.h"
#include "rawframe/ui/document.h"
#include "rawframe/ui/styles.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/navigation.h"
#include "rawframe/view/typing.h"
#include "rawframe/view/view.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_ui {

/// A text field's most bytes: what `rawframe.ui.Typed` holds (D426).
inline constexpr std::uint32_t kMostTypedBytes = 252;

/// `rawframe.ui.Typed` as Kest lays it out: its length, then its bytes.
struct Typed {
    std::uint32_t length = 0;
    std::array<std::byte, kMostTypedBytes> bytes{};
};

/// The words of label `label` with `value` as its argument, in the locale
/// of local player `player`, whose view shows them; none for a label the
/// game does not have, or whose words cannot be made (D386).
using Words = std::function<std::optional<std::string>(std::size_t player, std::uint64_t label, std::int64_t value)>;

/// The game's node components, in declaration order, and the one each is
/// inside by its UI document (D619), an index of them; none for a root of
/// its view.
/// The game's fonts by the identities its `font` lines give, in their order,
/// the first the one a node's nought names, and where its labels' words come
/// from (D386).
struct UiSettings {
    std::vector<schema::ComponentTypeId> nodes;
    std::vector<std::optional<std::size_t>> parents;
    /// Each node's words (D427): a component of `rawframe.ui.Typed` on its
    /// entity, as the UI document says (`shows`); none for a node that
    /// shows only its label. As long as `nodes`, or empty for none at all.
    std::vector<std::optional<schema::ComponentTypeId>> shows;
    std::vector<std::uint64_t> fonts;
    Words words;
    /// The game's style classes (D431), which nodes name by identity.
    ui::StyleSheet styles;
    /// Each node's value kept from a run before (D626), which its kept
    /// nodes start as; as long as `nodes`, or empty for none at all.
    std::vector<std::optional<float>> kept;
    /// The most nodes held at once, every view's together; a node past them
    /// is left out and counted.
    std::uint32_t maximumNodes = 4096;
};

/// A local player's view as the UI lays it out: the World its client
/// mirrors (none while it has none) and its own player in it, and the
/// view's rectangle in the window, logical pixels from its top left.
struct UiView {
    world::World* world = nullptr;
    world::EntityHandle player;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

/// `view` drawn in from each edge it shares with a `window` the platform
/// covers by `safe`, so the UI lays out where it is seen and touched
/// (SPEC-0030's safe-area root input, D589); an edge inside the window,
/// between two players' views, stays where it is.
[[nodiscard]] UiView insideSafeArea(UiView view, view::ViewSize window, view::ViewInsets safe) noexcept;

struct UiStatistics {
    /// Frames laid out and drawn.
    std::uint64_t frames = 0;
    /// Nodes made, and changed after: each costs a layout and a paint.
    std::uint64_t made = 0;
    std::uint64_t changed = 0;
    /// Nodes left out in a frame, summed: past the most held, or inside a
    /// parent neither their entity nor the player has, or with a value the
    /// tree refuses (a negative size or radius, an anchor past one).
    std::uint64_t leftOut = 0;
    /// The most nodes a frame held.
    std::uint64_t mostNodes = 0;
    /// Words given to nodes, and labels that had none (D386).
    std::uint64_t texts = 0;
    std::uint64_t textsUnknown = 0;
    /// Words given to nodes from a component of typed words (D427).
    std::uint64_t typedShown = 0;
    /// Text fields given the keyboard, what was typed into them (each text,
    /// key, and composition), and their texts given by Enter (D426).
    std::uint64_t focused = 0;
    std::uint64_t typed = 0;
    std::uint64_t submitted = 0;
    /// Wheel turns a scrolling node took (D444).
    std::uint64_t wheeled = 0;
    /// Nodes naming a class the game's styles do not have (D431).
    std::uint64_t stylesUnknown = 0;
    /// Moves of focus by navigation, and nodes it activated (D430).
    std::uint64_t navigated = 0;
    std::uint64_t activated = 0;
    /// Presses, moves, and releases of the pointer a range took (D621).
    std::uint64_t ranged = 0;
    /// Checkable nodes checked or unchecked by the player (D622).
    std::uint64_t toggled = 0;
    /// The game's style classes made again as their document changed
    /// (D634).
    std::uint64_t restyled = 0;
    /// Changes to the values of kept nodes, and kept nodes started as kept
    /// (D626).
    std::uint64_t kept = 0;
    std::uint64_t keptGiven = 0;
};

class WorldUi {
public:
    /// Refused for a parent index out of range.
    [[nodiscard]] static result::Result<std::unique_ptr<WorldUi>> create(UiSettings settings);

    WorldUi(const WorldUi&) = delete;
    WorldUi& operator=(const WorldUi&) = delete;
    ~WorldUi();

    /// One frame: each view's World read, its nodes made, changed, or
    /// removed as its components are, laid out in its rectangle of a window
    /// `width` by `height` logical pixels, and drawn, `scale` device pixels
    /// each. A view whose World is another than the last starts afresh.
    /// The roots of a view are laid out in a column from its top left, in
    /// `order` and then by entity, each its own size, an absolute one where
    /// it says; children likewise in their parent.
    /// Classes' transitions move to `seconds` on a monotonic clock.
    [[nodiscard]] result::Status
    update(std::span<const UiView> views, float width, float height, float scale, double seconds = 0);

    /// Font `id`, one of the settings' fonts, read from a cooked font's
    /// bytes; the nodes that show words are given them again in it. Refused
    /// for a font not declared, one already read, or bytes the tree does not
    /// take.
    [[nodiscard]] result::Status addFont(std::uint64_t id, std::span<const std::byte> bytes);

    /// The nodes that show the game's words are given them again (D539):
    /// the locale they are formatted in changed.
    void reword();
    /// The game's style classes made again from `styles` (D634), as an
    /// author edits them while the game plays: every node given its class
    /// anew, one the sheet no longer has leaving its node unstyled. Refused
    /// where the tree takes no class of the sheet: the nodes are unstyled
    /// until a sheet it takes comes.
    [[nodiscard]] result::Status restyle(ui::StyleSheet styles);

    /// The window's UI, every view's, seated where assistive technology
    /// reads it (D571); what its access answered, if one was made.
    [[nodiscard]] result::Status seatIn(ui::AccessSeat& seat);
    /// What a screen reader asked of a node navigation reaches (D572):
    /// focus moved to it as navigation moves it; a press focuses and
    /// activates it as navigation's activation does, its press code, none
    /// for a field, which takes the keyboard. A field's selection set or
    /// its text replaced gives it the keyboard and edits it as typing does
    /// (D613). Nothing for another node.
    std::optional<std::int64_t> ask(const ui::AccessRequest& request);

    /// What a press at `x`, `y`, logical pixels of the window, lands on as
    /// the last update laid the UI out (D421): none when it passes through to
    /// the game, else the press code of the node it lands on.
    [[nodiscard]] std::optional<std::int64_t> press(float x, float y) const;

    /// A press went down at `x`, `y` (D426): on a text field it gives the
    /// field the keyboard, the caret where it landed; anywhere else it
    /// takes the keyboard from the field that held it. The caret is placed
    /// at once, by the last layout.
    void pressAt(float x, float y);
    /// A wheel turned at `x`, `y` by `deltaX`, `deltaY` detents (D444):
    /// the scrolling node under it moves its children a step, eased over
    /// the frames that follow.
    void wheelAt(float x, float y, float deltaX, float deltaY);
    /// The press let go: the node it went down on is no longer pressed
    /// (D431), and a range it went down on lets the pointer go (D621).
    void release();
    /// Where the mouse is over the window, none off it: the node a press
    /// there would land on is hovered (D431), and a range a press went down
    /// on is dragged there (D621).
    void hoverAt(std::optional<std::array<float, 2>> pointer);
    /// The value of the range whose press code is `press` in local player
    /// `player`'s view, as the player moved it (D621), or of the checkable
    /// node's, one checked and nought not (D622); none for no such node.
    [[nodiscard]] std::optional<float> valueOf(std::size_t player, std::int64_t press) const;
    /// Each node component's value as its kept node in the first local
    /// player's view holds it (D626), a range's value or one for checked;
    /// as the settings' `kept` until such a node appears. As long as the
    /// settings' nodes; `statistics().kept` counts its changes.
    [[nodiscard]] std::span<const std::optional<float>> kept() const noexcept;
    /// What was typed, to the field holding the keyboard; nothing while
    /// none does. Enter gives a single line's text, Escape lets go.
    void type(const view::Typing& typing);
    /// Where the caret of the field holding the keyboard was last drawn,
    /// logical pixels of the window; none while none does.
    [[nodiscard]] std::optional<view::UiTyping::Caret> caret() const noexcept;
    /// The fields' texts given by Enter since last asked, oldest first.
    [[nodiscard]] std::vector<view::Submitted> takeSubmitted();

    /// Navigation without a pointer (SPEC-0030, D430), among the nodes
    /// that take presses and the text fields of a view, in one view: focus
    /// to the first in reading order of the first view with any, unless a
    /// node holds it; whether one holds it after.
    bool enterNavigation();
    /// How many nodes navigation reaches now: those that take presses and
    /// the text fields of the first view with any.
    [[nodiscard]] std::size_t reachable();
    /// Focus moved from the node holding it, the nearest node in a
    /// direction or the next in reading order; whether it moved.
    bool navigate(view::NavigationMove move);
    /// The focused node activated: its press code for one that takes
    /// presses; a field takes the keyboard, its caret at its end, and gives
    /// none. Enter or Escape there gives it back, focus staying on it.
    [[nodiscard]] std::optional<std::int64_t> activate();
    /// The keyboard taken back from a field, focus staying; or, none held,
    /// focus let go.
    void dismiss();
    /// Whether a node holds focus, by navigation or a field's keyboard.
    [[nodiscard]] bool navigating() const noexcept;

    /// What the last update drew.
    [[nodiscard]] const ui::DrawList& drawn() const noexcept;
    [[nodiscard]] const UiStatistics& statistics() const noexcept;

    struct State;

private:
    explicit WorldUi(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_ui
