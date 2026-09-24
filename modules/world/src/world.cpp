#include "rawframe/world/world.h"

#include "rawframe/world/errors.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace rawframe::world {

namespace {

// A slot whose generation reaches this is retired rather than reused, so no
// generation value is ever live twice (SPEC-0006).
constexpr std::uint32_t kLastGeneration = std::numeric_limits<std::uint32_t>::max();

} // namespace

World::World(std::shared_ptr<const schema::SchemaRegistry> registry, WorldSettings settings)
    : registry_(std::move(registry)), settings_(settings) {
    RAWFRAME_CHECK(registry_ != nullptr, "a World binds a registry");
    RAWFRAME_CHECK(settings_.firstGeneration != 0, "generation 0 is the null handle");
    // Archetype 0 is the empty set, where every entity starts.
    static_cast<void>(findOrCreateArchetype({}));
}

World::~World() = default;

result::Status World::checkStructure() const {
    if (structureLocked_) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            kWorldDomain,
                            code(WorldError::StructureLocked),
                            "structural change while systems run; record a command instead");
    }
    return {};
}

result::Status World::checkLive(EntityHandle entity) const {
    if (!alive(entity)) {
        return result::fail(result::ErrorClass::FailedPrecondition,
                            kWorldDomain,
                            code(WorldError::StaleEntity),
                            "the entity handle is null, stale, or from another World");
    }
    return {};
}

bool World::alive(EntityHandle entity) const noexcept {
    if (entity.isNull() || entity.slot >= records_.size()) {
        return false;
    }
    const EntityRecord& record = records_[entity.slot];
    return record.alive && record.generation == entity.generation;
}

result::Result<EntityHandle> World::create() {
    RAWFRAME_TRY(checkStructure());
    std::uint32_t slot = 0;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else if (records_.size() < settings_.maximumEntities) {
        slot = static_cast<std::uint32_t>(records_.size());
        records_.push_back(EntityRecord{.generation = settings_.firstGeneration});
    } else {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kWorldDomain,
                            code(WorldError::EntityCapacity),
                            "the World has used every entity slot it may");
    }
    EntityRecord& record = records_[slot];
    const EntityHandle kEntity{slot, record.generation};
    record.alive = true;
    record.archetype = 0;
    record.row = static_cast<std::uint32_t>(archetypes_[0]->appendRow(kEntity));
    ++liveEntities_;
    return kEntity;
}

result::Status World::destroy(EntityHandle entity) {
    RAWFRAME_TRY(checkStructure());
    RAWFRAME_TRY(checkLive(entity));
    EntityRecord& record = records_[entity.slot];
    const EntityHandle kMoved = archetypes_[record.archetype]->removeRow(record.row);
    if (!kMoved.isNull()) {
        records_[kMoved.slot].row = record.row;
    }
    record.alive = false;
    --liveEntities_;
    // Invalidate before the slot can be reused; retire it at the last
    // generation.
    if (record.generation != kLastGeneration) {
        ++record.generation;
        freeSlots_.push_back(entity.slot);
    }
    return {};
}

std::uint32_t World::findOrCreateArchetype(std::vector<schema::ComponentRuntimeId> components) {
    if (const auto kFound = archetypeIndex_.find(components); kFound != archetypeIndex_.end()) {
        return kFound->second;
    }
    const auto kIndex = static_cast<std::uint32_t>(archetypes_.size());
    archetypes_.push_back(std::make_unique<detail::Archetype>(components, *registry_));
    archetypeIndex_.emplace(std::move(components), kIndex);
    return kIndex;
}

std::uint32_t World::transition(std::uint32_t from, schema::ComponentRuntimeId component, bool adding) {
    auto& edges = adding ? archetypes_[from]->addEdges : archetypes_[from]->removeEdges;
    for (const auto& edge : edges) {
        if (edge.component == component) {
            return edge.archetype;
        }
    }
    const auto kSource = archetypes_[from]->components();
    std::vector<schema::ComponentRuntimeId> components(kSource.begin(), kSource.end());
    if (adding) {
        components.insert(std::lower_bound(components.begin(), components.end(), component), component);
    } else {
        components.erase(std::lower_bound(components.begin(), components.end(), component));
    }
    // Archetypes live behind unique_ptr, so `edges` survives the list growing.
    const std::uint32_t kTarget = findOrCreateArchetype(std::move(components));
    edges.push_back(detail::Archetype::Edge{component, kTarget});
    return kTarget;
}

std::size_t World::moveEntity(EntityHandle entity, std::uint32_t to) {
    EntityRecord& record = records_[entity.slot];
    detail::Archetype& source = *archetypes_[record.archetype];
    detail::Archetype& target = *archetypes_[to];
    const std::size_t kRow = target.appendRow(entity);
    for (const schema::ComponentRuntimeId kComponent : target.components()) {
        const int kFrom = source.columnIndex(kComponent);
        const int kTo = target.columnIndex(kComponent);
        if (kFrom >= 0 && kTo >= 0) {
            detail::Column& into = target.column(kTo);
            into.moveConstruct(into.at(kRow), source.column(kFrom).at(record.row));
        }
    }
    // Destroys the moved-from values and whatever did not come along.
    const EntityHandle kMoved = source.removeRow(record.row);
    if (!kMoved.isNull()) {
        records_[kMoved.slot].row = record.row;
    }
    record.archetype = to;
    record.row = static_cast<std::uint32_t>(kRow);
    return kRow;
}

result::Status World::insertErased(EntityHandle entity, schema::ComponentRuntimeId component, void* value) {
    RAWFRAME_TRY(checkStructure());
    RAWFRAME_TRY(checkLive(entity));
    const EntityRecord& record = records_[entity.slot];
    detail::Archetype& current = *archetypes_[record.archetype];
    const schema::ComponentDescriptor& descriptor = registry_->descriptor(component);
    if (current.has(component)) {
        if (descriptor.size != 0) {
            detail::Column& column = current.column(current.columnIndex(component));
            column.destroy(column.at(record.row));
            column.moveConstruct(column.at(record.row), value);
        }
        return {};
    }
    const std::uint32_t kTarget = transition(record.archetype, component, true);
    const std::size_t kRow = moveEntity(entity, kTarget);
    if (descriptor.size != 0) {
        detail::Archetype& target = *archetypes_[kTarget];
        detail::Column& column = target.column(target.columnIndex(component));
        column.moveConstruct(column.at(kRow), value);
    }
    return {};
}

result::Status World::removeErased(EntityHandle entity, schema::ComponentRuntimeId component) {
    RAWFRAME_TRY(checkStructure());
    RAWFRAME_TRY(checkLive(entity));
    const EntityRecord& record = records_[entity.slot];
    if (!archetypes_[record.archetype]->has(component)) {
        return {};
    }
    static_cast<void>(moveEntity(entity, transition(record.archetype, component, false)));
    return {};
}

Pcg32& World::randomStream(std::string_view owner, std::string_view name) {
    std::pair<std::string, std::string> key{owner, name};
    if (const auto kFound = randomStreams_.find(key); kFound != randomStreams_.end()) {
        return kFound->second;
    }
    return randomStreams_.emplace(std::move(key), deriveStream(settings_.rootSeed, owner, name)).first->second;
}

std::size_t World::availableSlots() const noexcept {
    return freeSlots_.size() + (settings_.maximumEntities - records_.size());
}

result::Result<CommitReport> World::apply(CommandBuffer& buffer) {
    RAWFRAME_TRY(checkStructure());
    CommitReport report;
    report.created.assign(buffer.pending_, EntityHandle{});
    if (buffer.pending_ > availableSlots()) {
        buffer.clear();
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kWorldDomain,
                            code(WorldError::EntityCapacity),
                            "the commit would create more entities than the World has slots for");
    }
    for (const CommandBuffer::Command& command : buffer.commands_) {
        const EntityHandle kTarget = std::holds_alternative<EntityHandle>(command.target)
                                         ? std::get<EntityHandle>(command.target)
                                         : report.created[std::get<PendingEntity>(command.target).index];
        if (command.kind == CommandBuffer::Kind::Create) {
            // Capacity was checked above, so creation cannot fail here.
            auto created = create();
            RAWFRAME_CHECK(created.has_value(), "commit capacity was checked");
            report.created[std::get<PendingEntity>(command.target).index] = *created;
            ++report.applied;
            continue;
        }
        if (!alive(kTarget)) {
            ++report.skippedStale;
            continue;
        }
        result::Status done;
        switch (command.kind) {
        case CommandBuffer::Kind::Destroy:
            done = destroy(kTarget);
            break;
        case CommandBuffer::Kind::Insert: {
            // A tag carries no value; insert still needs somewhere to point.
            std::byte tag{};
            done = insertErased(kTarget, command.component, command.value != nullptr ? command.value : &tag);
            break;
        }
        case CommandBuffer::Kind::Remove:
            done = removeErased(kTarget, command.component);
            break;
        case CommandBuffer::Kind::Create:
            break;
        }
        RAWFRAME_CHECK(done.has_value(), "a structural command on a live entity cannot fail");
        ++report.applied;
    }
    buffer.clear();
    return report;
}

void* World::getErased(EntityHandle entity, schema::ComponentRuntimeId component) noexcept {
    if (!alive(entity)) {
        return nullptr;
    }
    const EntityRecord& record = records_[entity.slot];
    detail::Archetype& archetype = *archetypes_[record.archetype];
    const int kColumn = archetype.columnIndex(component);
    return kColumn < 0 ? nullptr : archetype.column(kColumn).at(record.row);
}

bool World::hasErased(EntityHandle entity, schema::ComponentRuntimeId component) const noexcept {
    return alive(entity) && archetypes_[records_[entity.slot].archetype]->has(component);
}

} // namespace rawframe::world
