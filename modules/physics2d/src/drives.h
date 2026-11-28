#pragma once

// Constant forces (components.h's Force2D) and align drives (Align2D):
// what the step does to bodies every tick before it steps, private to the
// module, as Maul2D is.

#include "rawframe/physics2d/components.h"
#include "rawframe/physics2d/physics.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/query.h"
#include "rawframe/world/world.h"

#include <map>
#include <maul2d/maul2d.h>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::physics2d {

/// The entities whose made body is dynamic, with that body, in entity
/// order.
using DynamicBodies = std::vector<std::pair<world::EntityHandle, m2BodyId>>;

class Drives {
public:
    [[nodiscard]] static result::Result<Drives> resolve(const schema::SchemaRegistry& registry);

    /// What the step must declare it reads.
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> reads() const;

    /// Every Force2D applied to its entity's body for the next step, and
    /// every align drive made, aimed, or taken away to match its Align2D,
    /// in entity order.
    void apply(world::World& world, m2World* physics, const DynamicBodies& bodies, Physics2DStatistics& statistics);

private:
    /// One entity's drive: the motor joint pulling its body toward a
    /// kinematic anchor that sits at the target, or nothing while refused.
    struct Aligned {
        m2BodyId anchor{};
        m2JointId joint{};
        m2BodyId body{};
        Align2D made;
        bool refused = false;
    };

    void push(world::World& world, m2World* physics, const DynamicBodies& bodies, Physics2DStatistics& statistics);
    void align(world::World& world, m2World* physics, const DynamicBodies& bodies, Physics2DStatistics& statistics);
    static void remove(m2World* physics, Aligned& aligned) noexcept;

    std::optional<world::Query<world::Read<Force2D>>> forces_;
    std::optional<world::Query<world::Read<Align2D>>> aligns_;
    std::vector<std::pair<world::EntityHandle, const Force2D*>> forceRows_;
    std::vector<std::pair<world::EntityHandle, const Align2D*>> alignRows_;
    std::map<world::EntityHandle, Aligned> aligned_;
};

} // namespace rawframe::physics2d
