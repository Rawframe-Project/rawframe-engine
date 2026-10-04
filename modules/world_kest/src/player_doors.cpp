#include "player_doors.h"

#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <span>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kGives = {kest::Parameter{kest::Slot::Value, kEntityType}};

void playerDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const PlayerDoorContext*>(context);
    const world::EntityHandle kPlayer = kContext != nullptr ? kContext->player : world::EntityHandle{};
    if (!call.answerValue(std::as_bytes(std::span{&kPlayer, 1}))) {
        call.fail("the program's Entity is not the engine's");
    }
}

} // namespace

result::Status addPlayerDoor(kest::DoorTable& doors, const PlayerDoorContext* context) {
    return doors.add(kest::Door{.name = "Replication.player",
                                .function = &playerDoor,
                                .context = const_cast<PlayerDoorContext*>(context),
                                .gives = kGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
