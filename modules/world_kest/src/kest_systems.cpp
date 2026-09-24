#include "rawframe/world_kest/kest_systems.h"

#include "rawframe/world_kest/errors.h"

#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

bool carriesData(const KestColumn& column) noexcept {
    return column.entities || column.access == world::Access::Read || column.access == world::Access::Write;
}

std::size_t roundUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

// An entity a system's command buffer will create has no handle yet. Until
// the barrier a program holds it as generation 0 (never live) with the
// buffer's index, plus which run made it, so one kept past its run is
// refused rather than taken for another run's.
constexpr std::uint32_t kPendingIndexBits = 20;
constexpr std::uint32_t kPendingIndexMask = (1U << kPendingIndexBits) - 1U;
constexpr std::uint32_t kRunMask = (1U << (32U - kPendingIndexBits)) - 1U;

world::EntityHandle encodePending(world::PendingEntity pending, std::uint32_t run) noexcept {
    return world::EntityHandle{.slot = ((run & kRunMask) << kPendingIndexBits) | (pending.index + 1U), .generation = 0};
}

} // namespace

/// What the doors reach: the system running now, and each component a
/// program may insert or remove. Its address is every door's context, so it
/// lives as long as the machine.
struct KestSystems::Doorway {
    struct Component {
        Doorway* doorway = nullptr;
        schema::ComponentTypeId id;
        std::string kestType;
        std::string insertName;
        std::string removeName;
        std::array<kest::Parameter, 2> insertTakes{kest::Parameter{kest::Slot::Value, kEntityType},
                                                   kest::Parameter{kest::Slot::Value, ""}};
        schema::ComponentRuntimeId runtime;
        const schema::ComponentDescriptor* descriptor = nullptr;
        std::vector<std::byte> scratch;
    };

    world::SystemContext* context = nullptr;
    std::uint32_t run = 0;
    std::vector<Component> components;

    /// The target an entity from the program names, or why it names none.
    [[nodiscard]] std::optional<world::CommandTarget> target(kest::DoorCall& call, std::size_t argument) const {
        world::EntityHandle entity;
        if (!call.value(argument, std::as_writable_bytes(std::span{&entity, 1}))) {
            call.fail("an entity did not cross as an entity");
            return std::nullopt;
        }
        if (!entity.isNull()) {
            return world::CommandTarget{entity};
        }
        const std::uint32_t kIndex = entity.slot & kPendingIndexMask;
        if (kIndex == 0 || (entity.slot >> kPendingIndexBits) != (run & kRunMask)) {
            call.fail("the entity is null, or was created by another system run and kept");
            return std::nullopt;
        }
        return world::CommandTarget{world::PendingEntity{kIndex - 1U}};
    }
};

namespace {

constexpr std::array<kest::Parameter, 1> kEntityParameter = {kest::Parameter{kest::Slot::Value, kEntityType}};

using Doorway = KestSystems::Doorway;

void createDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("entities are created only while a system runs");
        return;
    }
    auto pending = doorway.context->commands.create();
    if (!pending.has_value()) {
        call.fail("the system's command buffer is full");
        return;
    }
    const world::EntityHandle kEntity = encodePending(*pending, doorway.run);
    static_cast<void>(call.answerValue(std::as_bytes(std::span{&kEntity, 1})));
}

void destroyDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("entities are destroyed only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (!kTarget) {
        return;
    }
    const auto* kLive = std::get_if<world::EntityHandle>(&*kTarget);
    if (kLive == nullptr) {
        call.fail("an entity is destroyed once it exists, after the barrier that creates it");
        return;
    }
    if (!doorway.context->commands.destroy(*kLive).has_value()) {
        call.fail("the system's command buffer is full");
    }
}

/// The stream a draw names, or null once the call has been failed.
world::Pcg32* streamOf(kest::DoorCall& call, Doorway& doorway) {
    if (doorway.context == nullptr) {
        call.fail("random streams are drawn from only while a system runs");
        return nullptr;
    }
    const std::int64_t kIndex = call.integer(0);
    const auto kStreams = doorway.context->declaredStreams;
    if (kIndex < 0 || static_cast<std::uint64_t>(kIndex) >= kStreams.size()) {
        call.fail("the system declared no random stream at that place");
        return nullptr;
    }
    auto stream = doorway.context->random(kStreams[static_cast<std::size_t>(kIndex)]);
    return stream.has_value() ? *stream : nullptr;
}

void belowDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    world::Pcg32* const kStream = streamOf(call, doorway);
    if (kStream == nullptr) {
        return;
    }
    const auto kBound = static_cast<std::uint32_t>(call.integer(1));
    if (kBound == 0) {
        call.fail("a draw below nought has nothing to draw");
        return;
    }
    call.answerInteger(kStream->nextBelow(kBound));
}

void unitDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (world::Pcg32* const kStream = streamOf(call, doorway)) {
        call.answerReal(kStream->nextDouble());
    }
}

constexpr std::array<kest::Parameter, 2> kStreamAndBound = {kest::Slot::I32, kest::Slot::U32};
constexpr std::array<kest::Parameter, 1> kStream = {kest::Slot::I32};
constexpr std::array<kest::Parameter, 1> kU32 = {kest::Slot::U32};
constexpr std::array<kest::Parameter, 1> kF64 = {kest::Slot::F64};

void insertDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway::Component& component = *static_cast<Doorway::Component*>(context);
    Doorway& doorway = *component.doorway;
    if (doorway.context == nullptr || component.descriptor == nullptr) {
        call.fail("components are inserted only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (!kTarget) {
        return;
    }
    if (!call.value(1, component.scratch)) {
        call.fail("a component value did not cross as its type");
        return;
    }
    if (!doorway.context->commands.insertBytes(*kTarget, component.runtime, *component.descriptor, component.scratch)
             .has_value()) {
        call.fail("the system's command buffer is full");
    }
}

void removeDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway::Component& component = *static_cast<Doorway::Component*>(context);
    Doorway& doorway = *component.doorway;
    if (doorway.context == nullptr || component.descriptor == nullptr) {
        call.fail("components are removed only while a system runs");
        return;
    }
    const auto kTarget = doorway.target(call, 0);
    if (kTarget && !doorway.context->commands.removeErased(*kTarget, component.runtime).has_value()) {
        call.fail("the system's command buffer is full");
    }
}

} // namespace

/// A declaration with every view it borrowed copied into strings it owns.
struct KestSystems::Declared {
    struct Column {
        schema::ComponentTypeId component;
        std::string element;
        world::Access access;
        bool entities;
    };

    std::string identity;
    world::Phase phase;
    kest::Entry entry;
    std::vector<Column> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
    std::vector<std::string> randomStreams;
    std::vector<std::string_view> afterViews;
    std::vector<std::string_view> beforeViews;
    std::vector<std::string_view> streamViews;
};

class KestSystems::KestSystem final : public world::System {
public:
    struct DataColumn {
        std::string element;
        std::size_t size;
        std::size_t alignment;
        bool writes;
        /// Which of the chunk's columns, or -1 for its entities.
        int chunkColumn;
    };

    KestSystem(kest::Machine& machine,
               Doorway& doorway,
               kest::Entry entry,
               world::ColumnQuery query,
               std::vector<DataColumn> columns) noexcept
        : machine_(&machine), doorway_(&doorway), entry_(entry), query_(std::move(query)),
          columns_(std::move(columns)) {
        reads_ = query_.reads();
        writes_ = query_.writes();
        frame_.resize(entry_.frameSlots);
    }

    [[nodiscard]] std::span<const schema::ComponentRuntimeId> reads() const noexcept {
        return reads_;
    }
    [[nodiscard]] std::span<const schema::ComponentRuntimeId> writes() const noexcept {
        return writes_;
    }

    result::Status run(world::SystemContext& context) noexcept override {
        chunks_.clear();
        lendFrom_.clear();
        std::size_t journalBytes = 0;
        bool tooMany = false;
        query_.forEachChunk(context.world, [&](const world::ColumnChunk& chunk) {
            const std::size_t kRows = chunk.entities.size();
            tooMany = tooMany || kRows > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
            chunks_.push_back(Chunk{.rows = kRows, .firstColumn = lendFrom_.size()});
            for (const DataColumn& column : columns_) {
                if (column.chunkColumn < 0) {
                    // Entities are read in place; their fields are the engine
                    // module's own, so a program cannot write them.
                    auto* entities = const_cast<world::EntityHandle*>(chunk.entities.data());
                    lendFrom_.push_back(Lent{.world = reinterpret_cast<std::byte*>(entities), .journal = kInPlace});
                } else if (column.writes) {
                    journalBytes = roundUp(journalBytes, column.alignment);
                    lendFrom_.push_back(Lent{.world = chunk.columns[static_cast<std::size_t>(column.chunkColumn)],
                                             .journal = journalBytes});
                    journalBytes += kRows * column.size;
                } else {
                    lendFrom_.push_back(Lent{.world = chunk.columns[static_cast<std::size_t>(column.chunkColumn)],
                                             .journal = kInPlace});
                }
            }
        });
        if (tooMany) {
            return refuse(result::ErrorClass::OutOfRange,
                          WorldKestError::TooManyRows,
                          "an archetype holds more rows than a Kest count can say");
        }
        if (chunks_.empty()) {
            return {};
        }
        // The journal grows to the largest tick seen and is then reused.
        if (journal_.size() < journalBytes) {
            journal_.resize(journalBytes);
        }
        forEachJournaled([this](const Lent& lent, std::size_t bytes) {
            std::memcpy(journalAt(lent), lent.world, bytes);
        });

        // The doors act on this run's command buffer, and only during it.
        doorway_->context = &context;
        ++doorway_->run;
        result::Status called;
        for (std::size_t at = 0; at < chunks_.size() && called.has_value(); ++at) {
            called = callOnce(chunks_[at], at == 0 ? kest::Fuel::Refill : kest::Fuel::Continue);
        }
        doorway_->context = nullptr;
        RAWFRAME_TRY(std::move(called));

        // Every call succeeded: the journal becomes the World's values.
        forEachJournaled([this](const Lent& lent, std::size_t bytes) {
            std::memcpy(lent.world, journalAt(lent), bytes);
        });
        return {};
    }

private:
    static constexpr std::size_t kInPlace = std::numeric_limits<std::size_t>::max();

    struct Chunk {
        std::size_t rows;
        std::size_t firstColumn;
    };

    /// Where one column of one chunk is lent from: the World in place, or an
    /// offset into the journal.
    struct Lent {
        std::byte* world;
        std::size_t journal;
    };

    template <typename Function> void forEachJournaled(Function&& function) {
        for (const Chunk& chunk : chunks_) {
            for (std::size_t index = 0; index < columns_.size(); ++index) {
                const Lent& lent = lendFrom_[chunk.firstColumn + index];
                if (lent.journal != kInPlace) {
                    function(lent, chunk.rows * columns_[index].size);
                }
            }
        }
    }

    [[nodiscard]] std::byte* journalAt(const Lent& lent) noexcept {
        return journal_.data() + lent.journal;
    }

    [[nodiscard]] result::Status callOnce(const Chunk& chunk, kest::Fuel fuel) {
        std::fill(frame_.begin(), frame_.end(), kest::Value{});
        frame_[0].integer = static_cast<std::int64_t>(chunk.rows);
        std::size_t made = 0;
        std::optional<result::Error> failure;
        for (; made < columns_.size(); ++made) {
            const Lent& lent = lendFrom_[chunk.firstColumn + made];
            auto handle = machine_->lend(lent.journal == kInPlace ? lent.world : journalAt(lent),
                                         static_cast<std::uint32_t>(chunk.rows),
                                         columns_[made].element,
                                         columns_[made].size);
            if (!handle.has_value()) {
                failure.emplace(std::move(handle).error());
                break;
            }
            frame_[1 + made] = *handle;
        }
        if (!failure) {
            auto outcome = machine_->call(entry_, frame_, fuel);
            if (outcome.isCancelled()) {
                failure.emplace(refuse(result::ErrorClass::FailedPrecondition,
                                       WorldKestError::Cancelled,
                                       "the Kest machine was cancelled while the system ran")
                                    .error());
            } else if (outcome.isError()) {
                failure.emplace(std::move(outcome).takeError());
            }
        }
        // Lends end whatever happened: the program must hold nothing over
        // World memory once the call is over.
        for (std::size_t index = 0; index < made; ++index) {
            machine_->endLend(frame_[1 + index]);
        }
        if (failure) {
            return std::unexpected<result::Error>{std::move(*failure)};
        }
        return {};
    }

    kest::Machine* machine_;
    Doorway* doorway_;
    kest::Entry entry_;
    world::ColumnQuery query_;
    std::vector<DataColumn> columns_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::vector<schema::ComponentRuntimeId> writes_;
    std::vector<kest::Value> frame_;
    std::vector<Chunk> chunks_;
    std::vector<Lent> lendFrom_;
    std::vector<std::byte> journal_;
};

KestSystems::KestSystems(std::shared_ptr<const kest::Program> program,
                         std::unique_ptr<Doorway> doorway,
                         std::unique_ptr<kest::Machine> machine,
                         std::vector<Declared> declared) noexcept
    : program_(std::move(program)), doorway_(std::move(doorway)), machine_(std::move(machine)),
      declared_(std::move(declared)) {
}

KestSystems::~KestSystems() = default;

result::Result<std::unique_ptr<KestSystems>> KestSystems::create(KestSystemsSettings settings) {
    if (settings.program == nullptr) {
        return refuse(
            result::ErrorClass::InvalidArgument, WorldKestError::EntryMismatch, "Kest systems need a program");
    }
    // The doorway first: every door's context points into it.
    auto doorway = std::make_unique<Doorway>();
    doorway->components.reserve(settings.components.size());
    for (const KestComponent& component : settings.components) {
        Doorway::Component& added = doorway->components.emplace_back();
        added.doorway = doorway.get();
        added.id = component.component;
        added.kestType = std::string{component.kestType};
        added.insertName = added.kestType + ".insert";
        added.removeName = added.kestType + ".remove";
    }
    kest::DoorTable doors = std::move(settings.doors);
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "World.create", .function = &createDoor, .context = doorway.get(), .gives = kEntityParameter}));
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "World.destroy", .function = &destroyDoor, .context = doorway.get(), .takes = kEntityParameter}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Random.below",
                                      .function = &belowDoor,
                                      .context = doorway.get(),
                                      .takes = kStreamAndBound,
                                      .gives = kU32}));
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "Random.unit", .function = &unitDoor, .context = doorway.get(), .takes = kStream, .gives = kF64}));
    for (Doorway::Component& component : doorway->components) {
        component.insertTakes[1] = kest::Parameter{kest::Slot::Value, component.kestType};
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.insertName,
                                          .function = &insertDoor,
                                          .context = &component,
                                          .takes = component.insertTakes}));
        RAWFRAME_TRY(doors.add(kest::Door{
            .name = component.removeName, .function = &removeDoor, .context = &component, .takes = kEntityParameter}));
    }

    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine,
                        kest::Machine::start(settings.program, doors, kest::Trust::Trusted, settings.limits));
    std::vector<Declared> declared;
    declared.reserve(settings.systems.size());
    for (const KestSystemDeclaration& system : settings.systems) {
        RAWFRAME_TRY_ASSIGN(const kest::Entry kEntry, machine->entry(system.entry));
        Declared copy{.identity = std::string{system.identity},
                      .phase = system.phase,
                      .entry = kEntry,
                      .columns = {},
                      .after = {system.after.begin(), system.after.end()},
                      .before = {system.before.begin(), system.before.end()},
                      .randomStreams = {system.randomStreams.begin(), system.randomStreams.end()},
                      .afterViews = {},
                      .beforeViews = {},
                      .streamViews = {}};
        std::size_t data = 0;
        for (const KestColumn& column : system.columns) {
            if (carriesData(column)) {
                ++data;
                RAWFRAME_TRY(settings.program->layout(column.entities ? kEntityType : column.element));
            }
            copy.columns.push_back(Declared::Column{.component = column.component,
                                                    .element = std::string{column.element},
                                                    .access = column.access,
                                                    .entities = column.entities});
        }
        // The count, then one array per data column; an answer, if any, is
        // one slot and fits over the count.
        if (kEntry.frameSlots != 1 + data) {
            return refuse(result::ErrorClass::InvalidArgument,
                          WorldKestError::EntryMismatch,
                          "a Kest system takes a count and one array per lent column");
        }
        declared.push_back(std::move(copy));
    }
    for (Declared& copy : declared) {
        copy.afterViews.assign(copy.after.begin(), copy.after.end());
        copy.beforeViews.assign(copy.before.begin(), copy.before.end());
        copy.streamViews.assign(copy.randomStreams.begin(), copy.randomStreams.end());
    }
    return std::make_unique<KestSystems>(
        std::move(settings.program), std::move(doorway), std::move(machine), std::move(declared));
}

result::Status KestSystems::declareSystems(const schema::SchemaRegistry& registry,
                                           std::vector<world::SystemDeclaration>& systems) noexcept {
    const auto kShaped = [this](const schema::ComponentDescriptor& descriptor,
                                std::string_view element) -> result::Status {
        RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kLayout, program_->layout(element));
        if (!descriptor.plainData || descriptor.size != kLayout.size || descriptor.alignment != kLayout.alignment) {
            return refuse(result::ErrorClass::InvalidArgument,
                          WorldKestError::ColumnMismatch,
                          "a Kest column's component is not plain data shaped like its Kest type");
        }
        return {};
    };
    for (Doorway::Component& component : doorway_->components) {
        RAWFRAME_TRY_ASSIGN(component.runtime, registry.find(component.id));
        component.descriptor = &registry.descriptor(component.runtime);
        RAWFRAME_TRY(kShaped(*component.descriptor, component.kestType));
        component.scratch.resize(component.descriptor->size);
    }
    // A World that starts again gets systems of its own: queries serve one
    // World.
    systems_.clear();
    for (const Declared& declared : declared_) {
        std::vector<world::ColumnTerm> terms;
        std::vector<KestSystem::DataColumn> data;
        int chunkColumns = 0;
        for (const Declared::Column& column : declared.columns) {
            if (column.entities) {
                data.push_back(KestSystem::DataColumn{.element = std::string{kEntityType},
                                                      .size = sizeof(world::EntityHandle),
                                                      .alignment = alignof(world::EntityHandle),
                                                      .writes = false,
                                                      .chunkColumn = -1});
                continue;
            }
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(column.component));
            terms.push_back(world::ColumnTerm{.component = kId, .access = column.access});
            if (column.access != world::Access::Read && column.access != world::Access::Write) {
                continue;
            }
            const schema::ComponentDescriptor& descriptor = registry.descriptor(kId);
            RAWFRAME_TRY(kShaped(descriptor, column.element));
            data.push_back(KestSystem::DataColumn{.element = column.element,
                                                  .size = descriptor.size,
                                                  .alignment = descriptor.alignment,
                                                  .writes = column.access == world::Access::Write,
                                                  .chunkColumn = chunkColumns++});
        }
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(terms, registry));
        auto system =
            std::make_unique<KestSystem>(*machine_, *doorway_, declared.entry, std::move(query), std::move(data));
        systems.push_back(world::SystemDeclaration{.identity = declared.identity,
                                                   .phase = declared.phase,
                                                   .reads = system->reads(),
                                                   .writes = system->writes(),
                                                   .after = declared.afterViews,
                                                   .before = declared.beforeViews,
                                                   .randomStreams = declared.streamViews,
                                                   .system = system.get()});
        systems_.push_back(std::move(system));
    }
    return {};
}

} // namespace rawframe::world_kest
