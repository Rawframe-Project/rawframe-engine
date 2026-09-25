#pragma once

// Attachments (components.h's Attach3D): entities that follow a parent's
// pose, private to the module.

#include "rawframe/physics3d/components.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/query.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <set>
#include <vector>

namespace rawframe::physics3d {

class Attachments {
public:
    [[nodiscard]] static result::Result<Attachments> resolve(const schema::SchemaRegistry& registry);

    /// What the step must declare it reads; it writes Pose3D, as the step
    /// does already.
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> reads() const {
        return query_->reads();
    }

    /// Puts every attached entity at its parent's pose and offset, parents
    /// before children, and adds to `refused` each attachment that newly
    /// cannot follow.
    void follow(world::World& world, std::uint64_t& refused);

private:
    struct Row {
        world::EntityHandle entity;
        const Attach3D* attach = nullptr;
        Pose3D* pose = nullptr;
        std::size_t depth = 0;
    };

    std::optional<world::Query<world::Read<Attach3D>, world::Write<Pose3D>>> query_;
    schema::ComponentRuntimeId pose_{};
    schema::ComponentRuntimeId body_{};
    std::vector<Row> rows_;
    /// The attachments refused last step, so each is counted once.
    std::set<world::EntityHandle> refused_;
};

} // namespace rawframe::physics3d
