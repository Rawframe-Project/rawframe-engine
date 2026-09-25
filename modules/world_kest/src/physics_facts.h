#pragma once

// What a game's physics line brings, by dimension: the engine components the
// game gains, the values queries answer with, and where in a body its motion and collision
// class are. Everything else about the two is the same (rawframe.physics).

#include "rawframe/physics/layout.h"
#include "rawframe/physics2d/components.h"
#include "rawframe/physics3d/components.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace rawframe::world_kest {

struct PhysicsFacts {
    std::span<const physics::ComponentLayout> components;
    /// The values queries answer with (a ray's hit, an overlap).
    std::span<const physics::ComponentLayout> answers;
    schema::ComponentTypeId body;
    std::size_t motion = 0;
    std::size_t collisionClass = 0;
    std::string_view contact;
    /// The joint component, whose two bodies are entity fields.
    std::string_view joint;
    /// The Kest module a program imports for them.
    std::string_view module;
};

/// For a game of 2 or 3 dimensions.
[[nodiscard]] inline PhysicsFacts physicsFacts(std::uint8_t dimensions) noexcept {
    if (dimensions == 3) {
        return PhysicsFacts{.components = physics3d::componentLayouts(),
                            .answers = physics3d::answerLayouts(),
                            .body = physics3d::Body3D::kComponentTypeId,
                            .motion = offsetof(physics3d::Body3D, motion),
                            .collisionClass = offsetof(physics3d::Body3D, collisionClass),
                            .contact = physics3d::Contact3D::kComponentName,
                            .joint = physics3d::Joint3D::kComponentName,
                            .module = "rawframe.physics3d"};
    }
    return PhysicsFacts{.components = physics2d::componentLayouts(),
                        .answers = physics2d::answerLayouts(),
                        .body = physics2d::Body2D::kComponentTypeId,
                        .motion = offsetof(physics2d::Body2D, motion),
                        .collisionClass = offsetof(physics2d::Body2D, collisionClass),
                        .contact = physics2d::Contact2D::kComponentName,
                        .joint = physics2d::Joint2D::kComponentName,
                        .module = "rawframe.physics2d"};
}

} // namespace rawframe::world_kest
