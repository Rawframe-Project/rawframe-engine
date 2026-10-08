#include "rawframe/world_kest/kest_systems.h"

#include "doorway.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace rawframe::world_kest {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, WorldKestError error, std::string_view why) {
    return result::fail(errorClass, kWorldKestDomain, code(error), why);
}

/// Whether a column, as declared or as kept, is lent to the system as an
/// array.
template <typename Column> bool carriesData(const Column& column) noexcept {
    return column.entities || column.access == world::Access::Read || column.access == world::Access::Write;
}

/// What a system takes: the count, then one lent array per data column.
std::vector<kest::Argument> systemArguments(std::size_t data) {
    std::vector<kest::Argument> takes(1 + data, kest::Argument{.slot = kest::Slot::I32, .lent = true});
    takes[0].lent = false;
    return takes;
}

std::size_t roundUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

namespace {

// What the doors take and give, as they are offered.
constexpr std::array<kest::Parameter, 2> kStreamAndBound = {kest::Slot::I32, kest::Slot::U32};
constexpr std::array<kest::Parameter, 1> kStream = {kest::Slot::I32};
constexpr std::array<kest::Parameter, 1> kU32 = {kest::Slot::U32};
constexpr std::array<kest::Parameter, 1> kF64 = {kest::Slot::F64};
constexpr std::array<kest::Parameter, 1> kBool = {kest::Slot::Bool};
constexpr std::array<kest::Parameter, 1> kI32 = {kest::Slot::I32};

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
    std::string entryName;
    kest::Entry entry;
    std::vector<Column> columns;
    std::vector<std::string> after;
    std::vector<std::string> before;
    std::vector<std::string> randomStreams;
    std::vector<schema::ComponentTypeId> lookups;
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
               std::vector<DataColumn> columns,
               std::vector<schema::ComponentRuntimeId> lookups) noexcept
        : machine_(&machine), doorway_(&doorway), entry_(entry), query_(std::move(query)), columns_(std::move(columns)),
          lookups_(std::move(lookups)) {
        reads_ = query_.reads();
        // A lookup reads its component wherever it lies: the schedule
        // orders it as a read.
        for (const schema::ComponentRuntimeId kLookup : lookups_) {
            if (!std::ranges::contains(reads_, kLookup)) {
                reads_.push_back(kLookup);
            }
        }
        writes_ = query_.writes();
        frame_.resize(entry_.frameSlots);
    }

    /// Points the system at a reloaded program's machine and entry.
    void retarget(kest::Machine& machine, kest::Entry entry) noexcept {
        machine_ = &machine;
        entry_ = entry;
    }

    [[nodiscard]] std::span<const schema::ComponentRuntimeId> reads() const noexcept {
        return reads_;
    }
    [[nodiscard]] std::span<const schema::ComponentRuntimeId> writes() const noexcept {
        return writes_;
    }

    result::Status run(world::SystemContext& context) noexcept override {
        if (doorway_->timing == nullptr) {
            return runUntimed(context);
        }
        const execution::MonotonicInstant kStart = doorway_->timing->now();
        result::Status ran = runUntimed(context);
        doorway_->timing->add(context.tick.value, doorway_->timing->now() - kStart);
        return ran;
    }

private:
    [[nodiscard]] result::Status runUntimed(world::SystemContext& context) noexcept {
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
        // SPEC-0013's journal per tick, counted across the machine's
        // systems before any is copied.
        if (doorway_->journalTick != context.tick.value) {
            doorway_->journalTick = context.tick.value;
            doorway_->journaled = 0;
            doorway_->operations = 0;
        }
        if (journalBytes > doorway_->journalLimit - doorway_->journaled) {
            return refuse(result::ErrorClass::ResourceExhausted,
                          WorldKestError::JournalExhausted,
                          "the tick's systems would journal more than they may");
        }
        doorway_->journaled += journalBytes;
        // The journal grows to the largest tick seen and is then reused.
        if (journal_.size() < journalBytes) {
            journal_.resize(journalBytes);
        }
        forEachJournaled([this](const Lent& lent, std::size_t bytes) {
            std::memcpy(journalAt(lent), lent.world, bytes);
        });

        // The doors act on this run's command buffer, and only during it.
        doorway_->context = &context;
        doorway_->lookups = lookups_;
        ++doorway_->run;
        const std::size_t kCommandsBefore = context.commands.size();
        for (KestStaging* each : doorway_->staging) {
            each->begin(context.tick);
        }
        result::Status called;
        for (std::size_t at = 0; at < chunks_.size() && called.has_value(); ++at) {
            called = callOnce(chunks_[at], at == 0 ? kest::Fuel::Refill : kest::Fuel::Continue);
        }
        doorway_->context = nullptr;
        doorway_->lookups = {};
        result::Status kept = keep(context, kCommandsBefore, std::move(called));
        for (KestStaging* each : doorway_->staging) {
            each->end(kept.has_value());
        }
        return kept;
    }

    /// What a run's calls answered, made the World's when they succeeded
    /// within the tick's operations.
    [[nodiscard]] result::Status
    keep(world::SystemContext& context, std::size_t commandsBefore, result::Status called) noexcept {
        RAWFRAME_TRY(std::move(called));
        // SPEC-0013's journal operations per tick, across the machine's
        // systems: past it, this run's commands and writes are discarded.
        const std::size_t kRecorded = context.commands.size() - commandsBefore;
        if (kRecorded > doorway_->operationLimit - doorway_->operations) {
            return refuse(result::ErrorClass::ResourceExhausted,
                          WorldKestError::JournalExhausted,
                          "the tick's systems would record more operations than they may");
        }
        doorway_->operations += kRecorded;

        // Every call succeeded: the journal becomes the World's values.
        forEachJournaled([this](const Lent& lent, std::size_t bytes) {
            std::memcpy(lent.world, journalAt(lent), bytes);
        });
        return {};
    }

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
    std::vector<schema::ComponentRuntimeId> lookups_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::vector<schema::ComponentRuntimeId> writes_;
    std::vector<kest::Value> frame_;
    std::vector<Chunk> chunks_;
    std::vector<Lent> lendFrom_;
    std::vector<std::byte> journal_;
};

KestSystems::KestSystems(std::shared_ptr<const kest::Program> program,
                         std::unique_ptr<Doorway> doorway,
                         kest::DoorTable doors,
                         kest::MachineLimits limits,
                         kest::Trust trust,
                         std::unique_ptr<kest::Machine> machine,
                         std::vector<Declared> declared) noexcept
    : program_(std::move(program)), doors_(std::move(doors)), limits_(limits), trust_(trust),
      doorway_(std::move(doorway)), machine_(std::move(machine)), declared_(std::move(declared)) {
}

result::Status KestSystems::reload(std::shared_ptr<const kest::Program> program) {
    if (program == nullptr) {
        return refuse(result::ErrorClass::InvalidArgument, WorldKestError::EntryMismatch, "a reload needs a program");
    }
    // Every shape the World holds values of must be the same shape.
    const auto kSameShape = [&](std::string_view type) -> result::Status {
        RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kOld, program_->layout(type));
        RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kNew, program->layout(type));
        if (kOld.mark != kNew.mark) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::FailedPrecondition,
                                                         WorldKestError::ColumnMismatch,
                                                         "a reloaded program changes the shape of a type the World "
                                                         "holds")
                                                      .error()
                                                      .withContext("type", type)};
        }
        return {};
    };
    for (const Doorway::Component& component : doorway_->components) {
        RAWFRAME_TRY(kSameShape(component.kestType));
    }
    for (const Declared& declared : declared_) {
        for (const Declared::Column& column : declared.columns) {
            if (column.entities) {
                RAWFRAME_TRY(kSameShape(kEntityType));
            } else if (column.access == world::Access::Read || column.access == world::Access::Write) {
                RAWFRAME_TRY(kSameShape(column.element));
            }
        }
    }
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine, kest::Machine::start(program, doors_, trust_, limits_));
    std::vector<kest::Entry> entries;
    for (const Declared& declared : declared_) {
        RAWFRAME_TRY_ASSIGN(const kest::Entry kEntry, machine->entry(declared.entryName));
        const auto kData =
            static_cast<std::size_t>(std::ranges::count_if(declared.columns, &carriesData<Declared::Column>));
        if (kEntry.frameSlots != declared.entry.frameSlots ||
            !machine->checkArguments(kEntry, systemArguments(kData)).has_value()) {
            return refuse(result::ErrorClass::FailedPrecondition,
                          WorldKestError::EntryMismatch,
                          "a reloaded system takes other columns than it was declared with");
        }
        entries.push_back(kEntry);
    }
    // Everything checked: the swap cannot fail.
    for (std::size_t index = 0; index < declared_.size(); ++index) {
        declared_[index].entry = entries[index];
        if (index < systems_.size()) {
            systems_[index]->retarget(*machine, entries[index]);
        }
    }
    machine_ = std::move(machine);
    program_ = std::move(program);
    return {};
}

KestSystems::~KestSystems() = default;

result::Result<std::unique_ptr<KestSystems>> KestSystems::create(KestSystemsSettings settings) {
    if (settings.program == nullptr) {
        return refuse(
            result::ErrorClass::InvalidArgument, WorldKestError::EntryMismatch, "Kest systems need a program");
    }
    // The doorway first: every door's context points into it.
    auto doorway = std::make_unique<Doorway>();
    doorway->timing = settings.timing;
    doorway->journalLimit = settings.journalBytesPerTick;
    doorway->operationLimit = settings.journalOperationsPerTick;
    doorway->staging.assign(settings.staging.begin(), settings.staging.end());
    doorway->components.reserve(settings.components.size() + 1);
    for (const KestComponent& component : settings.components) {
        Doorway::Component& added = doorway->components.emplace_back();
        added.doorway = doorway.get();
        added.id = component.component;
        added.kestType = std::string{component.kestType};
        added.entityFields.assign(component.entityFields.begin(), component.entityFields.end());
        added.insertName = added.kestType + ".insert";
        added.removeName = added.kestType + ".remove";
        added.getName = added.kestType + ".get";
        added.hasName = added.kestType + ".has";
        added.countName = added.kestType + ".count";
        added.entityName = added.kestType + ".entity";
    }
    // rawframe.world asks for the persistent identity's insert whether or
    // not the game lists it; any program that lays it out may insert it.
    if (std::ranges::none_of(settings.components,
                             [](const KestComponent& component) {
                                 return component.component == world::Persistent::kComponentTypeId;
                             }) &&
        settings.program->layout("Persistent").has_value()) {
        Doorway::Component& added = doorway->components.emplace_back();
        added.doorway = doorway.get();
        added.id = world::Persistent::kComponentTypeId;
        added.kestType = "Persistent";
        added.insertName = "Persistent.insert";
        added.removeName = "Persistent.remove";
        added.getName = "Persistent.get";
        added.hasName = "Persistent.has";
        added.countName = "Persistent.count";
        added.entityName = "Persistent.entity";
        added.optional = true;
    }
    for (const KestPrefab& prefab : settings.prefabs) {
        doorway->prefabs.push_back(Doorway::Prefab{.prefab = prefab, .runtimes = {}, .descriptors = {}});
    }
    kest::DoorTable doors = std::move(settings.doors);
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "World.create", .function = &createDoor, .context = doorway.get(), .gives = kEntityParameter}));
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "World.destroy", .function = &destroyDoor, .context = doorway.get(), .takes = kEntityParameter}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Scene.spawn",
                                      .function = &spawnDoor,
                                      .context = doorway.get(),
                                      .takes = kPrefabParameter,
                                      .gives = kEntityParameter}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Random.below",
                                      .function = &belowDoor,
                                      .context = doorway.get(),
                                      .takes = kStreamAndBound,
                                      .gives = kU32}));
    RAWFRAME_TRY(doors.add(kest::Door{
        .name = "Random.unit", .function = &unitDoor, .context = doorway.get(), .takes = kStream, .gives = kF64}));
    for (Doorway::Component& component : doorway->components) {
        // A Kest type two components share has no doors: which of them an
        // insert would add is not the program's to say (D260).
        if (std::ranges::count(doorway->components, component.kestType, &Doorway::Component::kestType) > 1) {
            continue;
        }
        component.insertTakes[1] = kest::Parameter{kest::Slot::Value, component.kestType};
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.insertName,
                                          .function = &insertDoor,
                                          .context = &component,
                                          .takes = component.insertTakes}));
        RAWFRAME_TRY(doors.add(kest::Door{
            .name = component.removeName, .function = &removeDoor, .context = &component, .takes = kEntityParameter}));
        // A lookup only reads a component the running system declared, of
        // a live entity, within its size (D391).
        component.getGives[0] = kest::Parameter{kest::Slot::Value, component.kestType};
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.getName,
                                          .function = &getDoor,
                                          .context = &component,
                                          .takes = kEntityParameter,
                                          .gives = component.getGives,
                                          .safeForUntrusted = true}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.hasName,
                                          .function = &hasDoor,
                                          .context = &component,
                                          .takes = kEntityParameter,
                                          .gives = kBool,
                                          .safeForUntrusted = true}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.countName,
                                          .function = &countDoor,
                                          .context = &component,
                                          .gives = kI32,
                                          .safeForUntrusted = true}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.entityName,
                                          .function = &entityDoor,
                                          .context = &component,
                                          .takes = kI32,
                                          .gives = kEntityParameter,
                                          .safeForUntrusted = true}));
    }

    RAWFRAME_TRY_ASSIGN(std::unique_ptr<kest::Machine> machine,
                        kest::Machine::start(settings.program, doors, settings.trust, settings.limits));
    std::vector<Declared> declared;
    declared.reserve(settings.systems.size());
    for (const KestSystemDeclaration& system : settings.systems) {
        RAWFRAME_TRY_ASSIGN(const kest::Entry kEntry, machine->entry(system.entry));
        Declared copy{.identity = std::string{system.identity},
                      .phase = system.phase,
                      .entryName = std::string{system.entry},
                      .entry = kEntry,
                      .columns = {},
                      .after = {system.after.begin(), system.after.end()},
                      .before = {system.before.begin(), system.before.end()},
                      .randomStreams = {system.randomStreams.begin(), system.randomStreams.end()},
                      .lookups = {system.lookups.begin(), system.lookups.end()},
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
        if (kEntry.frameSlots != 1 + data || !machine->checkArguments(kEntry, systemArguments(data)).has_value()) {
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
    return std::make_unique<KestSystems>(std::move(settings.program),
                                         std::move(doorway),
                                         std::move(doors),
                                         settings.limits,
                                         settings.trust,
                                         std::move(machine),
                                         std::move(declared));
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
        if (component.optional && !registry.find(component.id).has_value()) {
            component.descriptor = nullptr;
            component.holders.reset();
            continue;
        }
        RAWFRAME_TRY_ASSIGN(component.runtime, registry.find(component.id));
        component.descriptor = &registry.descriptor(component.runtime);
        const std::array<world::ColumnTerm, 1> kHeld = {world::ColumnTerm{component.runtime, world::Access::With}};
        RAWFRAME_TRY_ASSIGN(component.holders, world::ColumnQuery::resolve(kHeld, registry));
        RAWFRAME_TRY(kShaped(*component.descriptor, component.kestType));
        component.scratch.resize(component.descriptor->size);
    }
    for (Doorway::Prefab& prefab : doorway_->prefabs) {
        prefab.runtimes.clear();
        prefab.descriptors.clear();
        doorway_->prefabMade.reserve(std::max(doorway_->prefabMade.capacity(), prefab.prefab.entities.size()));
        for (const KestPrefab::Entity& entity : prefab.prefab.entities) {
            for (const KestPrefab::Part& part : entity.parts) {
                doorway_->prefabScratch.reserve(std::max(doorway_->prefabScratch.capacity(), part.value.size()));
                doorway_->prefabOffsets.reserve(std::max(doorway_->prefabOffsets.capacity(), part.references.size()));
            }
            std::vector<schema::ComponentRuntimeId>& runtimes = prefab.runtimes.emplace_back();
            std::vector<const schema::ComponentDescriptor*>& descriptors = prefab.descriptors.emplace_back();
            for (const KestPrefab::Part& part : entity.parts) {
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kRuntime, registry.find(part.component));
                runtimes.push_back(kRuntime);
                descriptors.push_back(&registry.descriptor(kRuntime));
            }
        }
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
        std::vector<schema::ComponentRuntimeId> lookups;
        for (const schema::ComponentTypeId kLookup : declared.lookups) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, registry.find(kLookup));
            // Its own writes are journaled until the run ends, so a lookup
            // of what it writes would read the value from before them.
            if (std::ranges::contains(query.writes(), kId)) {
                return refuse(result::ErrorClass::InvalidArgument,
                              WorldKestError::ColumnMismatch,
                              "a Kest system looks up a component it writes");
            }
            lookups.push_back(kId);
        }
        auto system = std::make_unique<KestSystem>(
            *machine_, *doorway_, declared.entry, std::move(query), std::move(data), std::move(lookups));
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
