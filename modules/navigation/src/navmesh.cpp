#include "rawframe/navigation/navmesh.h"

#include "rawframe/navigation/errors.h"

#include <cmath>
#include <map>
#include <maul-nav/bake.h>
#include <maul-nav/navmesh.h>
#include <maul-nav/query.h>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::navigation {

namespace {

std::unexpected<result::Error> refuse(NavigationError error, std::string_view why) {
    const result::ErrorClass kClass =
        error == NavigationError::Invalid ? result::ErrorClass::InvalidArgument : result::ErrorClass::ResourceExhausted;
    return std::unexpected<result::Error>{result::fail(kClass, kNavigationDomain, code(error), why).error()};
}

/// Maul Nav's answer as this module's.
result::Status checked(mnavResult outcome, std::string_view why) {
    switch (outcome) {
    case mnav_success:
        return {};
    case mnav_errorLimit:
        return refuse(NavigationError::Limit, why);
    case mnav_errorCapacity:
        return refuse(NavigationError::Capacity, why);
    default:
        return std::unexpected<result::Error>{
            refuse(NavigationError::Invalid, why).error().withContext("outcome", std::to_string(outcome))};
    }
}

bool finite(const Point& point) noexcept {
    return std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
}

mnavPos3 posOf(const Point& point) noexcept {
    return mnavPos3{.x = point[0], .y = point[1], .z = point[2]};
}

Point pointOf(const mnavPos3& point) noexcept {
    return {point.x, point.y, point.z};
}

/// A tile's place in the grid: its column and row.
using Place = std::pair<std::int32_t, std::int32_t>;

/// A committed tile: its bytes and its polygons.
struct Tile {
    std::vector<std::uint8_t> bytes;
    std::uint64_t polygons = 0;
};

} // namespace

struct Navmesh::State {
    mnavBakeDef def{};
    mnavNavmesh* navmesh = nullptr;
    mnavBaker* baker = nullptr;
    mnavQuery* query = nullptr;
    /// Every committed tile, in place order.
    std::map<Place, Tile> tiles;
    NavmeshStatistics statistics;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        mnavDestroyQuery(query);
        mnavDestroyBaker(baker);
        mnavDestroyNavmesh(navmesh);
    }

    [[nodiscard]] double tileSide() const noexcept {
        return static_cast<double>(def.cellSize) * def.tileCells;
    }

    /// Puts back what `staged` held before a bake that failed, so nothing
    /// it staged reaches a later commit.
    void unstage(const std::vector<Place>& staged) {
        for (const Place& kPlace : staged) {
            const auto kFound = tiles.find(kPlace);
            if (kFound != tiles.end()) {
                static_cast<void>(mnavStageTile(navmesh, kFound->second.bytes.data(), kFound->second.bytes.size()));
            } else {
                static_cast<void>(mnavStageTileRemoval(navmesh, kPlace.first, kPlace.second));
            }
        }
    }

    [[nodiscard]] mnavNearest nearestOf(const Point& point, double reach) const noexcept {
        mnavNearest nearest{};
        const auto kReach = static_cast<float>(reach);
        if (mnavFindNearest(
                navmesh, nullptr, posOf(point), mnavVec3{.x = kReach, .y = kReach * 2, .z = kReach}, &nearest) !=
            mnav_success) {
            nearest.polygon.slot = 0;
        }
        return nearest;
    }
};

Navmesh::Navmesh(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Navmesh::~Navmesh() = default;

result::Result<std::unique_ptr<Navmesh>> Navmesh::create(const Settings& settings) {
    auto state = std::make_unique<State>();
    mnavBakeDef& def = state->def;
    def = mnavDefaultBakeDef();
    def.origin = posOf(settings.origin);
    def.cellSize = static_cast<float>(settings.cellSize);
    def.cellHeight = static_cast<float>(settings.cellHeight);
    def.tileCells = settings.tileCells;
    def.agent = mnavAgentProfile{.radius = static_cast<float>(settings.agent.radius),
                                 .height = static_cast<float>(settings.agent.height),
                                 .stepHeight = static_cast<float>(settings.agent.step),
                                 .maxSlopeDegrees = static_cast<float>(settings.agent.maxSlope)};
    def.minRegionArea = static_cast<float>(settings.minRegionArea);
    if (settings.maximumTiles > 0) {
        def.limits.tiles = settings.maximumTiles;
    }
    if (settings.maximumMemoryBytes > 0) {
        def.limits.memoryBytes = settings.maximumMemoryBytes;
    }
    // Tiles are baked again in place as the World changes.
    def.tier = mnav_tierDynamic;
    const mnavBakeDefResult kMade = mnavCreateNavmesh(&def, &state->navmesh);
    if (kMade.result != mnav_success) {
        return std::unexpected<result::Error>{checked(kMade.result, "a navmesh's settings were refused")
                                                  .error()
                                                  .withContext("setting", std::to_string(kMade.setting))};
    }
    RAWFRAME_TRY(checked(mnavCreateBaker(&def, &state->baker).result, "a navmesh's baker could not be made"));
    const mnavQueryDef kQuery = mnavDefaultQueryDef();
    RAWFRAME_TRY(checked(mnavCreateQuery(&kQuery, &state->query), "a navmesh's search could not be made"));
    return std::unique_ptr<Navmesh>{new Navmesh{std::move(state)}};
}

result::Status Navmesh::bake(std::span<const Mesh> meshes, const Area& area) {
    State& state = *state_;
    const Point kOrigin = pointOf(state.def.origin);
    // The meshes in the grid's frame, as Maul Nav reads them.
    std::vector<std::vector<mnavVec3>> vertices;
    std::vector<mnavTriangleMesh> input;
    vertices.reserve(meshes.size());
    for (const Mesh& kMesh : meshes) {
        if (kMesh.indices.size() % 3 != 0) {
            return refuse(NavigationError::Invalid, "a mesh's indices are not whole triangles");
        }
        std::vector<mnavVec3>& relative = vertices.emplace_back();
        relative.reserve(kMesh.vertices.size());
        for (const Point& kVertex : kMesh.vertices) {
            relative.push_back(mnavVec3{.x = static_cast<float>(kVertex[0] - kOrigin[0]),
                                        .y = static_cast<float>(kVertex[1] - kOrigin[1]),
                                        .z = static_cast<float>(kVertex[2] - kOrigin[2])});
        }
        input.push_back(mnavTriangleMesh{.vertices = relative.data(),
                                         .vertexCount = static_cast<std::int32_t>(relative.size()),
                                         .indices = kMesh.indices.data(),
                                         .triangleCount = static_cast<std::int32_t>(kMesh.indices.size() / 3),
                                         .areas = nullptr});
    }
    const double kSide = state.tileSide();
    const auto kColumn = [&](double x) {
        return static_cast<std::int32_t>(std::floor((x - kOrigin[0]) / kSide));
    };
    const auto kRow = [&](double z) {
        return static_cast<std::int32_t>(std::floor((z - kOrigin[2]) / kSide));
    };
    if (!std::isfinite(area.minX) || !std::isfinite(area.minZ) || !std::isfinite(area.maxX) ||
        !std::isfinite(area.maxZ) || area.maxX < area.minX || area.maxZ < area.minZ ||
        (kColumn(area.maxX) - kColumn(area.minX) + 1.0) * (kRow(area.maxZ) - kRow(area.minZ) + 1.0) >
            static_cast<double>(state.def.limits.tiles)) {
        return refuse(NavigationError::Invalid, "a bake's area is not finite, is backward, or is past the tiles");
    }
    std::vector<Place> staged;
    std::map<Place, Tile> made;
    for (std::int32_t column = kColumn(area.minX); column <= kColumn(area.maxX); ++column) {
        for (std::int32_t row = kRow(area.minZ); row <= kRow(area.maxZ); ++row) {
            mnavBakeReport report{};
            const result::Status kBaked = checked(
                mnavBakeTile(state.baker, input.data(), static_cast<std::int32_t>(input.size()), column, row, &report),
                "a navmesh tile could not be baked");
            if (!kBaked.has_value()) {
                state.unstage(staged);
                return std::unexpected<result::Error>{kBaked.error()
                                                          .clone()
                                                          .withContext("column", std::to_string(column))
                                                          .withContext("row", std::to_string(row))
                                                          .withContext("stage", std::to_string(report.stage))};
            }
            staged.emplace_back(column, row);
            if (report.polygons == 0) {
                static_cast<void>(mnavStageTileRemoval(state.navmesh, column, row));
                continue;
            }
            std::size_t size = 0;
            static_cast<void>(mnavCopyBakedTile(state.baker, nullptr, 0, &size));
            std::vector<std::uint8_t> bytes(size);
            mnavTileResult staging{};
            if (mnavCopyBakedTile(state.baker, bytes.data(), bytes.size(), &size) == mnav_success) {
                staging = mnavStageTile(state.navmesh, bytes.data(), bytes.size());
            } else {
                staging.result = mnav_errorCapacity;
            }
            if (staging.result != mnav_success) {
                state.unstage(staged);
                return checked(staging.result, "a navmesh tile could not be staged");
            }
            made.emplace(Place{column, row},
                         Tile{.bytes = std::move(bytes), .polygons = static_cast<std::uint64_t>(report.polygons)});
        }
    }
    if (result::Status committed = checked(mnavCommit(state.navmesh), "a navmesh's tiles could not be committed");
        !committed.has_value()) {
        state.unstage(staged);
        return committed;
    }
    for (const Place& kPlace : staged) {
        state.tiles.erase(kPlace);
    }
    state.tiles.merge(made);
    ++state.statistics.bakes;
    state.statistics.tilesBaked += staged.size();
    state.statistics.tiles = static_cast<std::uint32_t>(state.tiles.size());
    state.statistics.polygons = 0;
    for (const auto& [kPlace, kTile] : state.tiles) {
        state.statistics.polygons += kTile.polygons;
    }
    return {};
}

result::Result<Path> Navmesh::path(const Point& from, const Point& to, double reach) {
    State& state = *state_;
    if (!finite(from) || !finite(to) || !(std::isfinite(reach) && reach >= 0)) {
        return refuse(NavigationError::Invalid, "a path's ends or reach are not finite");
    }
    ++state.statistics.searches;
    const mnavNearest kStart = state.nearestOf(from, reach);
    const mnavNearest kEnd = state.nearestOf(to, reach);
    if (kStart.polygon.slot == 0 || kEnd.polygon.slot == 0) {
        return Path{};
    }
    mnavPath found{};
    RAWFRAME_TRY(checked(
        mnavFindPath(
            state.query, state.navmesh, nullptr, kStart.polygon, kStart.point, kEnd.polygon, kEnd.point, &found),
        "a path could not be searched for"));
    Path path{.end = found.end == mnav_pathFound ? PathEnd::Reached : PathEnd::Partial, .length = 0};
    path.points.reserve(static_cast<std::size_t>(found.pointCount));
    for (std::int32_t at = 0; at < found.pointCount; ++at) {
        path.points.push_back(pointOf(found.points[at]));
        if (at > 0) {
            const Point& kFrom = path.points[static_cast<std::size_t>(at) - 1];
            const Point& kTo = path.points.back();
            path.length += std::hypot(kTo[0] - kFrom[0], kTo[1] - kFrom[1], kTo[2] - kFrom[2]);
        }
    }
    return path;
}

std::optional<Point> Navmesh::nearest(const Point& point, double reach) const {
    if (!finite(point) || !(std::isfinite(reach) && reach >= 0)) {
        return std::nullopt;
    }
    const mnavNearest kNearest = state_->nearestOf(point, reach);
    if (kNearest.polygon.slot == 0) {
        return std::nullopt;
    }
    return pointOf(kNearest.point);
}

std::uint64_t Navmesh::fingerprint() const noexcept {
    std::uint64_t hash = MNAV_HASH_INIT;
    for (const auto& [kPlace, kTile] : state_->tiles) {
        const std::array<std::int32_t, 2> kAt{kPlace.first, kPlace.second};
        hash = mnavHash64(hash, kAt.data(), static_cast<std::int32_t>(sizeof kAt));
        hash = mnavHash64(hash, kTile.bytes.data(), static_cast<std::int32_t>(kTile.bytes.size()));
    }
    return hash;
}

const NavmeshStatistics& Navmesh::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::navigation
