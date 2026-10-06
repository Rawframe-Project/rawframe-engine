#include "rawframe/input_kest/sources.h"

#include <array>
#include <cstdint>

namespace rawframe::input_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCodeGives = {kest::Parameter{kest::Slot::I64}};

void pressedDoor(kest::DoorCall& call, void* context) noexcept {
    call.answerInteger(static_cast<const UiDoorContext*>(context)->pressed);
}

} // namespace

result::Status addUiDoors(kest::DoorTable& doors, const UiDoorContext* context) {
    return doors.add(kest::Door{.name = "UI.pressed",
                                .function = &pressedDoor,
                                .context = const_cast<UiDoorContext*>(context),
                                .gives = kCodeGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::input_kest
