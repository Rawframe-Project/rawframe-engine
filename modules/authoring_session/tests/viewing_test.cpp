// The paths a scene's splines make, as a preview is told them (D891).

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
