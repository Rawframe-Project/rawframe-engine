#pragma once

// Which entity a ray meets, and where an author wrote it (D456): the first
// body along the ray, from whoever owns the World's bodies and knows which
// authored scene each entity came from (a game, through its physics and the
// scenes it follows), so a reader that knows neither, the tooling endpoint,
// can tell an author what they clicked. Read on the Host thread between
// ticks.

#include "rawframe/base/bits128.h"
#include "rawframe/composition/participant.h"
#include "rawframe/world/entity.h"

#include <array>
#include <optional>
#include <string>

namespace rawframe::world_runtime {

/// What a ray met: the entity, the point, and, for an entity an authored
/// scene brings, the scene's path among the game's and the entity's id in
/// it (empty and nought for one made while the World runs).
struct Picked {
    world::EntityHandle entity;
    std::array<double, 3> point{};
    std::string scene;
    base::Bits128 source{};
};

class Picking {
public:
    Picking() = default;
    Picking(const Picking&) = delete;
    Picking& operator=(const Picking&) = delete;
    virtual ~Picking() = default;

    /// The first body along the ray from `origin` to `origin + toward`, in
    /// World metres; none when it meets nothing, or the World has no bodies
    /// to meet.
    [[nodiscard]] virtual std::optional<Picked> pick(const std::array<double, 3>& origin,
                                                     const std::array<double, 3>& toward) const noexcept = 0;
};

inline constexpr composition::Capability<Picking> kPicking{"rawframe.world_runtime.picking"};

} // namespace rawframe::world_runtime
