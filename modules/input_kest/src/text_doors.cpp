#include "rawframe/input_kest/sources.h"

#include <array>
#include <cstdint>

namespace rawframe::input_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCountGives = {kest::Parameter{kest::Slot::U32}};
constexpr std::array<kest::Parameter, 1> kChooseTakes = {kest::Parameter{kest::Slot::U32}};
constexpr std::array<kest::Parameter, 1> kChooseGives = {kest::Parameter{kest::Slot::Bool}};

const TextDoorContext& textOf(void* context) noexcept {
    return *static_cast<const TextDoorContext*>(context);
}

void offeredDoor(kest::DoorCall& call, void* context) noexcept {
    const world_localization::GameText* text = textOf(context).text;
    call.answerInteger(text != nullptr ? static_cast<std::int64_t>(text->offered().size()) : 0);
}

void chosenDoor(kest::DoorCall& call, void* context) noexcept {
    const TextDoorContext& lent = textOf(context);
    call.answerInteger(lent.text != nullptr ? static_cast<std::int64_t>(lent.text->chosen(lent.player)) : 0);
}

void chooseDoor(kest::DoorCall& call, void* context) noexcept {
    const TextDoorContext& lent = textOf(context);
    const std::int64_t kIndex = call.integer(0);
    call.answerBoolean(lent.text != nullptr && kIndex >= 0 &&
                       lent.text->choose(lent.player, static_cast<std::size_t>(kIndex)));
}

} // namespace

result::Status addTextDoors(kest::DoorTable& doors, const TextDoorContext* context) {
    auto* lent = const_cast<TextDoorContext*>(context);
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Text.offered",
                                      .function = &offeredDoor,
                                      .context = lent,
                                      .gives = kCountGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Text.chosen",
                                      .function = &chosenDoor,
                                      .context = lent,
                                      .gives = kCountGives,
                                      .safeForUntrusted = true}));
    return doors.add(kest::Door{.name = "Text.choose",
                                .function = &chooseDoor,
                                .context = lent,
                                .takes = kChooseTakes,
                                .gives = kChooseGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::input_kest
