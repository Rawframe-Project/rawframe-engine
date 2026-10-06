#pragma once

// A game's navigation (ADR-0056, D400): one navmesh for the agent its
// navigation line names, baked on the server from the static bodies'
// triangles (rawframe.physics3d's static geometry) by a system after the
// physics steps, and baked again over the ground a change of them touches;
// and the `Navigation.next` door its programs ask the way with, which
// answers from the navmesh as the last bake left it, and on a machine that
// bakes none (a client) answers that there is no way.

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/kest/doors.h"
#include "rawframe/navigation/avoidance.h"
#include "rawframe/navigation/navmesh.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/schema/layout.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The system that bakes, after the physics steps.
inline constexpr std::string_view kNavigationBakeSystem = "rawframe.navigation.bake";

/// The system that steers agents round each other (D411), before the 3D
/// physics step: a game's own system that sets their velocities runs
/// before it.
inline constexpr std::string_view kNavigationAvoidSystem = "rawframe.navigation.avoid";

/// An agent (D411), `rawframe.navigation.Agent` in Kest, on an entity with a
/// 3D pose and velocity: the velocity a game's systems write is where the
/// agent would go, and the avoid system makes it the nearest velocity that
/// keeps clear of the other agents, ORCA's. `velocityX` and `velocityZ` are
/// the avoid system's, what it last gave.
struct NavigationAgent {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("31c10f52-6075-4dcb-9f8e-fa0aad7e3104");
    static constexpr std::string_view kComponentName = "rawframe.navigation.agent";

    float radius = 0.5F;
    float maxSpeed = 1;
    float priority = 1;
    float velocityX = 0;
    float velocityZ = 0;
};

/// The agent's layout, as a program declares it.
[[nodiscard]] const schema::ComponentLayout& navigationAgentLayout() noexcept;

/// Reads a navigation line's words after its keyword into `into`; false for
/// words out of its form or a value not above nought.
[[nodiscard]] bool readNavigationLine(std::span<const std::string_view> words, GameNavigation& into);

struct GameNavmeshStatistics {
    std::uint64_t bakes = 0;
    /// Agent steps the avoid system took.
    std::uint64_t avoided = 0;
    /// Bakes Maul Nav refused, the navmesh left as it was.
    std::uint64_t bakesRefused = 0;
    std::uint64_t tilesBaked = 0;
    std::uint32_t tiles = 0;
    std::uint64_t polygons = 0;
    std::uint64_t searches = 0;
    std::uint64_t fingerprint = 0;
};

class GameNavmesh final : public world_runtime::SystemContributor {
public:
    explicit GameNavmesh(const GameNavigation& navigation) noexcept;
    GameNavmesh(const GameNavmesh&) = delete;
    GameNavmesh& operator=(const GameNavmesh&) = delete;
    ~GameNavmesh() override;

    /// The physics whose static geometry it bakes, until it goes (null).
    void attach(const physics3d::Physics3DQueries* queries) noexcept;

    [[nodiscard]] result::Status declareSystems(const schema::SchemaRegistry& registry,
                                                std::vector<world::SystemDeclaration>& systems) noexcept override;

    /// Bakes what changed of the static geometry since the last bake: the
    /// whole of it the first time, then the ground every body made, moved,
    /// or removed covered before and after.
    void bake();

    /// The navmesh, once a bake made it; null before.
    [[nodiscard]] navigation::Navmesh* navmesh() noexcept;

    /// Steers every agent round the others for one step of `seconds`
    /// (D411).
    [[nodiscard]] result::Status avoid(world::World& world, double seconds);

    [[nodiscard]] GameNavmeshStatistics statistics() const noexcept;
    /// Logs the statistics as `navigation_summary`, the fingerprint as
    /// sixteen hex digits.
    void report(diagnostics::Emitter& emitter) const;

private:
    /// A static body's box: its low and high corners.
    using Box = std::array<std::array<double, 3>, 2>;

    GameNavigation navigation_;
    const physics3d::Physics3DQueries* queries_ = nullptr;
    std::unique_ptr<navigation::Navmesh> navmesh_;
    std::unique_ptr<world::System> system_;
    physics3d::StaticGeometry geometry_;
    std::map<world::EntityHandle, Box> boxes_;
    std::uint64_t revision_ = 0;
    bool baked_ = false;
    std::uint64_t refused_ = 0;
    /// The avoid system and what it works with, when the game has agents.
    std::unique_ptr<world::System> avoidSystem_;
    std::unique_ptr<navigation::Avoidance> avoidance_;
    using AgentQuery = world::
        Query<world::Write<NavigationAgent>, world::Read<physics3d::Pose3D>, world::Write<physics3d::Velocity3D>>;
    std::optional<AgentQuery> agentQuery_;
    std::vector<schema::ComponentRuntimeId> avoidReads_;
    std::vector<schema::ComponentRuntimeId> avoidWrites_;
    struct AgentRow {
        world::EntityHandle entity;
        NavigationAgent* agent = nullptr;
        const physics3d::Pose3D* pose = nullptr;
        physics3d::Velocity3D* velocity = nullptr;
    };
    std::vector<AgentRow> rows_;
    std::vector<navigation::AvoidingAgent> agents_;
    std::vector<navigation::Ground> velocities_;
    std::uint64_t avoided_ = 0;
};

/// Adds `Navigation.next(fromX, fromY, fromZ, toX, toY, toZ)`, the next
/// corner of the way from one point to another and how far the way is,
/// answered from `navmesh`'s navmesh (none while it has none, and for a null
/// `navmesh`). Safe for untrusted code: it reads, and its search is bounded.
[[nodiscard]] result::Status addNavigationDoors(kest::DoorTable& doors, GameNavmesh* navmesh);

} // namespace rawframe::world_kest
