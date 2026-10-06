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
                                  .pressed = &entry == pressed};
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
            if (!entry.node.has_value() || !(entry.editable || (!fields && entry.value.press != 0))) {
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
                return kEntry.value.press;
            }
        }
    }
    // Blocked, by a node that says nothing.
    return std::int64_t{0};
}

void WorldUi::pressAt(float x, float y) {
    State& state = *state_;
    const auto kHit = state.tree->hit(state.window, x, y);
    Entry* entry = kHit.has_value() && kHit->node.has_value() ? state.entryOf(*kHit->node) : nullptr;
    state.pressed = entry;
    if (entry != nullptr && entry->editable) {
        state.focusOn(*entry, true, false);
        static_cast<void>(state.focus->edit->pointAt(kHit->x, kHit->y, false));
        state.placeCaret();
        return;
    }
    state.letGo();
}

void WorldUi::release() {
    state_->pressed = nullptr;
}

void WorldUi::hoverAt(std::optional<std::array<float, 2>> pointer) {
    state_->pointer = pointer;
}

void WorldUi::type(const view::Typing& typing) {
    State& state = *state_;
    if (!state.focus.has_value() || state.focus->edit == nullptr) {
        return;
    }
    ui::TextEdit& edit = *state.focus->edit;
    const Node& kField = state.focus->entry->value;
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

bool WorldUi::navigate(view::NavigationMove move) {
    return state_->moveFocus(move, false);
}

std::optional<std::int64_t> WorldUi::activate() {
    State& state = *state_;
    if (!state.focus.has_value()) {
        return std::nullopt;
    }
    Entry& entry = *state.focus->entry;
    if (entry.editable) {
        state.focusOn(entry, true, state.focus->navigated);
        static_cast<void>(state.focus->edit->press(ui::EditKey::End, {}));
        state.placeCaret();
        return std::nullopt;
    }
    ++state.statistics.activated;
    return entry.value.press;
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
