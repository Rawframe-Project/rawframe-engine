#pragma once

// A game's splines (SPEC-0051's spline row, D868): entities of the game's
// components of `rawframe.spline.Point`'s type, each naming its spline's
// entity and its place among the spline's points and standing where its
// physics pose puts it, measured into paths by a system before the
// simulation phase; and the `Splines.along` door its programs follow them
// with, which answers from the paths as that system left them, so a system
// in the simulation phase or after sees this tick's points. A machine that
// measures none (a client's prediction) answers that there is no spline.

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/kest/doors.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/spline/spline.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/entity.h"
#include "rawframe/world_runtime/simulation.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// What a game's splines were at its stop: how many paths were measured.
inline constexpr diagnostics::EventIdentity kSplineSummary{"world_kest", "spline_summary"};

/// The system that measures every spline's path.
inline constexpr std::string_view kSplineMeasureSystem = "rawframe.spline.measure";

/// `rawframe.spline.Point` as C++ reads it.
struct SplinePoint {
    world::EntityHandle spline;
    std::uint32_t index = 0;
};

/// A game's components of the spline types, and the pose its points stand
/// by: `dimensions` 3 for rawframe.physics3d's, 2 for rawframe.physics2d's.
struct GameSplineComponents {
    std::vector<schema::ComponentTypeId> splines;
    std::vector<schema::ComponentTypeId> points;
    std::uint8_t dimensions = 3;
};

class GameSplines final : public world_runtime::SystemContributor {
public:
    explicit GameSplines(GameSplineComponents components) noexcept;
    GameSplines(const GameSplines&) = delete;
    GameSplines& operator=(const GameSplines&) = delete;
    ~GameSplines() override;

    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Measures every spline's points into its path, a path measured again
    /// only when its points or its closing changed.
    void measure(world::World& world);

    /// Where the spline `spline` is `meters` along it; none for an entity
    /// whose points make no path.
    [[nodiscard]] std::optional<spline::Along> along(world::EntityHandle spline, double meters) const;
    /// Its path's whole length; nought for none.
    [[nodiscard]] double lengthOf(world::EntityHandle spline) const;

    /// Paths measured, and measured again, since it began.
    [[nodiscard]] std::uint64_t measured() const noexcept {
        return measured_;
    }

private:
    struct Measured {
        spline::Spline points;
        spline::Walk walk;
    };

    GameSplineComponents components_;
    std::vector<world::ColumnQuery> pointQueries_;
    std::vector<schema::ComponentRuntimeId> splineIds_;
    std::vector<schema::ComponentRuntimeId> reads_;
    std::unique_ptr<world::System> system_;
    std::map<world::EntityHandle, Measured> paths_;
    std::uint64_t measured_ = 0;
};

/// Adds `Splines.along(spline, meters)`, answered from `splines`' paths
/// (none for a null `splines`). Safe for untrusted code: it reads, and a
/// path's walk is bounded by its points.
[[nodiscard]] result::Status addSplineDoors(kest::DoorTable& doors, const GameSplines* splines);

} // namespace rawframe::world_kest
