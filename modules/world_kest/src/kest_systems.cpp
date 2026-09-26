#include "rawframe/world_kest/kest_systems.h"

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
        std::vector<std::size_t> entityFields;
        /// Offered though the game does not list it: a World without it
        /// refuses the door when called, not the systems when declared.
        bool optional = false;
    };

    /// A prefab with its components' registry entries, found with the
    /// components'.
    struct Prefab {
        KestPrefab prefab;
        std::vector<std::vector<schema::ComponentRuntimeId>> runtimes;
        std::vector<std::vector<const schema::ComponentDescriptor*>> descriptors;
    };

    world::SystemContext* context = nullptr;
    std::uint32_t run = 0;
    /// Where runs are timed, or nowhere (D210).
    KestTiming* timing = nullptr;
    std::vector<Component> components;
    std::vector<Prefab> prefabs;
    // Reused by every spawn, sized when the systems are declared, so a
    // spawn allocates nothing.
    std::vector<std::byte> prefabScratch;
    std::vector<world::PendingEntity> prefabMade;
    std::vector<std::size_t> prefabOffsets;

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
constexpr std::array<kest::Parameter, 1> kPrefabParameter = {kest::Parameter{kest::Slot::U64}};

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

void spawnDoor(kest::DoorCall& call, void* context) noexcept {
    Doorway& doorway = *static_cast<Doorway*>(context);
    if (doorway.context == nullptr) {
        call.fail("prefabs are spawned only while a system runs");
        return;
    }
    const auto kId = static_cast<std::uint64_t>(call.integer(0));
    const auto kPrefab = std::ranges::find(doorway.prefabs, kId, [](const Doorway::Prefab& each) {
        return each.prefab.id;
    });
    if (kPrefab == doorway.prefabs.end()) {
        call.fail("the game declares no prefab of that identity");
        return;
    }
    world::CommandBuffer& commands = doorway.context->commands;
    std::vector<world::PendingEntity>& made = doorway.prefabMade;
    made.clear();
    for (std::size_t count = 0; count < kPrefab->prefab.entities.size(); ++count) {
        auto pending = commands.create();
        if (!pending.has_value()) {
            call.fail("the system's command buffer is full");
            return;
        }
        made.push_back(*pending);
    }
    std::vector<std::size_t>& offsets = doorway.prefabOffsets;
    for (std::size_t entity = 0; entity < made.size(); ++entity) {
        const std::vector<KestPrefab::Part>& parts = kPrefab->prefab.entities[entity].parts;
        for (std::size_t part = 0; part < parts.size(); ++part) {
            doorway.prefabScratch.assign(parts[part].value.begin(), parts[part].value.end());
            offsets.clear();
            for (const KestPrefab::Reference& reference : parts[part].references) {
                const world::EntityHandle kNamed = world::pendingReference(made[reference.target]);
                std::memcpy(doorway.prefabScratch.data() + reference.offset, &kNamed, sizeof kNamed);
                offsets.push_back(reference.offset);
            }
            if (!commands
                     .insertBytes(made[entity],
                                  kPrefab->runtimes[entity][part],
                                  *kPrefab->descriptors[entity][part],
                                  doorway.prefabScratch,
                                  offsets)
                     .has_value()) {
                call.fail("the system's command buffer is full");
                return;
            }
        }
    }
    // The prefab's first entity, as a program holds one this run creates.
    const world::EntityHandle kFirst = made.empty() ? world::EntityHandle{} : encodePending(made.front(), doorway.run);
    static_cast<void>(call.answerValue(std::as_bytes(std::span{&kFirst, 1})));
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
    // An entity this run creates, held in the value, as the buffer names it.
    for (const std::size_t kOffset : component.entityFields) {
        world::EntityHandle held;
        std::memcpy(&held, component.scratch.data() + kOffset, sizeof held);
        if (!held.isNull() || held.slot == 0) {
            continue;
        }
        const std::uint32_t kIndex = held.slot & kPendingIndexMask;
        if (kIndex == 0 || (held.slot >> kPendingIndexBits) != (doorway.run & kRunMask)) {
            call.fail("a value holds an entity created by another system run and kept");
            return;
        }
        const world::EntityHandle kPending = world::pendingReference(world::PendingEntity{kIndex - 1U});
        std::memcpy(component.scratch.data() + kOffset, &kPending, sizeof kPending);
    }
    if (!doorway.context->commands
             .insertBytes(*kTarget, component.runtime, *component.descriptor, component.scratch, component.entityFields)
             .has_value()) {
        call.fail("the system's command buffer is full, or a value names an entity the run has not created");
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
    std::string entryName;
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
    doorway->components.reserve(settings.components.size() + 1);
    for (const KestComponent& component : settings.components) {
        Doorway::Component& added = doorway->components.emplace_back();
        added.doorway = doorway.get();
        added.id = component.component;
        added.kestType = std::string{component.kestType};
        added.entityFields.assign(component.entityFields.begin(), component.entityFields.end());
        added.insertName = added.kestType + ".insert";
        added.removeName = added.kestType + ".remove";
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
        component.insertTakes[1] = kest::Parameter{kest::Slot::Value, component.kestType};
        RAWFRAME_TRY(doors.add(kest::Door{.name = component.insertName,
                                          .function = &insertDoor,
                                          .context = &component,
                                          .takes = component.insertTakes}));
        RAWFRAME_TRY(doors.add(kest::Door{
            .name = component.removeName, .function = &removeDoor, .context = &component, .takes = kEntityParameter}));
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
            continue;
        }
        RAWFRAME_TRY_ASSIGN(component.runtime, registry.find(component.id));
        component.descriptor = &registry.descriptor(component.runtime);
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
