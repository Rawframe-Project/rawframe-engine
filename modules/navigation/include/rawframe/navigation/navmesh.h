#pragma once

// Navigation (ADR-0056 as amended by D126, D399): a navmesh for one agent,
// baked from triangle geometry by Maul Nav, headlessly and tile by tile,
// the same bytes on every platform for the same geometry and settings; and
// paths over its committed tiles, found the same way everywhere. A navmesh
// changes only by tiles baked again over an area and committed together,
// between searches. Coordinates are the World's: meters, right-handed, +Y
// up. Maul Nav's types stay inside this module.

#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::navigation {

/// A point in the World, in meters.
using Point = std::array<double, 3>;

/// The agent a navmesh is for (ADR-0056's typed profile): its radius,
/// which walkable ground keeps from walls; its height, which it needs free
/// above the ground; the highest ledge it steps up or down; and the
/// steepest slope it walks, in degrees.
struct Agent {
    double radius = 0.4;
    double height = 1.8;
    double step = 0.4;
    double maxSlope = 45;
};

/// How a navmesh is baked: where its tile grid starts, a cell's side on
/// the ground and its height, a tile's side in cells, the agent, the
/// smallest region kept in square meters, and the named limits on the work
/// (Maul Nav's own where nought).
struct Settings {
    Point origin{};
    double cellSize = 0.2;
    double cellHeight = 0.1;
    std::int32_t tileCells = 64;
    Agent agent;
    double minRegionArea = 2;
    std::int32_t maximumTiles = 0;
    std::uint64_t maximumMemoryBytes = 0;
};

/// Triangles the agent may walk on or be blocked by: World points and three
/// indices a triangle, wound either way.
struct Mesh {
    std::vector<Point> vertices;
    std::vector<std::int32_t> indices;
};

/// The ground an area covers, from its smallest X and Z to its largest.
struct Area {
    double minX = 0;
    double minZ = 0;
    double maxX = 0;
    double maxZ = 0;
};

/// How a path search ended: the end reached; short of it, toward the
/// nearest place to it the search could reach (its nodes or length spent,
/// or no way on); or not begun, the start or the end being off the navmesh.
enum class PathEnd : std::uint8_t {
    Reached,
    Partial,
    OffMesh
};

/// A path: how it ended, its corners from the start point on, the corridor
/// pulled tight, and last the end point (or, short of it, the place
/// reached); and its length in meters.
struct Path {
    PathEnd end = PathEnd::OffMesh;
    std::vector<Point> points;
    double length = 0;
};

/// What a navmesh holds and what its bakes did.
struct NavmeshStatistics {
    std::uint32_t tiles = 0;
    std::uint64_t bakes = 0;
    std::uint64_t tilesBaked = 0;
    std::uint64_t polygons = 0;
    std::uint64_t searches = 0;
};

class Navmesh {
public:
    /// An empty navmesh for these settings; refused for a setting Maul Nav
    /// refuses (`Invalid`, with the setting named).
    [[nodiscard]] static result::Result<std::unique_ptr<Navmesh>> create(const Settings& settings);

    Navmesh(const Navmesh&) = delete;
    Navmesh& operator=(const Navmesh&) = delete;
    ~Navmesh();

    /// Bakes every tile `area` touches from `meshes` and commits them in
    /// place of what those tiles held: a tile with no walkable ground left
    /// is removed. Either every tile is replaced or, refused, none is.
    [[nodiscard]] result::Status bake(std::span<const Mesh> meshes, const Area& area);

    /// The path from `from` to `to`, each first moved to the navmesh's
    /// nearest point within `reach` meters along the ground and twice that
    /// up and down; refused only for a point that is not finite.
    [[nodiscard]] result::Result<Path> path(const Point& from, const Point& to, double reach = 2);

    /// The navmesh's nearest point to `point` within `reach`, none off it.
    [[nodiscard]] std::optional<Point> nearest(const Point& point, double reach = 2) const;

    /// A hash of every tile's bytes in place order (x, then z): equal
    /// navmeshes give equal fingerprints on every platform.
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

    [[nodiscard]] const NavmeshStatistics& statistics() const noexcept;

    struct State;

private:
    explicit Navmesh(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::navigation
