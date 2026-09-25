#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::world_runtime {

/// Declares systems once the World's registry is frozen, since a system's
/// declared access names runtime component IDs. The declarations, and the
/// systems they point at, must stay valid until the World stops.
class SystemContributor {
public:
    SystemContributor() = default;
    SystemContributor(const SystemContributor&) = delete;
    SystemContributor& operator=(const SystemContributor&) = delete;
    virtual ~SystemContributor() = default;

    [[nodiscard]] virtual result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                        std::vector<world::SystemDeclaration>& systems) noexcept = 0;
};

/// The World a Runtime simulates, as other participants see it. Components and
/// system contributors are added while participants are constructed; the
/// World itself exists from start.
class Simulation {
public:
    Simulation() = default;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    virtual ~Simulation() = default;

    /// Before start only; `failed_precondition` afterwards.
    [[nodiscard]] virtual result::Status addComponent(const schema::ComponentDescriptor& descriptor) = 0;
    [[nodiscard]] virtual result::Status addSystems(SystemContributor& contributor) = 0;

    template <schema::Component T> [[nodiscard]] result::Status addComponent() {
        return addComponent(schema::describeComponent<T>());
    }

    /// The World, or null before start and after stop.
    [[nodiscard]] virtual world::World* world() noexcept = 0;
    /// The next tick to run.
    [[nodiscard]] virtual world::TickIndex tick() const noexcept = 0;
    [[nodiscard]] virtual world::TickRate rate() const noexcept = 0;

    /// Counts World replacements. A participant that keeps entity handles
    /// checks it and lets them go when it changes: they name nothing in the
    /// new World (SPEC-0011 publication).
    [[nodiscard]] virtual std::uint64_t generation() const noexcept = 0;
    /// An empty World with the running one's registry and settings, for a
    /// restore to build off to the side. `failed_precondition` before start.
    [[nodiscard]] virtual result::Result<std::unique_ptr<world::World>> candidate() const = 0;
    /// Replaces the World, between ticks and on the Host thread only; `next`
    /// is the next tick to run. It cannot fail once called: the old World is
    /// gone and the new one ticks from `next`.
    virtual void replace(std::unique_ptr<world::World> world, world::TickIndex next) noexcept = 0;
    /// Runs no tick at or past `limit` until the limit moves or is lifted, so
    /// something can happen at an exact tick between two of them.
    virtual void holdAt(std::optional<world::TickIndex> limit) noexcept = 0;
};

inline constexpr composition::Capability<Simulation> kSimulation{"rawframe.world.simulation"};

} // namespace rawframe::world_runtime
