#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/schedule.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"

#include <cstdint>
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
};

inline constexpr composition::Capability<Simulation> kSimulation{"rawframe.world.simulation"};

} // namespace rawframe::world_runtime
