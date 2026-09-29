#include "message_doors.h"

#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <iterator>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCountGives = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 1> kReadTakes = {kest::Parameter{kest::Slot::I32}};

/// Whether a layout holds an entity: `rawframe.world.Entity`'s two pieces,
/// `slot` and then `generation` four bytes on, under one field's name or
/// none.
bool holdsEntity(const kest::TypeLayout& layout) {
    for (std::size_t index = 0; index + 1 < layout.fields.size(); ++index) {
        const kest::Field& kSlot = layout.fields[index];
        const kest::Field& kGeneration = layout.fields[index + 1];
        if (!kSlot.name.ends_with("slot") || kGeneration.offset != kSlot.offset + 4) {
            continue;
        }
        const std::string_view kOwner = std::string_view{kSlot.name}.substr(0, kSlot.name.size() - 4);
        if ((kOwner.empty() || kOwner.ends_with('.')) && kGeneration.name == std::string{kOwner} + "generation") {
            return true;
        }
    }
    return false;
}

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

void MessageDoors::begin() noexcept {
    pending_.clear();
}

void MessageDoors::end(bool kept) noexcept {
    if (kept) {
        std::ranges::move(pending_, std::back_inserter(kept_));
    }
    pending_.clear();
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
