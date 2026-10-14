// What holds focus and the pointer (D426, D429, D430, D431): presses and
// the mouse, text typed into a field, Tab between fields, and navigation
// without a pointer.

#include "rawframe/world_ui/world_ui.h"
#include "state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

namespace rawframe::world_ui {

namespace {

/// Whether `node` goes the way of `move` from `from`, and how far: along
/// it, the distance across it counted twice, so the nearest in line wins.
std::optional<float> distanceOf(const ui::Rect& from, const ui::Rect& node, view::NavigationMove move) noexcept {
    const float kX = (node.x + node.width / 2) - (from.x + from.width / 2);
    const float kY = (node.y + node.height / 2) - (from.y + from.height / 2);
    float along = 0;
    float across = 0;
    switch (move) {
    case view::NavigationMove::Up:
        along = -kY;
        across = kX;
        break;
    case view::NavigationMove::Down:
        along = kY;
        across = kX;
        break;
    case view::NavigationMove::Left:
        along = -kX;
        across = kY;
        break;
    case view::NavigationMove::Right:
        along = kX;
        across = kY;
        break;
    case view::NavigationMove::Next:
    case view::NavigationMove::Previous:
        return std::nullopt;
    }
    if (along <= 0) {
        return std::nullopt;
    }
    return along + 2 * std::abs(across);
}

} // namespace

void WorldUi::State::letGo(bool shown) {
    Entry* holding = focus.has_value() ? focus->entry : nullptr;
    focus.reset();
    caret.reset();
    // A screen reader follows focus (D573).
    if (holding != nullptr) {
        static_cast<void>(tree->focus(std::nullopt, false));
    }
    if (holding != nullptr && shown) {
        static_cast<void>(giveFocusLook(*holding));
    }
}

[[nodiscard]] Entry* WorldUi::State::entryOf(ui::Node node) noexcept {
    for (ViewState& view : views) {
        for (auto& [kKey, entry] : view.entries) {
            if (entry.node == node) {
                return &entry;
            }
        }
    }
    return nullptr;
}

void WorldUi::State::toggle(Entry& entry) {
    ++statistics.toggled;
    if (entry.value.check != 3) {
        entry.checkedNow = !entry.checkedNow;
        return;
    }
    entry.checkedNow = true;
    for (ViewState& view : views) {
        for (auto& [kKey, other] : view.entries) {
            if (&other != &entry && other.value.check == 3 && other.parent == entry.parent) {
                other.checkedNow = false;
            }
        }
    }
}

void WorldUi::State::point(ui::PointerEvent::Action action, float x, float y, bool holding) {
    if (tree->contains(window) &&
        tree->point(window, ui::PointerEvent{.action = action, .x = x, .y = y, .held = holding, .seconds = seconds})
            .value_or(false)) {
        ++statistics.ranged;
    }
}

void WorldUi::State::giveStates() {
    Entry* hovered = nullptr;
    if (pointer.has_value()) {
        const auto kHit = tree->hit(window, (*pointer)[0], (*pointer)[1]);
        if (kHit.has_value() && kHit->node.has_value()) {
            hovered = entryOf(*kHit->node);
        }
    }
    for (ViewState& view : views) {
        for (auto& [kKey, entry] : view.entries) {
            const ui::States kNow{.focused = focus.has_value() && focus->entry == &entry,
                                  .hovered = &entry == hovered,
                                  .pressed = &entry == pressed,
                                  .disabled = entry.value.disabled != 0,
                                  .checked = entry.checkedNow};
            if (entry.node.has_value() && kNow != entry.states && tree->setStates(*entry.node, kNow).has_value()) {
                entry.states = kNow;
            }
        }
    }
}

void WorldUi::State::endTyping() {
    if (!focus.has_value() || !focus->navigated) {
        letGo();
        return;
    }
    focus->keyboard = false;
    focus->edit.reset();
    caret.reset();
    static_cast<void>(giveFocusLook(*focus->entry));
}

void WorldUi::State::placeCaret() {
    caret.reset();
    if (!focus.has_value() || focus->edit == nullptr) {
        return;
    }
    const auto kPlace = tree->placeOf(window, focus->edit->node());
    const auto kCaret = focus->edit->caretRect();
    if (kPlace.has_value() && kCaret.has_value()) {
        caret = view::UiTyping::Caret{kPlace->x + kCaret->x, kPlace->y + kCaret->y, kCaret->width, kCaret->height};
    }
}

void WorldUi::State::focusOn(Entry& entry, bool keyboard, bool navigated) {
    if (!focus.has_value() || focus->entry != &entry) {
        letGo();
        focus = Focus{.entry = &entry, .navigated = navigated, .keyboard = false, .edit = nullptr};
        static_cast<void>(tree->focus(*entry.node, navigated));
    }
    if (keyboard && entry.editable && !focus->keyboard) {
        focus->keyboard = true;
        ++statistics.focused;
    }
    static_cast<void>(giveFocusLook(entry));
    if (focus->keyboard && focus->edit == nullptr) {
        focus->edit = std::make_unique<ui::TextEdit>(*tree, *entry.node, editSettingsOf(entry.value));
    }
}

std::vector<WorldUi::State::Reachable> WorldUi::State::reachable(bool fields) {
    std::vector<Reachable> found;
    for (ViewState& view : views) {
        found.clear();
        bool holds = false;
        for (auto& [kKey, entry] : view.entries) {
            holds = holds || (focus.has_value() && &entry == focus->entry);
            // A disabled node is passed by (D624).
            if (!entry.node.has_value() || entry.value.disabled != 0 ||
                !(entry.editable || (!fields && entry.value.press != 0))) {
                continue;
            }
            const auto kPlace = tree->placeOf(window, *entry.node);
            if (!kPlace.has_value()) {
                continue;
            }
            const ui::Rect kSize = tree->rectOf(*entry.node);
            found.push_back(
                Reachable{.rect = {kPlace->x, kPlace->y, kSize.width, kSize.height}, .key = kKey, .entry = &entry});
        }
        if (focus.has_value() ? holds : !found.empty()) {
            break;
        }
    }
    std::ranges::sort(found, [](const Reachable& left, const Reachable& right) {
        return std::tie(left.rect.y, left.rect.x, left.key) < std::tie(right.rect.y, right.rect.x, right.key);
    });
    return found;
}

bool WorldUi::State::moveFocus(view::NavigationMove move, bool fields) {
    if (!focus.has_value()) {
        return false;
    }
    const std::vector<Reachable> kFound = reachable(fields);
    const auto kHere = std::ranges::find(kFound, focus->entry, &Reachable::entry);
    if (kHere == kFound.end() || kFound.size() < 2) {
        return false;
    }
    const auto kAt = static_cast<std::size_t>(kHere - kFound.begin());
    const Reachable* next = nullptr;
    if (move == view::NavigationMove::Next) {
        next = &kFound[(kAt + 1) % kFound.size()];
    } else if (move == view::NavigationMove::Previous) {
        next = &kFound[(kAt + kFound.size() - 1) % kFound.size()];
    } else {
        std::optional<float> nearest;
        for (const Reachable& kEach : kFound) {
            const std::optional<float> kDistance = distanceOf(kHere->rect, kEach.rect, move);
            if (kDistance.has_value() && (!nearest.has_value() || *kDistance < *nearest)) {
                nearest = kDistance;
                next = &kEach;
            }
        }
        if (next == nullptr) {
            return false;
        }
    }
    focusOn(*next->entry, fields, !fields || focus->navigated);
    if (fields) {
        static_cast<void>(focus->edit->press(ui::EditKey::SelectAll, {}));
        placeCaret();
    } else {
        ++statistics.navigated;
    }
    return true;
}

std::optional<std::int64_t> WorldUi::press(float x, float y) const {
    const State& state = *state_;
    const auto kHit = state.tree->hit(state.window, x, y);
    if (!kHit.has_value() || !kHit->node.has_value() || kHit->passThrough) {
        return std::nullopt;
    }
    for (const ViewState& kView : state.views) {
        for (const auto& [kKey, kEntry] : kView.entries) {
            if (kEntry.node == kHit->node) {
                // A disabled node blocks and says nothing (D624).
                return kEntry.value.disabled != 0 ? 0 : kEntry.value.press;
            }
        }
    }
    // Blocked, by a node that says nothing.
    return std::int64_t{0};
}

void WorldUi::wheelAt(float x, float y, float deltaX, float deltaY) {
    State& state = *state_;
    if (state.tree->contains(state.window) &&
        state.tree->wheel(state.window, x, y, deltaX, deltaY, state.seconds).value_or(false)) {
        ++state.statistics.wheeled;
    }
}

void WorldUi::pressAt(float x, float y) {
    State& state = *state_;
    const auto kHit = state.tree->hit(state.window, x, y);
    Entry* entry = kHit.has_value() && kHit->node.has_value() ? state.entryOf(*kHit->node) : nullptr;
    // A disabled node takes it and does nothing with it (D624), nor does
    // the thumb of a disabled range.
    const auto kDisabled = [&state](const Entry& pressed) {
        if (pressed.value.disabled != 0) {
            return true;
        }
        if (pressed.value.thumb == 0) {
            return false;
        }
        for (const ViewState& kView : state.views) {
            for (const auto& [kKey, kOther] : kView.entries) {
                if (kOther.node.has_value() && keyOf(*kOther.node) == pressed.parent) {
                    return kOther.value.disabled != 0;
                }
            }
        }
        return false;
    };
    if (entry != nullptr && kDisabled(*entry)) {
        state.letGo();
        state.pressed = nullptr;
        return;
    }
    state.pressed = entry;
    // A range, or its thumb, takes the press: paged toward it, then
    // dragged as the pointer moves until it is let go (D621).
    if (entry != nullptr && (entry->value.range != 0 || entry->value.thumb != 0)) {
        state.letGo();
        state.ranging = true;
        state.point(ui::PointerEvent::Action::Press, x, y, true);
        return;
    }
    if (entry != nullptr && entry->editable) {
        state.focusOn(*entry, true, false);
        static_cast<void>(state.focus->edit->pointAt(kHit->x, kHit->y, false));
        state.placeCaret();
        return;
    }
    state.letGo();
}

void WorldUi::release() {
    State& state = *state_;
    // A checkable node let go of where it was pressed is clicked (D622).
    if (state.pressed != nullptr && state.pressed->value.check != 0 && state.pressed->node.has_value() &&
        state.pointer.has_value()) {
        const auto kHit = state.tree->hit(state.window, (*state.pointer)[0], (*state.pointer)[1]);
        if (kHit.has_value() && kHit->node == state.pressed->node) {
            state.toggle(*state.pressed);
        }
    }
    state.pressed = nullptr;
    if (state.ranging) {
        state.ranging = false;
        const std::array<float, 2> kAt = state.pointer.value_or(std::array<float, 2>{});
        state.point(ui::PointerEvent::Action::Release, kAt[0], kAt[1], false);
        // Gone from the ranges, so none stays hovered by a pointer only
        // they were told of.
        state.point(ui::PointerEvent::Action::Leave, kAt[0], kAt[1], false);
    }
}

void WorldUi::hoverAt(std::optional<std::array<float, 2>> pointer) {
    State& state = *state_;
    if (state.ranging && pointer.has_value() && pointer != state.pointer) {
        state.point(ui::PointerEvent::Action::Move, (*pointer)[0], (*pointer)[1], true);
    }
    state.pointer = pointer;
}

std::optional<float> WorldUi::State::keptFor(const Entry& entry) {
    if (entry.component >= kept.size() || !kept[entry.component].has_value()) {
        return std::nullopt;
    }
    ++statistics.keptGiven;
    return kept[entry.component];
}

void WorldUi::State::noteKept() {
    // The first local player's: one machine keeps one value a node, which
    // every local player's starts as.
    if (views.empty()) {
        return;
    }
    for (const auto& [kKey, kEntry] : views.front().entries) {
        if (!kEntry.node.has_value() || kEntry.value.kept == 0 || kEntry.component >= kept.size()) {
            continue;
        }
        std::optional<float> now;
        if (kEntry.checkGiven.has_value()) {
            now = kEntry.checkedNow ? 1.0F : 0.0F;
        } else if (kEntry.ranged.has_value()) {
            if (const std::optional<ui::Range> kHeld = tree->rangeOf(*kEntry.node)) {
                now = kHeld->value;
            }
        }
        std::optional<float>& noted = kept[kEntry.component];
        if (!now.has_value() || noted == now) {
            continue;
        }
        // What a node starts as is no change of the player's.
        if (noted.has_value()) {
            ++statistics.kept;
        }
        noted = now;
    }
}

std::span<const std::optional<float>> WorldUi::kept() const noexcept {
    return state_->kept;
}

std::optional<float> WorldUi::valueOf(std::size_t player, std::int64_t press) const {
    const State& state = *state_;
    if (player >= state.views.size() || press == 0) {
        return std::nullopt;
    }
    for (const auto& [kKey, kEntry] : state.views[player].entries) {
        if (kEntry.value.press != press) {
            continue;
        }
        // A checkable node's: one checked, nought not (D622).
        if (kEntry.value.check != 0) {
            return kEntry.checkedNow ? 1.0F : 0.0F;
        }
        if (kEntry.value.range == 0) {
            continue;
        }
        if (kEntry.node.has_value()) {
            if (const std::optional<ui::Range> kHeld = state.tree->rangeOf(*kEntry.node)) {
                return kHeld->value;
            }
        }
        return kEntry.value.rangeValue;
    }
    return std::nullopt;
}

void WorldUi::type(const view::Typing& typing) {
    State& state = *state_;
    // A chord is a tool's shortcut (D777); a game's field takes none.
    if (!state.focus.has_value() || state.focus->edit == nullptr || typing.kind == view::Typing::Kind::Chord) {
        return;
    }
    ui::TextEdit& edit = *state.focus->edit;
    const ui::NodeFields& kField = state.focus->entry->value;
    ++state.statistics.typed;
    const std::optional<view::TypingKey> kLeft = view::edit(edit, typing);
    if (!kLeft.has_value()) {
        return;
    }
    if (*kLeft == view::TypingKey::Dismiss) {
        state.endTyping();
        return;
    }
    if (*kLeft == view::TypingKey::Next) {
        static_cast<void>(
            state.moveFocus(typing.extend ? view::NavigationMove::Previous : view::NavigationMove::Next, true));
        return;
    }
    // Enter: a line break in a field of lines, else the text given.
    if (kField.edit == 3) {
        static_cast<void>(edit.type("\n"));
        return;
    }
    state.submitted.push_back(
        view::Submitted{.press = kField.press, .text = std::string{state.tree->textOf(edit.node())}});
    ++state.statistics.submitted;
    if (kField.edit == 2) {
        // A message's field is emptied for the next.
        static_cast<void>(edit.press(ui::EditKey::SelectAll, {}));
        static_cast<void>(edit.type({}));
        return;
    }
    state.endTyping();
}

bool WorldUi::enterNavigation() {
    State& state = *state_;
    if (!state.focus.has_value()) {
        const std::vector<State::Reachable> kFound = state.reachable(false);
        if (kFound.empty()) {
            return false;
        }
        state.focusOn(*kFound.front().entry, false, true);
        ++state.statistics.navigated;
    }
    return true;
}

std::size_t WorldUi::reachable() {
    return state_->reachable(false).size();
}

bool WorldUi::navigate(view::NavigationMove move) {
    State& state = *state_;
    // A slider holding focus takes the moves along its axis, a step toward
    // each, as ARIA's slider does; the others move focus on (D621).
    if (state.focus.has_value() && state.focus->entry->node.has_value() && state.focus->entry->value.range != 0 &&
        state.focus->entry->value.disabled == 0) {
        using Move = view::NavigationMove;
        const bool kVertical = state.focus->entry->value.range == 2;
        const bool kAlong =
            kVertical ? move == Move::Up || move == Move::Down : move == Move::Left || move == Move::Right;
        if (kAlong) {
            if (state.tree->stepRange(*state.focus->entry->node, move == Move::Right || move == Move::Up)) {
                ++state.statistics.ranged;
            }
            return true;
        }
    }
    return state.moveFocus(move, false);
}

std::optional<std::int64_t> WorldUi::activate() {
    State& state = *state_;
    if (!state.focus.has_value()) {
        return std::nullopt;
    }
    Entry& entry = *state.focus->entry;
    if (entry.value.disabled != 0) {
        return std::nullopt;
    }
    if (entry.editable) {
        state.focusOn(entry, true, state.focus->navigated);
        static_cast<void>(state.focus->edit->press(ui::EditKey::End, {}));
        state.placeCaret();
        return std::nullopt;
    }
    ++state.statistics.activated;
    if (entry.value.check != 0) {
        state.toggle(entry);
    }
    return entry.value.press;
}

std::optional<std::int64_t> WorldUi::ask(const ui::AccessRequest& request) {
    State& state = *state_;
    for (const State::Reachable& kFound : state.reachable(false)) {
        if (kFound.entry->node != request.node) {
            continue;
        }
        if (request.kind == ui::AccessRequest::Kind::Select || request.kind == ui::AccessRequest::Kind::Replace) {
            // A field's edit, the field given the keyboard first; what
            // its own typing refuses, as text past its most, is left out.
            if (!kFound.entry->editable) {
                return std::nullopt;
            }
            state.focusOn(*kFound.entry, true, true);
            ui::TextEdit& edit = *state.focus->edit;
            if (!edit.select(request.anchor, request.focus).has_value()) {
                return std::nullopt;
            }
            if (request.kind == ui::AccessRequest::Kind::Replace) {
                ++state.statistics.typed;
                static_cast<void>(edit.type(request.text));
            }
            state.placeCaret();
            return std::nullopt;
        }
        state.focusOn(*kFound.entry, false, true);
        ++state.statistics.navigated;
        return request.kind == ui::AccessRequest::Kind::Press ? activate() : std::nullopt;
    }
    return std::nullopt;
}

void WorldUi::dismiss() {
    State& state = *state_;
    if (state.focus.has_value() && state.focus->keyboard) {
        state.endTyping();
        return;
    }
    state.letGo();
}

bool WorldUi::navigating() const noexcept {
    return state_->focus.has_value();
}

std::optional<view::UiTyping::Caret> WorldUi::caret() const noexcept {
    return state_->caret;
}

std::vector<view::Submitted> WorldUi::takeSubmitted() {
    return std::exchange(state_->submitted, {});
}

} // namespace rawframe::world_ui
