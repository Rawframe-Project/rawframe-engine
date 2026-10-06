#include "message_doors.h"

#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <iterator>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCountGives = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 1> kReadTakes = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 2> kTerminateTakes = {kest::Parameter{kest::Slot::Value, kEntityType},
                                                            kest::Parameter{kest::Slot::Text}};

} // namespace

result::Result<std::unique_ptr<MessageDoors>>
MessageDoors::create(const GameDescription& game, const kest::Program& program, Role role) {
    std::unique_ptr<MessageDoors> made{new MessageDoors{role}};
    for (std::size_t index = 0; index < game.messages.size(); ++index) {
        const GameMessage& message = game.messages[index];
        const auto kLayout = program.layout(message.kestType);
        if (!kLayout.has_value() || holdsEntity(*kLayout) || kLayout->size > network::kMaximumEventRecord) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "a message's type is one the program lays out, of at most 64 KiB, holding no entity")
                    .error()
                    .withContext("message", message.name)};
        }
        auto& kind = *made->kinds_.emplace_back(std::make_unique<Kind>());
        kind.owner = made.get();
        kind.kind = static_cast<std::uint32_t>(index);
        kind.size = kLayout->size;
        kind.kestType = message.kestType;
        kind.send = "Messages." + message.name;
        kind.count = "ReceivedCount." + message.name;
        kind.read = "Received." + message.name;
        made->largest_ = std::max({made->largest_, kLayout->size, std::size_t{1}});
    }
    // The type names the doors take and give point into each kind, which
    // stays where it is.
    for (const std::unique_ptr<Kind>& kind : made->kinds_) {
        kind->sendTakes[1] = kest::Parameter{kest::Slot::Value, kind->kestType};
        kind->readGives[0] = kest::Parameter{kest::Slot::Value, kind->kestType};
    }
    return made;
}

result::Status MessageDoors::addDoors(kest::DoorTable& doors) {
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "Players.terminate", .function = &terminateDoor, .context = this, .takes = kTerminateTakes}));
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->send, .function = &sendDoor, .context = kind.get(), .takes = kind->sendTakes}));
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->count, .function = &countDoor, .context = kind.get(), .gives = kCountGives}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = kind->read,
                                          .function = &readDoor,
                                          .context = kind.get(),
                                          .takes = kReadTakes,
                                          .gives = kind->readGives}));
    }
    return {};
}

void MessageDoors::begin(world::TickIndex /*tick*/) noexcept {
    pending_.clear();
    pendingTerminations_.clear();
}

void MessageDoors::end(bool kept) noexcept {
    if (kept) {
        std::ranges::move(pending_, std::back_inserter(kept_));
        std::ranges::move(pendingTerminations_, std::back_inserter(keptTerminations_));
    }
    pending_.clear();
    pendingTerminations_.clear();
}

void MessageDoors::takeTerminations(std::vector<world_replication::PostedTermination>& into) {
    std::ranges::move(keptTerminations_, std::back_inserter(into));
    keptTerminations_.clear();
}

void MessageDoors::terminateDoor(kest::DoorCall& call, void* context) noexcept {
    MessageDoors& owner = *static_cast<MessageDoors*>(context);
    if (owner.role_ != Role::Send) {
        return;
    }
    world::EntityHandle player;
    const std::string_view kNote = call.text(1);
    if (!call.value(0, std::as_writable_bytes(std::span{&player, 1})) || player.isNull()) {
        call.fail("a session is ended for a player that exists");
        return;
    }
    if (kNote.size() > network::kMaximumTerminationNote) {
        call.fail("a termination's note is at most 256 bytes");
        return;
    }
    if (owner.pendingTerminations_.size() + owner.keptTerminations_.size() >= kMaximumMessagesPerTick) {
        call.fail("a tick's systems end at most 1024 sessions");
        return;
    }
    call.spendFuel(kNote.size());
    owner.pendingTerminations_.push_back(
        world_replication::PostedTermination{.player = player, .note = std::string{kNote}});
}

void MessageDoors::take(std::vector<world_replication::PostedMessage>& into) {
    std::ranges::move(kept_, std::back_inserter(into));
    kept_.clear();
}

void MessageDoors::arrived(std::span<const world_replication::ReceivedMessage> messages) {
    arrived_.assign(messages.begin(), messages.end());
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        kind->arrived.clear();
    }
    for (const world_replication::ReceivedMessage& message : arrived_) {
        // A value not the size of its kind is not that kind's: left unread.
        if (message.kind < kinds_.size() && message.value.size() == kinds_[message.kind]->size) {
            kinds_[message.kind]->arrived.emplace_back(message.value);
        }
    }
}

void MessageDoors::sendDoor(kest::DoorCall& call, void* context) noexcept {
    Kind& kind = *static_cast<Kind*>(context);
    MessageDoors& owner = *kind.owner;
    if (owner.role_ != Role::Send) {
        return;
    }
    world::EntityHandle to;
    if (!call.value(0, std::as_writable_bytes(std::span{&to, 1})) || to.isNull()) {
        call.fail("a message goes to a player that exists");
        return;
    }
    if (owner.pending_.size() + owner.kept_.size() >= kMaximumMessagesPerTick) {
        call.fail("a tick's systems send at most 1024 messages");
        return;
    }
    world_replication::PostedMessage message{.player = to, .kind = kind.kind, .value = {}};
    message.value.resize(kind.size);
    if (!call.value(1, message.value)) {
        call.fail("a message's value did not cross as its type");
        return;
    }
    call.spendFuel(kind.size);
    owner.pending_.push_back(std::move(message));
}

void MessageDoors::countDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    call.answerInteger(static_cast<std::int64_t>(kind.arrived.size()));
}

void MessageDoors::readDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    const std::int64_t kIndex = call.integer(0);
    if (kIndex < 0 || static_cast<std::uint64_t>(kIndex) >= kind.arrived.size()) {
        call.fail("no message of this kind arrived at that index");
        return;
    }
    if (!call.answerValue(kind.arrived[static_cast<std::size_t>(kIndex)])) {
        call.fail("the program's message type is not the one it was sent as");
        return;
    }
    call.spendFuel(kind.size);
}

} // namespace rawframe::world_kest
