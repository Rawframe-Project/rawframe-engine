// A model drawn where its entity is placed in place of its pose:
// at the place, turned as the pose turns, its lift still along the pose's
// up; at the pose again once it is not placed. And where a headset's hand
// is this frame, moved as it says, before its place, and placed again
// where the hand is not located; a headset's hand placed in the
// World by the player's camera as its eyes are. And left out while an
// editor's preview hides its layer.

#include "../src/camera.h"
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

RAWFRAME_TEST(AModelFollowsAHeadsetsHandBeforeItsPlace) {
    Rig rig;
    const world::EntityHandle kBall =
        rig.spawn(Model{.mesh = kBox, .color = 0xFF0000FF}, physics3d::Pose3D{.x = 1, .z = -5, .qw = 1});
    Placed placed{.placed = true, .x = -1, .y = 2, .z = -4};
    Tracked tracked{.hand = 2, .x = 0, .y = 0.5, .z = 0};
    RAWFRAME_EXPECT(rig.world.insertErased(kBall, *rig.schema->find(kPlacedId), &placed).has_value());
    RAWFRAME_EXPECT(rig.world.insertErased(kBall, *rig.schema->find(kTrackedId), &tracked).has_value());
    const auto kDrawnAt = [&rig](const HandsInWorld* hands, float x, float y, float z, bool followed) {
        const SceneFrame& kFrame = rig.frame({.eye = {0, 0, 0}}, hands);
        RAWFRAME_EXPECT(kFrame.draws.size() == 1);
        if (kFrame.draws.size() != 1) {
            return;
        }
        RAWFRAME_EXPECT(kFrame.tracked == (followed ? 1U : 0U) && kFrame.placed == (followed ? 0U : 1U));
        RAWFRAME_EXPECT(near(kFrame.draws[0].model[12], x) && near(kFrame.draws[0].model[13], y) &&
                        near(kFrame.draws[0].model[14], z));
    };
    // No headset: placed.
    kDrawnAt(nullptr, -1, 2, -4, false);
    // The right hand located: there, half a meter up.
    const HandsInWorld kRight{std::nullopt, std::array<double, 3>{0.5, 1, -3}};
    kDrawnAt(&kRight, 0.5F, 1.5F, -3, true);
    // Only the left located: placed again.
    const HandsInWorld kLeft{std::array<double, 3>{0.5, 1, -3}, std::nullopt};
    kDrawnAt(&kLeft, -1, 2, -4, false);
    // A hand of nought follows none.
    tracked.hand = 0;
    RAWFRAME_EXPECT(rig.world.insertErased(kBall, *rig.schema->find(kTrackedId), &tracked).has_value());
    kDrawnAt(&kRight, -1, 2, -4, false);
    // The play space: the player's eye its origin, turned a quarter by its
    // yaw, so the headset's -Z ahead is the World's -X.
    const SceneCamera kSeated{.eye = {2, 1.2, 3}, .yaw = 1.5707963F};
    const std::array<double, 3> kAhead = inPlaySpace(kSeated, {0, -0.3F, -0.5F});
    RAWFRAME_EXPECT(near(static_cast<float>(kAhead[0]), 1.5F) && near(static_cast<float>(kAhead[1]), 0.9F) &&
                    near(static_cast<float>(kAhead[2]), 3));
}

RAWFRAME_TEST(AModelOnAHiddenLayerIsLeftOut) {
    // Two boxes ahead of the eye, one on layer 2.
    Rig rig;
    rig.spawn(Model{.mesh = kBox, .color = 0xFF0000FF}, physics3d::Pose3D{.x = -1, .z = -5, .qw = 1});
    const world::EntityHandle kLayered =
        rig.spawn(Model{.mesh = kBox, .color = 0xFF0000FF}, physics3d::Pose3D{.x = 1, .z = -5, .qw = 1});
    world::Layer onTwo{.layer = 2};
    RAWFRAME_EXPECT(
        rig.world.insertErased(kLayered, *rig.schema->find(world::Layer::kComponentTypeId), &onTwo).has_value());
    RAWFRAME_EXPECT(rig.frame({.eye = {0, 0, 0}}).draws.size() == 2);
    // Layer 3 hidden leaves both; layer 2 hidden leaves one.
    rig.scene->hideLayers({3});
    RAWFRAME_EXPECT(rig.frame({.eye = {0, 0, 0}}).draws.size() == 2);
    rig.scene->hideLayers({2});
    RAWFRAME_EXPECT(rig.frame({.eye = {0, 0, 0}}).draws.size() == 1 && rig.scene->modelsHidden() == 1);
    rig.scene->hideLayers({});
    RAWFRAME_EXPECT(rig.frame({.eye = {0, 0, 0}}).draws.size() == 2);
}
