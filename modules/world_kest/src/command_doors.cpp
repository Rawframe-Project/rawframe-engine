#include "command_doors.h"

#include <algorithm>
#include <iterator>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCountGives = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 1> kIndexTakes = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 1> kFromGives = {kest::Parameter{kest::Slot::Value, kEntityType}};

} // namespace

result::Result<std::unique_ptr<CommandDoors>>
CommandDoors::create(const GameDescription& game, const kest::Program& program, Role role) {
    std::unique_ptr<CommandDoors> made{new CommandDoors{role}};
    RAWFRAME_TRY_ASSIGN(const std::vector<CommandKind> kKinds, commandKindsOf(game, program, true));
    for (const CommandKind& each : kKinds) {
        auto& kind = *made->kinds_.emplace_back(std::make_unique<Kind>());
        kind.owner = made.get();
        kind.command = each;
        kind.count = "CommandCount." + each.name;
        kind.read = "Command." + each.name;
        kind.from = "CommandFrom." + each.name;
        made->sizes_.push_back(each.size);
    }
    // The type names the doors give point into each kind, which stays where
    // it is.
    for (const std::unique_ptr<Kind>& kind : made->kinds_) {
        kind->readGives[0] = kest::Parameter{kest::Slot::Value, kind->command.kestType};
    }
    return made;
}

result::Status CommandDoors::addDoors(kest::DoorTable& doors) {
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->count, .function = &countDoor, .context = kind.get(), .gives = kCountGives}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = kind->read,
                                          .function = &readDoor,
                                          .context = kind.get(),
                                          .takes = kIndexTakes,
                                          .gives = kind->readGives}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = kind->from,
                                          .function = &fromDoor,
                                          .context = kind.get(),
                                          .takes = kIndexTakes,
                                          .gives = kFromGives}));
    }
    return {};
}

void CommandDoors::begin(world::TickIndex tick) noexcept {
    if (role_ != Role::Read || tick_ == tick) {
        return;
    }
    // A new tick: what was delivered for it, or for a tick that ran no
    // system, is now readable, and what the last tick read is gone.
    tick_ = tick;
    current_.clear();
    const auto kDue = std::ranges::stable_partition(delivered_, [tick](const auto& each) {
        return each.tick.value <= tick.value;
    });
    current_.assign(std::make_move_iterator(delivered_.begin()), std::make_move_iterator(kDue.begin()));
    delivered_.erase(delivered_.begin(), kDue.begin());
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        kind->current.clear();
    }
    for (const world_replication::ReceivedCommand& each : current_) {
        const auto kKind = std::ranges::find(kinds_, each.kind, [](const std::unique_ptr<Kind>& kind) {
            return kind->command.kind;
        });
        if (kKind != kinds_.end()) {
            (*kKind)->current.push_back(&each);
        }
    }
}

void CommandDoors::end(bool /*kept*/) noexcept {
}

void CommandDoors::deliver(std::span<const world_replication::ReceivedCommand> commands) {
    if (role_ != Role::Read || commands.empty()) {
        return;
    }
    // What no system read by the tick before the newest arriving never will
    // be: a game whose systems do not run cannot hold its players' commands
    // without bound.
    const std::uint64_t kNewest = std::ranges::max(commands, {}, [](const auto& each) {
                                      return each.tick.value;
                                  }).tick.value;
    std::erase_if(delivered_, [kNewest](const world_replication::ReceivedCommand& each) {
        return each.tick.value + 1 < kNewest;
    });
    delivered_.insert(delivered_.end(), commands.begin(), commands.end());
}

const world_replication::ReceivedCommand* CommandDoors::at(kest::DoorCall& call, const Kind& kind) noexcept {
    const std::int64_t kIndex = call.integer(0);
    if (kIndex < 0 || static_cast<std::size_t>(kIndex) >= kind.current.size()) {
        call.fail("a command is read by an index below its count");
        return nullptr;
    }
    return kind.current[static_cast<std::size_t>(kIndex)];
}

void CommandDoors::countDoor(kest::DoorCall& call, void* context) noexcept {
    call.answerInteger(static_cast<std::int64_t>(static_cast<const Kind*>(context)->current.size()));
}

void CommandDoors::readDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    if (const world_replication::ReceivedCommand* command = at(call, kind)) {
        if (!call.answerValue(command->value)) {
            call.fail("the program's command type is not the size the server checked");
        }
    }
}

void CommandDoors::fromDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    if (const world_replication::ReceivedCommand* command = at(call, kind)) {
        if (!call.answerValue(std::as_bytes(std::span{&command->player, 1}))) {
            call.fail("the program's Entity is not the engine's");
        }
    }
}

} // namespace rawframe::world_kest
