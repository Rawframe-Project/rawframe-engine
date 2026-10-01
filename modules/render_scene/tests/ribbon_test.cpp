// Trails and beams in the view stage (D354): a trail leaves a point each
// time its pose has gone its spacing, runs from its pose through them
// narrowing over their life, and lets go of the dead; a beam follows its
// bent curve in its segments, its texture repeated along its length;
// ribbons out of view are left alone, the nearest kept up to the limit
// and drawn farthest first, and values past a limit point held and
// counted.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string_view>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

constexpr auto kModelId = schema::ComponentTypeId::fromText("3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kTrailId = schema::ComponentTypeId::fromText("8d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kBeamId = schema::ComponentTypeId::fromText("9d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");

template <typename T> schema::ComponentDescriptor plain(schema::ComponentTypeId id, std::string_view name) {
    return schema::ComponentDescriptor{
        .id = id, .name = name, .size = sizeof(T), .alignment = alignof(T), .plainData = true};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(plain<Model>(kModelId, "test.model"));
    builder.add(plain<Trail>(kTrailId, "test.trail"));
    builder.add(plain<Beam>(kBeamId, "test.beam"));
    builder.add<physics3d::Pose3D>();
    return *builder.freeze();
}

bool near(float a, float b) {
    return std::abs(a - b) < 1e-4F;
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Scene> scene;

    explicit Rig(SceneLimits limits = {}) {
        scene =
            *Scene::create(*schema, {.models = {kModelId}, .trails = {kTrailId}, .beams = {kBeamId}, .limits = limits});
    }

    template <typename Ribbon>
    world::EntityHandle place(schema::ComponentTypeId id, Ribbon ribbon, double x, double z) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(id), &ribbon).has_value());
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<physics3d::Pose3D>(), physics3d::Pose3D{.x = x, .z = z}).has_value());
        return kEntity;
    }

    void move(world::EntityHandle entity, double x) {
        auto* pose = world.get(entity, *schema->key<physics3d::Pose3D>());
        RAWFRAME_EXPECT(pose != nullptr);
        if (pose != nullptr) {
            pose->x = x;
        }
    }

    const SceneFrame& frame(float elapsed) {
        scene->extract(world);
        return scene->queue({.fovY = 1.2F, .aspect = 1, .elapsed = elapsed});
    }
};

/// A trail living a second, a point every half meter, a fifth of a meter
/// wide narrowing to nothing, white fading out.
Trail streak() {
    return Trail{.lifetime = 1, .spacing = 0.5F, .widthStart = 0.2F, .widthEnd = 0, .colorEnd = 0xFFFFFF00};
}

/// A beam four meters right, bent a meter up, in four segments, its
/// texture repeated every two meters.
Beam arc() {
    return Beam{.toX = 4, .bendY = 1, .segments = 4, .widthStart = 0.2F, .widthEnd = 0.1F, .textureLength = 2};
}

} // namespace

RAWFRAME_TEST(ATrailLeavesPointsAndLetsThemGo) {
    Rig rig;
    const world::EntityHandle kTrail = rig.place(kTrailId, streak(), 0, -5);
    // Standing still, a point where it stands: nothing to draw.
    RAWFRAME_EXPECT(rig.frame(0).ribbons.empty());
    // Gone a meter: from the pose to where it was.
    rig.move(kTrail, 1);
    const SceneFrame& kMoved = rig.frame(0.1F);
    RAWFRAME_EXPECT(kMoved.ribbons.size() == 1 && kMoved.ribbonPoints.size() == 2);
    if (kMoved.ribbonPoints.size() == 2) {
        const SceneRibbonPoint& kHead = kMoved.ribbonPoints[0];
        const SceneRibbonPoint& kTail = kMoved.ribbonPoints[1];
        RAWFRAME_EXPECT(near(kHead.place[0], 1) && near(kHead.place[2], -5) && near(kHead.width, 0.2F));
        RAWFRAME_EXPECT(near(kHead.along, 0) && near(kHead.color[3], 1));
        RAWFRAME_EXPECT(near(kTail.place[0], 0) && near(kTail.width, 0.18F) && near(kTail.along, 0.1F));
        RAWFRAME_EXPECT(near(kTail.color[3], 0.9F));
    }
    // A quarter meter more leaves no point; the head follows.
    rig.move(kTrail, 1.25);
    const SceneFrame& kFollowed = rig.frame(0.1F);
    RAWFRAME_EXPECT(kFollowed.ribbonPoints.size() == 3 && near(kFollowed.ribbonPoints[0].place[0], 1.25F));
    // A second after it was left, the first point is gone; a second after
    // the other, nothing is left to draw.
    for (int frame = 0; frame < 7; ++frame) {
        static_cast<void>(rig.frame(0.1F));
    }
    const SceneFrame& kOlder = rig.frame(0.1F);
    RAWFRAME_EXPECT(kOlder.ribbons.size() == 1 && kOlder.ribbonPoints.size() == 2);
    RAWFRAME_EXPECT(rig.frame(0.1F).ribbons.empty());
}

RAWFRAME_TEST(ABeamFollowsItsCurve) {
    Rig rig;
    rig.place(kBeamId, arc(), -2, -6);
    const SceneFrame& kFrame = rig.frame(0);
    RAWFRAME_EXPECT(kFrame.ribbons.size() == 1 && kFrame.ribbonPoints.size() == 5);
    if (kFrame.ribbonPoints.size() == 5) {
        const SceneRibbonPoint& kMiddle = kFrame.ribbonPoints[2];
        RAWFRAME_EXPECT(near(kMiddle.place[0], 0) && near(kMiddle.place[1], 1) && near(kMiddle.place[2], -6));
        RAWFRAME_EXPECT(near(kMiddle.width, 0.15F) && near(kFrame.ribbonPoints[4].place[0], 2));
        // Along its length, a repeat every two meters: longer than straight.
        RAWFRAME_EXPECT(near(kFrame.ribbonPoints[0].along, 0) && kFrame.ribbonPoints[4].along > 2);
    }
}

RAWFRAME_TEST(RibbonsAreKeptByTheViewAndTheLimits) {
    Rig rig({.maximumRibbons = 2, .maximumBeamSegments = 8});
    // Before the eye, nearer and farther; behind it; one not sound; one cut
    // past the limit; one with nothing to show.
    rig.place(kBeamId, arc(), -2, -5);
    rig.place(kBeamId, arc(), -2, -20);
    rig.place(kBeamId, arc(), -2, 30);
    Beam broken = arc();
    broken.toX = std::numeric_limits<float>::quiet_NaN();
    rig.place(kBeamId, broken, -2, -6);
    Beam fine = arc();
    fine.segments = 100;
    rig.place(kBeamId, fine, -2, -10);
    rig.place(kBeamId, Beam{}, -2, -7);
    const SceneFrame& kFrame = rig.frame(0);
    // The nearest two kept, the farthest first; the third in view past the
    // limit and the unsound left out; the fine one held to eight.
    RAWFRAME_EXPECT(kFrame.ribbons.size() == 2 && kFrame.ribbonsLeftOut == 2 && kFrame.ribbonsHeld == 1);
    if (kFrame.ribbons.size() == 2) {
        RAWFRAME_EXPECT(kFrame.ribbons[0].count == 9 && kFrame.ribbons[1].count == 5);
        RAWFRAME_EXPECT(kFrame.ribbons[1].first == 9);
        RAWFRAME_EXPECT(near(kFrame.ribbonPoints[9].place[2], -5));
    }
}
