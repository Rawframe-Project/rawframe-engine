// The paths a scene's splines make, as a preview is told them (D891), and
// the boxes an outline draws about what an author chose (D907).

#include "../src/viewing.h"
#include "rawframe/spline/spline.h"
#include "rawframe/test/test.h"

#include <cmath>

using namespace rawframe;
using namespace rawframe::authoring_session;

namespace {

scene::FieldValue number(double value) {
    char made[32];
    std::snprintf(made, sizeof made, "%g", value);
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = made};
}

/// A point of `spline` at `index`, standing at (x, y) by a 2D pose.
scene::SceneEntity point(base::Bits128 id, base::Bits128 spline, double index, double x, double y) {
    scene::SceneComponent onIt{
        .name = "game.point",
        .fields = {{.name = "spline", .value = {.kind = scene::FieldValue::Kind::Entity, .entity = spline}}}};
    if (index != 0) {
        onIt.fields.push_back({.name = "index", .value = number(index)});
    }
    return scene::SceneEntity{
        .id = id,
        .name = "point",
        .components = {std::move(onIt),
                       {.name = "rawframe.physics2d.pose",
                        .fields = {{.name = "x", .value = number(x)}, {.name = "y", .value = number(y)}}}}};
}

authoring::ComponentCatalog catalog() {
    authoring::ComponentCatalog made;
    RAWFRAME_EXPECT(
        made.add({.id = schema::ComponentTypeId{base::Bits128{1, 2}}, .name = "game.path", .splinePath = true})
            .has_value());
    RAWFRAME_EXPECT(
        made.add({.id = schema::ComponentTypeId{base::Bits128{1, 1}}, .name = "game.point", .splinePoint = true})
            .has_value());
    return made;
}

} // namespace

RAWFRAME_TEST(ASplinesPathIsWalkedThroughItsPointsInTheirOrder) {
    // A closed square of four points, listed out of their order, and a
    // spline of one point, which makes no path.
    const base::Bits128 kSquare{9, 1};
    const base::Bits128 kAlone{9, 2};
    scene::Scene level;
    level.entities.push_back(scene::SceneEntity{
        .id = kSquare,
        .name = "square",
        .components = {
            {.name = "game.path", .fields = {{.name = "closed", .value = {.kind = scene::FieldValue::Kind::True}}}}}});
    level.entities.push_back(scene::SceneEntity{.id = kAlone, .name = "alone", .components = {{.name = "game.path"}}});
    level.entities.push_back(point({1, 3}, kSquare, 2, 4, 4));
    level.entities.push_back(point({1, 1}, kSquare, 0, 0, 0));
    level.entities.push_back(point({1, 4}, kSquare, 3, 0, 4));
    level.entities.push_back(point({1, 2}, kSquare, 1, 4, 0));
    level.entities.push_back(point({1, 5}, kAlone, 0, 9, 9));
    const auto kPaths = pathsOf(level, catalog());
    RAWFRAME_EXPECT(kPaths.size() == 1);
    if (kPaths.size() != 1) {
        return;
    }
    const spline::Spline kSquared{.points = {{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}, .closed = true};
    RAWFRAME_EXPECT(kPaths[0] == spline::walk(kSquared).points);
    // Through its second point, a quarter of the way round.
    const std::array<double, 3> kSecond = kPaths[0][spline::kPiecesEach];
    RAWFRAME_EXPECT(std::abs(kSecond[0] - 4) < 1e-9 && std::abs(kSecond[1]) < 1e-9);
    // A game with no spline components draws none.
    RAWFRAME_EXPECT(pathsOf(level, authoring::ComponentCatalog{}).empty());
}

RAWFRAME_TEST(AnOutlinedEntitysBoxIsItsBodysAtItsPose) {
    // A 3D box, a 3D capsule, a 2D circle, a 2D tilemap, and an entity with
    // no body; and one asked that is not in the scene, which has no box.
    const auto kBody = [](std::string name, double shape, double width, double height, double depth) {
        scene::SceneComponent made{.name = std::move(name),
                                   .fields = {{.name = "shape", .value = number(shape)},
                                              {.name = "width", .value = number(width)},
                                              {.name = "height", .value = number(height)}}};
        if (depth != 0) {
            made.fields.push_back({.name = "depth", .value = number(depth)});
        }
        return made;
    };
    const auto kPose = [](std::string name, double x, double y) {
        return scene::SceneComponent{.name = std::move(name),
                                     .fields = {{.name = "x", .value = number(x)}, {.name = "y", .value = number(y)}}};
    };
    scene::Scene level;
    level.entities.push_back(scene::SceneEntity{
        .id = base::Bits128{3, 1},
        .name = "crate",
        .components = {kBody("rawframe.physics3d.body", 1, 1, 2, 3), kPose("rawframe.physics3d.pose", 4, 5)}});
    level.entities.push_back(scene::SceneEntity{
        .id = base::Bits128{3, 2},
        .name = "figure",
        .components = {kBody("rawframe.physics3d.body", 2, 0.5, 1, 0), kPose("rawframe.physics3d.pose", 0, 1)}});
    level.entities.push_back(scene::SceneEntity{
        .id = base::Bits128{3, 3},
        .name = "ball",
        .components = {kBody("rawframe.physics2d.body", 0, 0.25, 0, 0), kPose("rawframe.physics2d.pose", 1, 1)}});
    level.entities.push_back(scene::SceneEntity{
        .id = base::Bits128{3, 4},
        .name = "ground",
        .components = {kBody("rawframe.physics2d.body", 3, 0, 0, 0), kPose("rawframe.physics2d.pose", 0, 0)}});
    level.entities.push_back(scene::SceneEntity{
        .id = base::Bits128{3, 5}, .name = "marker", .components = {kPose("rawframe.physics3d.pose", 2, 2)}});
    RAWFRAME_EXPECT(halfOf(level, base::Bits128{3, 1}) == (std::array<double, 3>{1, 2, 3}));
    RAWFRAME_EXPECT(halfOf(level, base::Bits128{3, 2}) == (std::array<double, 3>{0.5, 1.5, 0.5}));
    RAWFRAME_EXPECT(halfOf(level, base::Bits128{3, 3}) == (std::array<double, 3>{0.25, 0.25, 0}));
    RAWFRAME_EXPECT(halfOf(level, base::Bits128{3, 4}) == (std::array<double, 3>{0.5, 0.5, 0}));
    RAWFRAME_EXPECT(halfOf(level, base::Bits128{3, 5}) == (std::array<double, 3>{0.5, 0.5, 0.5}));
    const std::array<authoring::SessionOutline, 1> kAsked = {
        authoring::SessionOutline{.color = {1, 0, 0}, .entities = {base::Bits128{3, 1}, base::Bits128{3, 9}}}};
    const document::Value kOutlines = outlinesOf(level, kAsked);
    RAWFRAME_EXPECT(document::writeCompact(kOutlines) ==
                    R"([{"color":[1,0,0],"boxes":[{"center":[4,5,0],"half":[1,2,3]}]}])");
}
