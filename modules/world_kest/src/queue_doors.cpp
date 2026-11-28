#include "queue_doors.h"

#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 1> kCountGives = {kest::Parameter{kest::Slot::I32}};
constexpr std::array<kest::Parameter, 1> kReadTakes = {kest::Parameter{kest::Slot::I32}};

} // namespace

result::Result<std::unique_ptr<QueueDoors>> QueueDoors::create(const GameDescription& game,
                                                               const kest::Program& program) {
    std::unique_ptr<QueueDoors> made{new QueueDoors{}};
    for (const GameQueue& queue : game.queues) {
        const auto kLayout = program.layout(queue.kestType);
        if (!kLayout.has_value() || holdsEntity(*kLayout) || kLayout->size == 0 ||
            kLayout->size > world::kMaximumQueuedValueBytes) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::BadGameLine),
                             "a queue's type is one the program lays out, of 1 to 4096 bytes, holding no entity")
                    .error()
                    .withContext("queue", queue.name)};
        }
        auto& kind = *made->kinds_.emplace_back(std::make_unique<Kind>());
        kind.owner = made.get();
        kind.declaration = world::QueueDeclaration{
            .name = queue.name, .valueBytes = kLayout->size, .lasts = queue.lasts, .holds = queue.holds, .readers = {}};
        for (const GameSystem& system : game.systems) {
            if (std::ranges::contains(system.reads, queue.name)) {
                kind.declaration.readers.push_back(system.identity);
            }
            if (std::ranges::contains(system.writes, queue.name)) {
                kind.writers.push_back(system.identity);
            }
        }
        kind.kestType = queue.kestType;
        kind.write = "Queue." + queue.name;
        kind.count = "QueuedCount." + queue.name;
        kind.read = "Queued." + queue.name;
    }
    // The type names the doors take and give point into each kind, which
    // stays where it is.
    for (const std::unique_ptr<Kind>& kind : made->kinds_) {
        kind->writeTakes[0] = kest::Parameter{kest::Slot::Value, kind->kestType};
        kind->readGives[0] = kest::Parameter{kest::Slot::Value, kind->kestType};
    }
    return made;
}

result::Status QueueDoors::addDoors(kest::DoorTable& doors) {
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        RAWFRAME_TRY(doors.add(
            kest::Door{.name = kind->write, .function = &writeDoor, .context = kind.get(), .takes = kind->writeTakes}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = kind->count,
                                          .function = &countDoor,
                                          .context = kind.get(),
                                          .gives = kCountGives,
                                          .safeForUntrusted = true}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = kind->read,
                                          .function = &readDoor,
                                          .context = kind.get(),
                                          .takes = kReadTakes,
                                          .gives = kind->readGives,
                                          .safeForUntrusted = true}));
    }
    return {};
}

std::vector<world::QueueDeclaration> QueueDoors::declarations() const {
    std::vector<world::QueueDeclaration> declared;
    declared.reserve(kinds_.size());
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        declared.push_back(kind->declaration);
    }
    return declared;
}

void QueueDoors::begin(world::SystemContext& context) noexcept {
    context_ = &context;
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        kind->pending.clear();
    }
}

void QueueDoors::end(bool kept) noexcept {
    if (context_ != nullptr && kept) {
        const std::string_view kSystem = context_->system;
        for (const std::unique_ptr<Kind>& kind : kinds_) {
            world::MessageQueue* const kQueue = context_->world.queues().find(kind->declaration.name);
            if (kQueue == nullptr) {
                continue;
            }
            // Read before its own writes join, so a system that reads and
            // writes a queue reads its own next tick.
            kQueue->markRead(kSystem);
            if (!kind->pending.empty()) {
                // The write door kept the queue within what it holds.
                static_cast<void>(kQueue->write(context_->tick, kind->pending));
            }
        }
    }
    for (const std::unique_ptr<Kind>& kind : kinds_) {
        kind->pending.clear();
    }
    context_ = nullptr;
}

world::MessageQueue* QueueDoors::queueOf(const Kind& kind) const noexcept {
    return context_ == nullptr ? nullptr : context_->world.queues().find(kind.declaration.name);
}

void QueueDoors::writeDoor(kest::DoorCall& call, void* context) noexcept {
    Kind& kind = *static_cast<Kind*>(context);
    QueueDoors& owner = *kind.owner;
    world::MessageQueue* const kQueue = owner.queueOf(kind);
    if (kQueue == nullptr || !std::ranges::contains(kind.writers, owner.context_->system)) {
        call.fail("a queue is written only by a system whose line writes it");
        return;
    }
    const std::size_t kBytes = kind.declaration.valueBytes;
    if (kQueue->messages().size() + (kind.pending.size() / kBytes) >= kind.declaration.holds) {
        call.fail("the queue holds as many messages as it may");
        return;
    }
    const std::size_t kAt = kind.pending.size();
    kind.pending.resize(kAt + kBytes);
    if (!call.value(0, std::span{kind.pending}.subspan(kAt, kBytes))) {
        kind.pending.resize(kAt);
        call.fail("the program's value is not the queue's type");
    }
}

void QueueDoors::countDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    const QueueDoors& owner = *kind.owner;
    const world::MessageQueue* const kQueue = owner.queueOf(kind);
    if (kQueue == nullptr || !std::ranges::contains(kind.declaration.readers, owner.context_->system)) {
        call.fail("a queue is read only by a system whose line reads it");
        return;
    }
    call.answerInteger(static_cast<std::int64_t>(kQueue->unread(owner.context_->system).size()));
}

void QueueDoors::readDoor(kest::DoorCall& call, void* context) noexcept {
    const Kind& kind = *static_cast<const Kind*>(context);
    const QueueDoors& owner = *kind.owner;
    const world::MessageQueue* const kQueue = owner.queueOf(kind);
    if (kQueue == nullptr || !std::ranges::contains(kind.declaration.readers, owner.context_->system)) {
        call.fail("a queue is read only by a system whose line reads it");
        return;
    }
    const auto kUnread = kQueue->unread(owner.context_->system);
    const std::int64_t kIndex = call.integer(0);
    if (kIndex < 0 || static_cast<std::uint64_t>(kIndex) >= kUnread.size()) {
        call.fail("no unread message at that place");
        return;
    }
    if (!call.answerValue(kUnread[static_cast<std::size_t>(kIndex)].value)) {
        call.fail("the program's value is not the queue's type");
    }
}

} // namespace rawframe::world_kest
