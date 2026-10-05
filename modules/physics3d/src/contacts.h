#pragma once

// The step's contact report, over the bodies the step keeps.

#include "bodies.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <map>
#include <maul3d/maul3d.h>
#include <utility>
#include <vector>

namespace rawframe::physics3d {

/// What the report reads of the step, and what it writes besides the
/// Contact3D components.
struct ContactReport {
    m3WorldId physics{};
    const std::vector<Row>* rows = nullptr;
    const std::map<world::EntityHandle, Mapped>* mapped = nullptr;
    const ShapeOwners* owners = nullptr;
    schema::ComponentRuntimeId contact{};
    /// Entity pairs overlapping now, lower entity first, and through how
    /// many shape pairs; the report keeps it.
    std::map<std::pair<world::EntityHandle, world::EntityHandle>, std::uint32_t>* overlapping = nullptr;
    /// Scratch for a body's contact data, kept between steps.
    std::vector<m3ContactData>* touching = nullptr;
    Physics3DStatistics* statistics = nullptr;
};

/// Every Contact3D of a body, from this step's event streams and what
/// touches and overlaps now, all in Maul3D's canonical order.
void reportContacts(world::World& world, const ContactReport& report);

} // namespace rawframe::physics3d
