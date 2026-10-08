// Skinned models (D508): a skinned mesh whose entity was played is drawn
// with a palette, each joint its bone as posed after its inverse bind, the
// bone found by its target whatever the skeleton's order; one not played,
// or whose skeleton lacks a joint's bone, is drawn as its mesh was bound;
// a posed model carries the frame before's palette for its motion (D510);
// and the frame's palette has a limit.

#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

namespace {

constexpr std::uint64_t kLeg = 0x1e6;
constexpr base::Bits128 kHip{1, 1};
constexpr base::Bits128 kKnee{2, 2};

/// The rig's rock skinned to a hip and a knee, the knee's bind a meter up;
/// every vertex follows the hip.
std::shared_ptr<const mesh::Mesh> leg() {
    mesh::Mesh made = *rock();
    constexpr std::array<float, 16> kIdentity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 16> lowered = kIdentity;
    lowered[13] = -1;
    made.skin.joints = {{.bone = kHip, .inverseBind = kIdentity}, {.bone = kKnee, .inverseBind = lowered}};
    made.skin.influences.assign(made.positions.size(), {0, 1, 0, 0});
    made.skin.weights.assign(made.positions.size(), {1, 0, 0, 0});
    return std::make_shared<const mesh::Mesh>(std::move(made));
}

/// Poses as an animation would leave them, for the entities given one.
class Poses final : public world_animation::AnimationQueries {
public:
    std::map<world::EntityHandle, animation::Pose> poses;
    std::vector<base::Bits128> skeleton;

    const animation::Pose* pose(world::EntityHandle entity) const noexcept override {
        const auto kFound = poses.find(entity);
        return kFound == poses.end() ? nullptr : &kFound->second;
    }
    std::span<const base::Bits128> bones(world::EntityHandle entity) const noexcept override {
        return poses.contains(entity) ? std::span<const base::Bits128>{skeleton} : std::span<const base::Bits128>{};
    }
    std::span<const animation::GraphEvent> events(world::EntityHandle /*entity*/) const noexcept override {
        return {};
    }
    std::optional<world_animation::MachineView> machine(world::EntityHandle /*entity*/,
                                                        std::uint64_t /*node*/) const noexcept override {
        return std::nullopt;
    }
};

/// The knee first, a meter up and turned a quarter about z; the hip five
/// meters along x.
void pose(Poses& poses, world::EntityHandle entity) {
    poses.skeleton = {kKnee, kHip};
    const double kHalf = std::sqrt(0.5);
    poses.poses[entity] =
        animation::Pose{.bones = {animation::Transform{.translation = {0, 1, 0}, .rotation = {0, 0, kHalf, kHalf}},
                                  animation::Transform{.translation = {5, 0, 0}}}};
}

std::unique_ptr<Scene> sceneOf(const Rig& rig, SceneLimits limits = {}) {
    return *Scene::create(*rig.schema,
                          {.models = {kModelId}, .meshes = {{.id = kLeg, .mesh = leg()}}, .limits = limits});
}

} // namespace

RAWFRAME_TEST(AJointIsItsBonePosedAfterItsInverseBind) {
    Rig rig;
    const world::EntityHandle kLegged = rig.spawn(Model{.mesh = kLeg}, physics3d::Pose3D{.z = -10, .qw = 1});
    auto scene = sceneOf(rig);
    Poses poses;
    pose(poses, kLegged);
    scene->extract(rig.world, &poses);
    RAWFRAME_EXPECT(scene->extracted().size() == 1 && scene->extracted()[0].joints == 2);
    const std::span<const Matrix> kPalette = scene->extractedPalette();
    RAWFRAME_EXPECT(kPalette.size() == 2);
    if (kPalette.size() != 2) {
        return;
    }
    // The hip's: five meters along x, unturned.
    RAWFRAME_EXPECT(near(kPalette[0][0], 1) && near(kPalette[0][12], 5) && near(kPalette[0][13], 0));
    // The knee's: lowered to its bone, turned a quarter, raised back; a
    // point at the knee's bind stays where the posed knee is.
    RAWFRAME_EXPECT(near(kPalette[1][0], 0) && near(kPalette[1][1], 1) && near(kPalette[1][4], -1));
    RAWFRAME_EXPECT(near(kPalette[1][12], 1) && near(kPalette[1][13], 1) && near(kPalette[1][14], 0));
    // Its draws name the frame's palette, which holds it.
    const SceneFrame& kFrame = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.drawn == 1 && kFrame.skinned == 1 && kFrame.palette.size() == 2);
    RAWFRAME_EXPECT(!kFrame.draws.empty() && kFrame.draws[0].palette == 0 && kFrame.draws[0].joints == 2);
    RAWFRAME_EXPECT(kFrame.palette[1] == kPalette[1]);
}

RAWFRAME_TEST(AModelNotPosedIsDrawnAsItsMeshWasBound) {
    Rig rig;
    const world::EntityHandle kLegged = rig.spawn(Model{.mesh = kLeg}, physics3d::Pose3D{.z = -10, .qw = 1});
    const world::EntityHandle kOther = rig.spawn(Model{.mesh = kLeg}, physics3d::Pose3D{.z = -12, .qw = 1});
    auto scene = sceneOf(rig);
    // No animation; then one that played only the first.
    scene->extract(rig.world);
    RAWFRAME_EXPECT(scene->extractedPalette().empty());
    Poses poses;
    pose(poses, kLegged);
    scene->extract(rig.world, &poses);
    std::size_t posedModels = 0;
    for (const ModelInstance& kInstance : scene->extracted()) {
        posedModels += kInstance.joints != 0 ? 1 : 0;
        RAWFRAME_EXPECT(kInstance.entity != kOther || kInstance.joints == 0);
    }
    RAWFRAME_EXPECT(posedModels == 1);
    // A skeleton without the knee: drawn as bound.
    Poses kneeless;
    pose(kneeless, kLegged);
    kneeless.skeleton = {base::Bits128{9, 9}, kHip};
    scene->extract(rig.world, &kneeless);
    RAWFRAME_EXPECT(scene->extractedPalette().empty());
    const SceneFrame& kFrame = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.drawn == 2 && kFrame.skinned == 0 && kFrame.palette.empty());
    // The same skeleton again finds the knee.
    scene->extract(rig.world, &poses);
    RAWFRAME_EXPECT(scene->extractedPalette().size() == 2);
}

RAWFRAME_TEST(AFramesPaletteHasALimit) {
    Rig rig;
    const world::EntityHandle kLegged = rig.spawn(Model{.mesh = kLeg}, physics3d::Pose3D{.z = -10, .qw = 1});
    auto scene = sceneOf(rig, {.maximumPalette = 1});
    Poses poses;
    pose(poses, kLegged);
    scene->extract(rig.world, &poses);
    const SceneFrame& kFrame = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.drawn == 1 && kFrame.skinned == 0 && kFrame.paletteOverLimit == 1 &&
                    kFrame.palette.empty() && kFrame.draws[0].joints == 0);
}

RAWFRAME_TEST(APosedModelCarriesThePaletteItWasPosedWithBefore) {
    Rig rig;
    const world::EntityHandle kLegged = rig.spawn(Model{.mesh = kLeg}, physics3d::Pose3D{.z = -10, .qw = 1});
    auto scene = sceneOf(rig);
    Poses poses;
    pose(poses, kLegged);
    // The first frame: posed for the first time, its motion measured from
    // its own palette.
    scene->extract(rig.world, &poses);
    const SceneFrame& kFirst = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kFirst.palette.size() == 2 && !kFirst.draws.empty() && kFirst.draws[0].previousPalette == 0);
    const Matrix kHipBefore = kFirst.palette[0];
    // The hip steps a meter on: this frame's palette, then the last's.
    poses.poses[kLegged].bones[1].translation = {6, 0, 0};
    scene->extract(rig.world, &poses);
    const SceneFrame& kSecond = scene->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kSecond.palette.size() == 4 && !kSecond.draws.empty());
    if (kSecond.palette.size() != 4 || kSecond.draws.empty()) {
        return;
    }
    RAWFRAME_EXPECT(kSecond.draws[0].palette == 0 && kSecond.draws[0].previousPalette == 2);
    RAWFRAME_EXPECT(near(kSecond.palette[0][12], 6) && kSecond.palette[2] == kHipBefore);
    // Without the room for both, it is drawn as bound.
    auto tight = sceneOf(rig, {.maximumPalette = 3});
    tight->extract(rig.world, &poses);
    static_cast<void>(tight->queue({.fovY = 1, .near = 0.1F, .aspect = 1}));
    tight->extract(rig.world, &poses);
    const SceneFrame& kTight = tight->queue({.fovY = 1, .near = 0.1F, .aspect = 1});
    RAWFRAME_EXPECT(kTight.paletteOverLimit == 1 && kTight.palette.empty());
}
