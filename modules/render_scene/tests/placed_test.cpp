// A model drawn where its entity is placed in place of its pose (D665):
// at the place, turned as the pose turns, its lift still along the pose's
// up; at the pose again once it is not placed.

#include "rawframe/render_scene/components.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <cmath>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

RAWFRAME_TEST(AModelIsDrawnWhereItsEntityIsPlaced) {
    Rig rig;
    // A box turned a quarter about +Y, posed a meter right of five ahead of
    // the eye, lifted half a meter.
    const float kHalf = std::sqrt(0.5F);
    const world::EntityHandle kBoxed = rig.spawn(Model{.mesh = kBox, .scaleX = 2, .color = 0xFF0000FF, .lift = 0.5F},
                                                 physics3d::Pose3D{.x = 1, .z = -5, .qy = kHalf, .qw = kHalf});
    Placed placed{.placed = false, .x = -1, .y = 2, .z = -4};
    RAWFRAME_EXPECT(rig.world.insertErased(kBoxed, *rig.schema->find(kPlacedId), &placed).has_value());
    const auto kDrawnAt = [&rig](float x, float y, float z) {
        const SceneFrame& kFrame = rig.frame({.eye = {0, 0, 0}});
        RAWFRAME_EXPECT(kFrame.draws.size() == 1);
        if (kFrame.draws.size() != 1) {
            return;
        }
        const SceneDraw& kDraw = kFrame.draws[0];
        RAWFRAME_EXPECT(kFrame.placed == (near(z, -4) ? 1U : 0U));
        RAWFRAME_EXPECT(near(kDraw.model[12], x) && near(kDraw.model[13], y) && near(kDraw.model[14], z));
        // Turned as the pose is: its X along -Z.
        RAWFRAME_EXPECT(near(kDraw.model[0], 0) && near(kDraw.model[2], -2));
    };
    // Not placed: at the pose, lifted.
    kDrawnAt(1, 0.5F, -5);
    placed.placed = true;
    RAWFRAME_EXPECT(rig.world.insertErased(kBoxed, *rig.schema->find(kPlacedId), &placed).has_value());
    kDrawnAt(-1, 2.5F, -4);
    placed.placed = false;
    RAWFRAME_EXPECT(rig.world.insertErased(kBoxed, *rig.schema->find(kPlacedId), &placed).has_value());
    kDrawnAt(1, 0.5F, -5);
}
