#include "ui_doors.h"

#include <array>
#include <cstddef>
#include <span>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kGivesCode = {kest::Parameter{kest::Slot::I64}};
constexpr std::array<kest::Parameter, 1> kTakesCode = {kest::Parameter{kest::Slot::I64}};
constexpr std::array<kest::Parameter, 1> kGivesValue = {kest::Parameter{kest::Slot::F32}};
constexpr std::array<kest::Parameter, 1> kGivesSides = {kest::Parameter{kest::Slot::Value, "rawframe.ui.SafeArea"}};

void hoveredDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const UiDoorContext*>(context);
    call.answerInteger(kContext != nullptr ? kContext->hovered : 0);
}

void valueDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const UiDoorContext*>(context);
    const std::int64_t kPress = call.integer(0);
    call.answerReal(kContext != nullptr && kContext->queries != nullptr
                        ? kContext->queries->valueOf(kContext->player, kPress).value_or(0)
                        : 0);
}

void safeAreaDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const UiDoorContext*>(context);
    // rawframe.ui.SafeArea: top, right, bottom, left, each an f32.
    const std::array<float, 4> kSides = kContext != nullptr && kContext->queries != nullptr
                                            ? kContext->queries->safeAreaOf(kContext->player)
                                            : std::array<float, 4>{};
    if (!call.answerValue(std::as_bytes(std::span{kSides}))) {
        call.fail("the program's rawframe.ui.SafeArea is not the engine's");
    }
}

} // namespace

result::Status addUiDoors(kest::DoorTable& doors, const UiDoorContext* context) {
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.hovered",
                                      .function = &hoveredDoor,
                                      .context = const_cast<UiDoorContext*>(context),
                                      .gives = kGivesCode,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.value",
                                      .function = &valueDoor,
                                      .context = const_cast<UiDoorContext*>(context),
                                      .takes = kTakesCode,
                                      .gives = kGivesValue,
                                      .safeForUntrusted = true}));
    return doors.add(kest::Door{.name = "UI.safeArea",
                                .function = &safeAreaDoor,
                                .context = const_cast<UiDoorContext*>(context),
                                .gives = kGivesSides,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
