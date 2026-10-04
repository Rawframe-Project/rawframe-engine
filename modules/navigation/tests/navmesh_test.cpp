// Navmeshes (D399): a floor with a wall baked for an agent, a path around
// the wall's end, the same bytes from the same geometry, the wall removed
// by baking its tiles again and the path then straight, an island the
// path cannot reach, a point off the navmesh, and settings Maul Nav
// refuses.

#include "rawframe/navigation/errors.h"
#include "rawframe/navigation/navmesh.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>

using namespace rawframe;
using namespace rawframe::navigation;

namespace {

/// A box's top and sides.
Mesh box(Point low, Point high) {
    Mesh mesh;
    for (int corner = 0; corner < 8; ++corner) {
        mesh.vertices.push_back({(corner & 1) != 0 ? high[0] : low[0],
                                 (corner & 2) != 0 ? high[1] : low[1],
                                 (corner & 4) != 0 ? high[2] : low[2]});
    }
    // Top, then the four sides, wound outward.
    mesh.indices = {2, 6, 7, 2, 7, 3, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5,
                    0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1};
    return mesh;
}

/// A flat floor at height 0.
Mesh floor(double minX, double minZ, double maxX, double maxZ) {
    return Mesh{.vertices = {{minX, 0, minZ}, {maxX, 0, minZ}, {maxX, 0, maxZ}, {minX, 0, maxZ}},
                .indices = {0, 2, 1, 0, 3, 2}};
}

bool near(const Point& point, const Point& expected, double within) {
    return std::abs(point[0] - expected[0]) <= within && std::abs(point[1] - expected[1]) <= within &&
           std::abs(point[2] - expected[2]) <= within;
}

} // namespace

RAWFRAME_TEST(APathGoesAroundAWallAndStraightOnceItIsGone) {
    auto made = Navmesh::create({});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Navmesh& navmesh = **made;
    const Area kLot{.minX = 0, .minZ = 0, .maxX = 20, .maxZ = 20};
    // A wall two meters high from one side of the floor to fourteen meters
    // in.
    const std::vector<Mesh> kWalled = {floor(0, 0, 20, 20), box({8, 0, 0}, {12, 2, 14})};
    RAWFRAME_EXPECT(navmesh.bake(kWalled, kLot).has_value());
    RAWFRAME_EXPECT(navmesh.statistics().tiles == 4 && navmesh.statistics().polygons > 0);
    const auto kAround = navmesh.path({2, 0, 2}, {18, 0, 2});
    RAWFRAME_EXPECT(kAround.has_value());
    if (!kAround.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kAround->end == PathEnd::Reached && kAround->points.size() >= 4);
    // Past the wall's end, kept the agent's radius from it (to the cell),
    // and back; on the navmesh's surface, a cell's height over the floor.
    bool past = false;
    for (const Point& kCorner : kAround->points) {
        past = past || (kCorner[2] > 14.3 && kCorner[2] < 15.2);
    }
    RAWFRAME_EXPECT(past && kAround->length > 28 && near(kAround->points.back(), {18, 0.1, 2}, 0.01));
    RAWFRAME_EXPECT(kAround->points.size() == 4 && near(kAround->points[1], {7.8, 0.1, 14.4}, 0.01) &&
                    near(kAround->points[2], {12.4, 0.1, 14.4}, 0.01));
    std::printf("around: %zu points, %.3f m, fingerprint %016llx\n",
                kAround->points.size(),
                kAround->length,
                static_cast<unsigned long long>(navmesh.fingerprint()));

    // The same geometry gives the same bytes, here and on every platform.
    RAWFRAME_EXPECT(navmesh.fingerprint() == 0x1747c8991ba56575ULL);
    auto again = Navmesh::create({});
    RAWFRAME_EXPECT(again.has_value() && (*again)->bake(kWalled, kLot).has_value());
    RAWFRAME_EXPECT(again.has_value() && (*again)->fingerprint() == navmesh.fingerprint());

    // The wall gone from the tiles it stood on: straight across.
    const std::vector<Mesh> kOpen = {floor(0, 0, 20, 20)};
    RAWFRAME_EXPECT(navmesh.bake(kOpen, {.minX = 8, .minZ = 0, .maxX = 12, .maxZ = 14}).has_value());
    const auto kStraight = navmesh.path({2, 0, 2}, {18, 0, 2});
    RAWFRAME_EXPECT(kStraight.has_value() && kStraight->end == PathEnd::Reached && kStraight->points.size() == 2 &&
                    std::abs(kStraight->length - 16) < 0.05);
    RAWFRAME_EXPECT(navmesh.statistics().bakes == 2 && navmesh.statistics().searches == 2);
}

RAWFRAME_TEST(AnIslandIsReachedOnlyPartWayAndAPointOffTheNavmeshNotAtAll) {
    auto made = Navmesh::create({});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    Navmesh& navmesh = **made;
    const std::vector<Mesh> kIslands = {floor(0, 0, 8, 8), floor(12, 0, 20, 8)};
    RAWFRAME_EXPECT(navmesh.bake(kIslands, {.minX = 0, .minZ = 0, .maxX = 20, .maxZ = 8}).has_value());
    const auto kAcross = navmesh.path({2, 0, 2}, {18, 0, 2});
    RAWFRAME_EXPECT(kAcross.has_value() && kAcross->end == PathEnd::Partial && !kAcross->points.empty());
    // No edge of the start island leads on: the path is its start.
    RAWFRAME_EXPECT(kAcross.has_value() && kAcross->points.size() == 1);
    const auto kOff = navmesh.path({2, 0, 2}, {2, 0, 50});
    RAWFRAME_EXPECT(kOff.has_value() && kOff->end == PathEnd::OffMesh && kOff->points.empty());
    RAWFRAME_EXPECT(!navmesh.nearest({2, 0, 50}).has_value());
    const auto kNearest = navmesh.nearest({2, 1, 2});
    RAWFRAME_EXPECT(kNearest.has_value() && near(*kNearest, {2, 0.1, 2}, 0.01));
    const auto kNotFinite = navmesh.path({NAN, 0, 0}, {2, 0, 2});
    RAWFRAME_EXPECT(!kNotFinite.has_value() && kNotFinite.error().code() == code(NavigationError::Invalid));
}

RAWFRAME_TEST(SettingsAndAreasMaulNavRefusesAreRefused) {
    const auto kFlat = Navmesh::create({.cellSize = 0});
    RAWFRAME_EXPECT(!kFlat.has_value() && kFlat.error().code() == code(NavigationError::Invalid));
    const auto kTall = Navmesh::create({.agent = {.height = 0}});
    RAWFRAME_EXPECT(!kTall.has_value());
    auto made = Navmesh::create({.maximumTiles = 2});
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    const std::vector<Mesh> kFloor = {floor(0, 0, 20, 20)};
    // Four tiles where two may be, a backward area, and a mesh that is not
    // whole triangles: nothing is committed.
    RAWFRAME_EXPECT(!(*made)->bake(kFloor, {.minX = 0, .minZ = 0, .maxX = 20, .maxZ = 20}).has_value());
    RAWFRAME_EXPECT(!(*made)->bake(kFloor, {.minX = 5, .minZ = 0, .maxX = 1, .maxZ = 1}).has_value());
    const std::vector<Mesh> kTorn = {Mesh{.vertices = {{0, 0, 0}, {1, 0, 0}}, .indices = {0, 1}}};
    RAWFRAME_EXPECT(!(*made)->bake(kTorn, {.minX = 0, .minZ = 0, .maxX = 1, .maxZ = 1}).has_value());
    RAWFRAME_EXPECT((*made)->statistics().tiles == 0 && (*made)->statistics().bakes == 0);
}
