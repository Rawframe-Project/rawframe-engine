#pragma once

// World systems written in Kest (ADR-0084, SPEC-0005 systems). A Kest system is
// a function of a program taking the row count and one array per data column:
//
//   fn integrate(count: i32, positions: [Position], velocities: [Velocity])
//
// It runs once per matching archetype. Read columns are lent in place; write
// columns are lent as a journal copy that replaces the World's values only
// when every call of the system that tick succeeded, so a refused or
// exhausted system changes nothing. Structural changes go through doors into
// the system's command buffer, discarded with it when the system fails.

#include "rawframe/kest/doors.h"
#include "rawframe/kest/machine.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world_kest/kest_timing.h"
#include "rawframe/world_runtime/simulation.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// One query term of a Kest system. A Read or Write column is lent as an
/// array of the program's type `element`, which must have the component's
/// size and alignment; With and Without only filter and name no element.
/// The Kest type of an entity, declared by the engine's own Kest module
/// (`modules/kest_library/kest/rawframe/world.kest`).
inline constexpr std::string_view kEntityType = "rawframe.world.Entity";

struct KestColumn {
    schema::ComponentTypeId component;
    std::string_view element;
    world::Access access = world::Access::Read;
    /// Lends the archetype's entities, as `[rawframe.world.Entity]`, in
    /// place of a component column; the other fields are not read.
    bool entities = false;
};

/// A component programs may add and take away through the doors
/// `<kestType>.insert(entity: Entity, value: <kestType>)` and
/// `<kestType>.remove(entity: Entity)`, recorded in the running system's
/// command buffer. It must be plain data shaped like its Kest type.
struct KestComponent {
    schema::ComponentTypeId component;
    std::string_view kestType;
    /// Where its value holds entities: an inserted value naming an entity
    /// the same run creates names the entity the barrier creates (D97).
    std::span<const std::size_t> entityFields;
};

struct KestSystemDeclaration {
    std::string_view identity;
    world::Phase phase = world::Phase::Simulation;
    /// The Kest function, as the file names it.
    std::string_view entry;
    std::span<const KestColumn> columns;
    std::span<const std::string_view> after;
    std::span<const std::string_view> before;
    /// The World random streams the system draws from; the program names one
    /// by its place in this list (`rawframe.random`).
    std::span<const std::string_view> randomStreams;
};

/// A scene a program spawns whole with `Scene.spawn(prefab: u64)` (D98):
/// its entities, each as the values of its components, and where a value
/// names another of the prefab's entities.
struct KestPrefab {
    struct Reference {
        /// Where in the value an entity lies.
        std::size_t offset = 0;
        /// Which of the prefab's entities it names.
        std::size_t target = 0;
    };
    struct Part {
        schema::ComponentTypeId component;
        std::vector<std::byte> value;
        std::vector<Reference> references;
    };
    struct Entity {
        std::vector<Part> parts;
    };

    std::uint64_t id = 0;
    std::vector<Entity> entities;
};

/// SPEC-0013's mutation journal per Domain and tick: the columns a
/// machine's systems copy to write in one tick, together (D229).
inline constexpr std::size_t kMaximumJournalBytes = std::size_t{4} << 20U;
/// SPEC-0013's journal operations per Domain and tick: the structural
/// commands a machine's systems record in one tick, together (D240).
inline constexpr std::size_t kMaximumJournalOperations = 65'536;

struct KestSystemsSettings {
    std::shared_ptr<const kest::Program> program;
    /// Copied: the doors themselves must outlive the systems. `World.create`,
    /// `World.destroy`, `Random.below`, `Random.unit`, `Scene.spawn`, and
    /// each component's doors are added to it.
    kest::DoorTable doors;
    std::span<const KestComponent> components;
    /// What `Scene.spawn` spawns, by identity; copied.
    std::span<const KestPrefab> prefabs;
    /// One budget per system per tick, spent across its archetypes.
    kest::MachineLimits limits;
    /// Untrusted for a mod's program (D181): only doors marked safe for it
    /// bind, under Kest's untrusted profile.
    kest::Trust trust = kest::Trust::Trusted;
    std::span<const KestSystemDeclaration> systems;
    /// Where each system's runs are timed, or nowhere; outlives the systems.
    KestTiming* timing = nullptr;
    /// Journal bytes all the systems may take in one tick; a system that
    /// would pass it is refused for the tick and changes nothing.
    std::size_t journalBytesPerTick = kMaximumJournalBytes;
    /// Commands all the systems may record in one tick; a system that would
    /// pass it is refused for the tick and changes nothing.
    std::size_t journalOperationsPerTick = kMaximumJournalOperations;
};

/// The Kest systems of one program on one machine, contributed to a World.
/// Systems of one machine never run at the same time, which the schedule's
/// sequential phases guarantee today; parallel waves will have to keep them
/// on one lane.
class KestSystems final : public world_runtime::SystemContributor {
public:
    /// Starts the machine and finds every entry. Checks that do not need the
    /// registry happen here, so a bad program fails before the World starts.
    [[nodiscard]] static result::Result<std::unique_ptr<KestSystems>> create(KestSystemsSettings settings);

    ~KestSystems() override;

    /// Resolves the columns against the frozen registry and checks each data
    /// column against its Kest type and each entry's frame against its
    /// columns.
    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Replaces the program, keeping the World, the systems, and their
    /// schedule: a new machine is started and every entry found and checked
    /// before anything changes, and every Kest type the systems lend or insert
    /// must keep its shape mark, since the World holds values of it. Call it
    /// between ticks, never while a system of this set runs. On refusal the
    /// old program keeps running.
    [[nodiscard]] result::Status reload(std::shared_ptr<const kest::Program> program);

    [[nodiscard]] kest::Machine& machine() noexcept {
        return *machine_;
    }

    class KestSystem;
    struct Declared;
    struct Doorway;

    KestSystems(std::shared_ptr<const kest::Program> program,
                std::unique_ptr<Doorway> doorway,
                kest::DoorTable doors,
                kest::MachineLimits limits,
                kest::Trust trust,
                std::unique_ptr<kest::Machine> machine,
                std::vector<Declared> declared) noexcept;

private:
    std::shared_ptr<const kest::Program> program_;
    kest::DoorTable doors_;
    kest::MachineLimits limits_;
    kest::Trust trust_ = kest::Trust::Trusted;
    // Before the machine: its doors point into it, so it goes last.
    std::unique_ptr<Doorway> doorway_;
    std::unique_ptr<kest::Machine> machine_;
    std::vector<Declared> declared_;
    std::vector<std::unique_ptr<KestSystem>> systems_;
};

} // namespace rawframe::world_kest
