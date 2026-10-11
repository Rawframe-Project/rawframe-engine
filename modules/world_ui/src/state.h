#pragma once

// world_ui's own state, shared by its files: what it laid out of each
// view's World (world_ui.cpp) and what holds focus and the pointer
// (interaction.cpp).

#include "rawframe/ui/text_edit.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_ui/world_ui.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawframe::world_ui {

/// What a node's range was last given from its fields (D621), and its
/// thumb: the value the player moves stays until the node's own changes.
struct RangeGiven {
    std::uint32_t axis = 0;
    float minimum = 0;
    float maximum = 0;
    float step = 0;
    float value = 0;
    std::optional<ui::Node> thumb;
    friend bool operator==(const RangeGiven&, const RangeGiven&) = default;
};

/// The popup a node was last made (D623): its side, its gap, and the node
/// it is placed beside, by its key.
struct PopupGiven {
    std::uint32_t side = 0;
    float gap = 0;
    std::uint64_t anchor = 0;
    friend bool operator==(const PopupGiven&, const PopupGiven&) = default;
};

/// A node component of an entity, and the node it is in the tree: none
/// while its value is refused or there was no room for it.
struct Entry {
    std::optional<ui::Node> node;
    ui::NodeFields value;
    /// The words it shows, as last read (D427).
    Typed words;
    bool seen = false;
    /// Its node was made a text field (D426), and shows its label as the
    /// field's placeholder now, not what was typed (D428).
    bool editable = false;
    bool placeholder = false;
    /// The states its node was last given (D431).
    ui::States states;
    /// The local player whose view it is in, whose locale its words are in.
    std::size_t player = 0;
    /// The range its node was last given (D621): none for no range.
    std::optional<RangeGiven> ranged;
    /// A checkable node's `checked` as it last read (D622), none for one
    /// not checkable; and whether it is checked now, which the player's
    /// presses change until `checked` itself does.
    std::optional<std::uint32_t> checkGiven;
    bool checkedNow = false;
    /// The node it is inside, by its key, as the last frame nested it: a
    /// radio's group is those inside the same one, and a popup is placed
    /// beside it.
    std::uint64_t parent = 0;
    std::optional<PopupGiven> popped;
};

/// The node holding focus (D430): a text field given the keyboard, its
/// editing, or any node reached by navigation, ringed while it holds it.
struct Focus {
    Entry* entry = nullptr;
    /// Reached by navigation, so focus stays as the keyboard is given back.
    bool navigated = false;
    /// The field holds the keyboard; its editing, made as it takes it.
    bool keyboard = false;
    std::unique_ptr<ui::TextEdit> edit;
};

using EntryKey = std::pair<world::EntityHandle, std::uint32_t>;

/// A view's root, sized to its rectangle, and the nodes its World holds.
struct ViewState {
    world::World* world = nullptr;
    std::optional<ui::Node> root;
    std::array<float, 4> placed{-1, -1, -1, -1};
    std::vector<world::ColumnQuery> queries;
    /// Each node component's words component in the World, if it shows one.
    std::vector<std::optional<schema::ComponentRuntimeId>> shows;
    std::map<EntryKey, Entry> entries;
};

/// A child in its parent's order: by `order`, then entity, then component.
struct Placed {
    std::int32_t order = 0;
    world::EntityHandle entity;
    std::uint32_t component = 0;
    ui::Node node;
};

/// How a field's text is edited: its kind and room, and its caret and
/// selection in its text's color.
ui::EditSettings editSettingsOf(const ui::NodeFields& node) noexcept;

/// A node by one number: its slot, then its generation.
std::uint64_t keyOf(ui::Node node) noexcept;

struct WorldUi::State {
    UiSettings settings;
    std::unique_ptr<ui::Tree> tree;
    /// The fonts read, by their game identities.
    std::unordered_map<std::uint64_t, ui::Font> fonts;
    ui::Node window;
    std::vector<ViewState> views;
    /// Each parent's children as last attached, by the parent's node.
    std::unordered_map<std::uint64_t, std::vector<ui::Node>> attached;
    std::uint32_t held = 0;
    ui::DrawList drawn;
    UiStatistics statistics;
    /// The last frame's time, which wheel steps ease by.
    double seconds = 0;
    std::optional<Focus> focus;
    std::optional<view::UiTyping::Caret> caret;
    std::vector<view::Submitted> submitted;
    /// The game's classes by identity (D431); where the mouse is; and the
    /// node a press went down on, until it is let go.
    std::unordered_map<std::uint64_t, ui::Style> classes;
    std::optional<std::array<float, 2>> pointer;
    Entry* pressed = nullptr;
    /// A press went down on a range and is held: the pointer's moves drag
    /// it until it is let go (D621). The thumb inside each range, by the
    /// range's node, as the last frame nested them.
    bool ranging = false;
    std::unordered_map<std::uint64_t, ui::Node> thumbs;

    /// Focus taken from the node holding it: its ring goes, and a field
    /// gives back the keyboard and shows its placeholder again if it is
    /// empty; `shown` false for a node going.
    void letGo(bool shown = true);

    /// `entry`'s look as focus finds it: ringed while it holds focus, and a
    /// field's text or placeholder; whether the tree took it.
    bool giveFocusLook(Entry& entry);

    /// The entry whose node is `node`; null for none.
    [[nodiscard]] Entry* entryOf(ui::Node node) noexcept;

    /// Each range given its fields where they or its thumb changed, the
    /// value the player moved kept unless its own changed; a node no
    /// longer a range cleared (D621).
    void giveRanges();

    /// Each popup placed beside the node it is inside where its side, gap,
    /// or parent changed; a node no longer one cleared (D623).
    void givePopups();

    /// `entry` checked or unchecked as a press does it (D622): a box or a
    /// switch turned over, a radio checked and the radios beside it
    /// unchecked.
    void toggle(Entry& entry);

    /// The pointer's event given to the ranges under it (D621), counted
    /// where one took it.
    void point(ui::PointerEvent::Action action, float x, float y, bool holding);

    /// Each node given the states it is in now, where they changed: hovered
    /// under the mouse as the last layout placed it, pressed while a press
    /// on it is held, focused while it holds focus (D431).
    void giveStates();

    /// A field done with the keyboard, by Enter or Escape: focus stays on
    /// it if navigation brought it there, else it goes.
    void endTyping();

    /// Where the caret of the field holding the keyboard is, in the window,
    /// as the tree was last laid out; none while none does.
    void placeCaret();

    /// Focus given to `entry`, from any node holding it, and with
    /// `keyboard` a field's keyboard too: its placeholder goes before its
    /// caret is made.
    void focusOn(Entry& entry, bool keyboard, bool navigated);

    /// A node focus can reach and where it was last laid out.
    struct Reachable {
        ui::Rect rect;
        EntryKey key;
        Entry* entry = nullptr;
    };

    /// What focus can reach in the view of the node holding it, or with
    /// none, of the first view that has any, in reading order: top, then
    /// left, then entity and component. Text fields alone with `fields`,
    /// else every node that takes presses too. A view is one layer, and
    /// focus never leaves it.
    std::vector<Reachable> reachable(bool fields);

    /// Focus moved from the node holding it by `move`, among `fields`
    /// alone or every node it can reach; whether it moved. Next and the
    /// one before go round; a direction goes to the nearest node that way,
    /// or nowhere. A field reached by Tab, `fields`, takes the keyboard
    /// with its text all selected (D429).
    bool moveFocus(view::NavigationMove move, bool fields);

    /// `entry`'s node gone, its children first made roots so they live on.
    void drop(Entry& entry);

    /// `value` given to `entry`, its node made if it has none; whether the
    /// tree took it.
    bool give(Entry& entry, world::EntityHandle entity, const ui::NodeFields& value);

    /// A text field shows what was typed into it in its text look, never a
    /// label (D426); empty and without the keyboard, its label's words as
    /// its placeholder, at half its text's alpha (D428). Whether the tree
    /// took it.
    bool giveFieldLook(Entry& entry);

    /// `value`'s words given to `node`, or none, the typed `shown` in place
    /// of its label (D427); whether the tree took them.
    bool giveWords(ui::Node node,
                   const ui::NodeFields& value,
                   std::size_t player,
                   std::optional<std::string_view> shown = std::nullopt);
    /// Every node of an entry `which` picks given its words again, a field
    /// its look, keeping what was typed into it; one the tree refuses is
    /// dropped.
    void giveWordsAgain(const std::function<bool(const Entry&)>& which);

    /// `view` bound to `world`: every node of the World before gone.
    result::Status bind(ViewState& view, world::World* world);

    /// `view`'s nodes made, changed, and removed as its World's components
    /// are.
    void mirror(ViewState& view);

    /// Every node of `view` in its parent, in order: its component's
    /// parent on its entity or the player, or the view's root.
    void
    nest(ViewState& view, world::EntityHandle player, std::unordered_map<std::uint64_t, std::vector<Placed>>& into);

    /// Each parent's children as `wanted` has them, moved only where they
    /// differ from what is attached.
    result::Status attach(std::unordered_map<std::uint64_t, std::vector<Placed>>& wanted);
};

} // namespace rawframe::world_ui
