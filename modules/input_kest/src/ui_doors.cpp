#include "rawframe/input_kest/sources.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace rawframe::input_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCodeGives = {kest::Parameter{kest::Slot::I64}};
constexpr std::array<kest::Parameter, 1> kTruthGives = {kest::Parameter{kest::Slot::Bool}};
constexpr std::array<kest::Parameter, 1> kTypedGives = {kest::Parameter{kest::Slot::Value, "rawframe.ui.Typed"}};

void pressedDoor(kest::DoorCall& call, void* context) noexcept {
    call.answerInteger(static_cast<const UiDoorContext*>(context)->pressed);
}

void submittedDoor(kest::DoorCall& call, void* context) noexcept {
    call.answerInteger(static_cast<const UiDoorContext*>(context)->submitted);
}

void typedDoor(kest::DoorCall& call, void* context) noexcept {
    if (!call.answerValue(static_cast<const UiDoorContext*>(context)->typed)) {
        call.fail("the program's rawframe.ui.Typed is not the engine's");
    }
}

void navigateDoor(kest::DoorCall& call, void* context) noexcept {
    const view::UiNavigation* navigation = static_cast<const UiDoorContext*>(context)->navigation;
    call.answerBoolean(navigation != nullptr && navigation->enter());
}

void navigatingDoor(kest::DoorCall& call, void* context) noexcept {
    const view::UiNavigation* navigation = static_cast<const UiDoorContext*>(context)->navigation;
    call.answerBoolean(navigation != nullptr && navigation->focused());
}

void hoveredDoor(kest::DoorCall& call, void* context) noexcept {
    const view::UiPointing* pointing = static_cast<const UiDoorContext*>(context)->pointing;
    call.answerInteger(pointing != nullptr ? pointing->hovered().value_or(0) : 0);
}

} // namespace

result::Status addUiDoors(kest::DoorTable& doors, const UiDoorContext* context) {
    auto* lent = const_cast<UiDoorContext*>(context);
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.pressed",
                                      .function = &pressedDoor,
                                      .context = lent,
                                      .gives = kCodeGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.submitted",
                                      .function = &submittedDoor,
                                      .context = lent,
                                      .gives = kCodeGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "UI.typed", .function = &typedDoor, .context = lent, .gives = kTypedGives, .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.navigate",
                                      .function = &navigateDoor,
                                      .context = lent,
                                      .gives = kTruthGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "UI.navigating",
                                      .function = &navigatingDoor,
                                      .context = lent,
                                      .gives = kTruthGives,
                                      .safeForUntrusted = true}));
    // rawframe.ui's own wrapper asks for it wherever the module is imported.
    return doors.add(kest::Door{.name = "UI.hovered",
                                .function = &hoveredDoor,
                                .context = lent,
                                .gives = kCodeGives,
                                .safeForUntrusted = true});
}

void typedOf(std::string_view text, std::array<std::byte, 256>& into) noexcept {
    std::size_t length = std::min<std::size_t>(text.size(), into.size() - 4);
    // Whole code points: a continuation byte past the end shortens it.
    while (length < text.size() && length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) {
        --length;
    }
    into.fill(std::byte{0});
    const auto kLength = static_cast<std::uint32_t>(length);
    std::memcpy(into.data(), &kLength, sizeof kLength);
    if (length != 0) {
        std::memcpy(into.data() + 4, text.data(), length);
    }
}

} // namespace rawframe::input_kest
