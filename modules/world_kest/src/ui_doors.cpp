#include "hover_doors.h"

#include <array>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kGives = {kest::Parameter{kest::Slot::I64}};

void hoveredDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const HoverDoorContext*>(context);
    call.answerInteger(kContext != nullptr ? kContext->hovered : 0);
}

} // namespace

result::Status addHoverDoor(kest::DoorTable& doors, const HoverDoorContext* context) {
    return doors.add(kest::Door{.name = "UI.hovered",
                                .function = &hoveredDoor,
                                .context = const_cast<HoverDoorContext*>(context),
                                .gives = kGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
