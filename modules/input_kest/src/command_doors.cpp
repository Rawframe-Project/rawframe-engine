#include "rawframe/input_kest/sources.h"

#include <algorithm>

namespace rawframe::input_kest {

namespace {

void sendDoor(kest::DoorCall& call, void* context) noexcept {
    auto& kind = *static_cast<CommandDoorContext::Kind*>(context);
    CommandDoorContext& owner = *kind.owner;
    if (owner.sent.size() >= world_kest::kMaximumCommandsPerTick) {
        call.fail("a sample sends at most 8 commands a tick");
        return;
    }
    world_replication::PostedCommand command{.kind = kind.command.kind,
                                             .value = std::vector<std::byte>(kind.command.size)};
    if (!call.value(0, command.value)) {
        call.fail("the program's command type is not the game's");
        return;
    }
    owner.sent.push_back(std::move(command));
}

} // namespace

result::Status addCommandDoors(kest::DoorTable& doors, CommandDoorContext& context) {
    // The type names the doors take point into each kind, which stays where
    // it is.
    for (const std::unique_ptr<CommandDoorContext::Kind>& kind : context.kinds) {
        kind->owner = &context;
        kind->send = "Commands." + kind->command.name;
        kind->takes[0] = kest::Parameter{kest::Slot::Value, kind->command.kestType};
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->send, .function = &sendDoor, .context = kind.get(), .takes = kind->takes}));
    }
    return {};
}

} // namespace rawframe::input_kest
