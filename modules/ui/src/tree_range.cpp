// Ranges (SPEC-0030's UiRangeValue and UiDragValue, D621): Maul UI's value
// ranges, and the pointer's events that move them.

#include "rawframe/ui/tree.h"
#include "tree_state.h"

#include <cmath>
#include <cstdint>
#include <maul-ui/event.h>
#include <maul-ui/pointer.h>
#include <maul-ui/range.h>

namespace rawframe::ui {

namespace {

/// The one pointer the owner's events are: a mouse's, or a touch's.
constexpr std::uint32_t kPointer = 1;

} // namespace

result::Status Tree::setRange(Node node, const Range& range) {
    muiValueRange made = muiDefaultValueRange();
    made.minimum = range.minimum;
    made.maximum = range.maximum;
    made.value = range.value;
    made.step = range.step;
    made.page = range.page;
    made.axis = range.vertical ? mui_rangeVertical : mui_rangeHorizontal;
    made.thumb = range.thumb.has_value() ? idOf(*range.thumb) : muiNodeId{};
    return checked(muiNode_SetValueRange(state_->context, idOf(node), &made), "a UI node's range was refused");
}

result::Status Tree::clearRange(Node node) {
    return checked(muiNode_ClearValueRange(state_->context, idOf(node)), "a UI node's range could not be cleared");
}

std::optional<Range> Tree::rangeOf(Node node) const {
    muiValueRange held{};
    if (muiNode_GetValueRange(state_->context, idOf(node), &held) != mui_success) {
        return std::nullopt;
    }
    Range range{.minimum = held.minimum,
                .maximum = held.maximum,
                .value = held.value,
                .step = held.step,
                .page = held.page,
                .vertical = held.axis == mui_rangeVertical,
                .thumb = std::nullopt};
    if (held.thumb.index1 != 0) {
        range.thumb = Node{.index1 = held.thumb.index1, .generation = held.thumb.generation};
    }
    return range;
}

result::Result<bool> Tree::point(Node root, const PointerEvent& event) {
    constexpr muiPointerAction kActions[] = {
        mui_pointerMove, mui_pointerPress, mui_pointerRelease, mui_pointerCancel, mui_pointerLeave};
    const muiPointerEvent kEvent{.timeNs = event.seconds > 0 ? static_cast<std::uint64_t>(event.seconds * 1e9) : 0,
                                 .pointer = kPointer,
                                 .kind = mui_pointerMouse,
                                 .action = kActions[static_cast<std::size_t>(event.action)],
                                 .button = mui_buttonPrimary,
                                 .buttons = static_cast<muiPointerButtons>(event.held ? 1U << mui_buttonPrimary : 0U),
                                 .x = event.x,
                                 .y = event.y,
                                 .player = 0};
    RAWFRAME_TRY(checked(muiPointerInput(state_->context, idOf(root), &kEvent), "a pointer's event was refused"));
    // Each record routed now, so a press pages and a drag moves its range
    // before the next layout.
    bool taken = false;
    muiPointerRecord record{};
    while (muiNextPointerRecord(state_->context, &record) == mui_success) {
        bool handled = false;
        RAWFRAME_TRY(
            checked(muiDispatchPointerRecord(state_->context, &record, &handled), "a pointer's record was refused"));
        taken = taken || handled;
    }
    return taken;
}

} // namespace rawframe::ui
