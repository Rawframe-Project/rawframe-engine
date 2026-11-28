#pragma once

// Constant forces (components.h's Force3D) and align drives (Align3D):
// what the step does to bodies every tick before it steps, private to the
// module, as Maul3D is.

#include "bodies.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/query.h"
#include "rawframe/world/world.h"

#include <map>
#include <maul3d/maul3d.h>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::physics3d {

class Drives {
public:
    [[nodiscard]] static result::Result<Drives> resolve(const schema::SchemaRegistry& registry);

    /// What the step must declare it reads.
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> reads() const;

    /// Every Force3D applied to its entity's body for the next step, and
    /// every align drive made, aimed, or taken away to match its Align3D,
    /// in entity order. `mapped` is the step's entities' bodies.
    void apply(world::World& world,
               m3World* physics,
               const std::map<world::EntityHandle, Mapped>& mapped,
               Physics3DStatistics& statistics);

private:
    /// One entity's drive: the joint pulling its body toward a kinematic
    /// anchor that sits at the target, or nothing while refused.
    struct Aligned {
        m3BodyId anchor{};
        m3JointId joint{};
        m3BodyId body{};
        Align3D made;
        bool refused = false;
    };

    void push(world::World& world,
              m3World* physics,
              const std::map<world::EntityHandle, Mapped>& mapped,
              Physics3DStatistics& statistics);
    void align(world::World& world,
               m3World* physics,
               const std::map<world::EntityHandle, Mapped>& mapped,
               Physics3DStatistics& statistics);
    static void remove(m3World* physics, Aligned& aligned) noexcept;

    std::optional<world::Query<world::Read<Force3D>>> forces_;
    std::optional<world::Query<world::Read<Align3D>>> aligns_;
    std::vector<std::pair<world::EntityHandle, const Force3D*>> forceRows_;
    std::vector<std::pair<world::EntityHandle, const Align3D*>> alignRows_;
    std::map<world::EntityHandle, Aligned> aligned_;
};

} // namespace rawframe::physics3d
