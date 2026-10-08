// A camera's effects made sound before a frame uses them: its metering
// (D293, D345), grade and tonemapper (D294, D295), screen-space
// reflections, motion blur, depth of field, contact shadows, bloom, and
// ambient occlusion, and its post processes in its order (D349).

#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"
#include "scene_rig.h"

#include <cmath>
#include <limits>

using namespace rawframe;
using namespace rawframe::render_scene;
using namespace rawframe::render_scene::test_rig;

RAWFRAME_TEST(AMeteredCameraIsMadeSound) {
    Rig rig;
    SceneCamera camera{.fovY = 1, .near = 0.1F, .aspect = 1};
    RAWFRAME_EXPECT(!rig.frame(camera).metering.enabled);
    // A negative rate, fractions and a middle's weight past one (D345), and
    // a long pause: stopped, clamped, and cut to a quarter second; the first metered frame goes
    // at once to what it measures, the next at the rates, and one after
    // the eye cuts away at once again.
    camera.metering = AutoExposure{.minimum = 8,
                                   .maximum = 16,
                                   .brighten = -1,
                                   .darken = 2,
                                   .compensation = 1,
                                   .low = 0.9F,
                                   .high = 1.5F,
                                   .centered = 2};
    camera.elapsed = 3;
    const SceneMetering kMetering = rig.frame(camera).metering;
    RAWFRAME_EXPECT(kMetering.enabled && kMetering.settings.minimum == 8 && kMetering.settings.maximum == 16 &&
                    kMetering.settings.brighten == 0 && kMetering.settings.darken == 2 &&
                    kMetering.settings.low == 0.9F && kMetering.settings.high == 1 && kMetering.elapsed == 0.25F &&
                    kMetering.settings.centered == 1 && kMetering.snap);
    RAWFRAME_EXPECT(!rig.frame(camera).metering.snap);
    camera.eye = {0, 0, 40};
    RAWFRAME_EXPECT(rig.frame(camera).metering.snap);
    RAWFRAME_EXPECT(!rig.frame(camera).metering.snap);
    // A meter not yet set, all nought, or with its bounds out of order,
    // meters nothing.
    SceneCamera unset = camera;
    unset.metering = AutoExposure{};
    RAWFRAME_EXPECT(!rig.frame(unset).metering.enabled);
    unset.metering = AutoExposure{.minimum = 16, .maximum = 8, .high = 1};
    RAWFRAME_EXPECT(!rig.frame(unset).metering.enabled);
    // A value not finite meters nothing.
    camera.metering->compensation = std::numeric_limits<float>::quiet_NaN();
    RAWFRAME_EXPECT(!rig.frame(camera).metering.enabled);
}

RAWFRAME_TEST(ACamerasGradeAndTonemapperAreMadeSound) {
    // Neutral: the balance is the identity, and the rest passes light on.
    const SceneGrading kNeutral = gradingOf(Grading{});
    RAWFRAME_EXPECT(kNeutral.enabled && kNeutral.table == 0);
    // A grading table passes through to be looked up (D344).
    RAWFRAME_EXPECT(gradingOf(Grading{.table = 0x7a}).table == 0x7a);
    for (std::size_t at = 0; at < 9; ++at) {
        RAWFRAME_EXPECT(near(kNeutral.balance[at], at % 4 == 0 ? 1.0F : 0.0F, 2e-3F));
    }
    // Warmer: white comes out redder than blue; cooler, bluer; a tint
    // above nought lowers green.
    const auto kWhiteOf = [](const SceneGrading& grade) {
        std::array<float, 3> out{};
        for (std::size_t row = 0; row < 3; ++row) {
            out[row] = grade.balance[row * 3] + grade.balance[(row * 3) + 1] + grade.balance[(row * 3) + 2];
        }
        return out;
    };
    const std::array<float, 3> kWarm = kWhiteOf(gradingOf(Grading{.temperature = 1}));
    const std::array<float, 3> kCool = kWhiteOf(gradingOf(Grading{.temperature = -1}));
    const std::array<float, 3> kMagenta = kWhiteOf(gradingOf(Grading{.tint = 1}));
    std::printf("warm %f %f %f cool %f %f %f magenta %f %f %f\n",
                kWarm[0],
                kWarm[1],
                kWarm[2],
                kCool[0],
                kCool[1],
                kCool[2],
                kMagenta[0],
                kMagenta[1],
                kMagenta[2]);
    RAWFRAME_EXPECT(kWarm[0] > kWarm[2] && kCool[2] > kCool[0] && kMagenta[1] < kMagenta[0] &&
                    kMagenta[1] < kMagenta[2]);
    // Not yet set (all nought), or not finite: no grade; a negative
    // saturation is none.
    RAWFRAME_EXPECT(!gradingOf(Grading{.powerR = 0, .powerG = 0, .powerB = 0}).enabled &&
                    !gradingOf(std::nullopt).enabled);
    RAWFRAME_EXPECT(!gradingOf(Grading{.slopeG = std::numeric_limits<float>::infinity()}).enabled);
    RAWFRAME_EXPECT(gradingOf(Grading{.saturation = -2}).saturation == 0);
    // A camera's grade reaches its frame.
    Rig rig;
    SceneCamera camera{.fovY = 1, .near = 0.1F, .aspect = 1};
    RAWFRAME_EXPECT(!rig.frame(camera).grading.enabled);
    camera.grading = Grading{.slopeR = 2};
    const SceneGrading kGraded = rig.frame(camera).grading;
    RAWFRAME_EXPECT(kGraded.enabled && kGraded.slope[0] == 2);
    // The camera's tonemapper, AgX for a number outside the set (D295).
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Agx);
    camera.tonemapper = 1;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::PbrNeutral);
    camera.tonemapper = 2;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Linear);
    camera.tonemapper = 7;
    RAWFRAME_EXPECT(rig.frame(camera).tonemapper == Tonemapper::Agx);
}

RAWFRAME_TEST(ACamerasScreenSpaceReflectionsAreMadeSound) {
    RAWFRAME_EXPECT(!reflectionsOf(std::nullopt).enabled &&
                    !reflectionsOf(ScreenSpaceReflections{.distance = 0}).enabled &&
                    !reflectionsOf(ScreenSpaceReflections{.distance = std::nanf("")}).enabled);
    const SceneScreenReflections kAsked = reflectionsOf(ScreenSpaceReflections{.distance = 20});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.distance == 20 &&
                    reflectionsOf(ScreenSpaceReflections{.distance = 1000}).distance == 100 &&
                    reflectionsOf(ScreenSpaceReflections{.distance = 0.01F}).distance == 0.1F);
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.reflections = ScreenSpaceReflections{.distance = 5}}).reflections.enabled &&
                    !rig.frame({}).reflections.enabled);
}

RAWFRAME_TEST(ACamerasMotionBlurIsMadeSound) {
    RAWFRAME_EXPECT(!motionBlurOf(std::nullopt).enabled && !motionBlurOf(MotionBlur{.shutter = 0}).enabled &&
                    !motionBlurOf(MotionBlur{.shutter = -1}).enabled &&
                    !motionBlurOf(MotionBlur{.shutter = std::nanf("")}).enabled);
    const SceneMotionBlur kAsked = motionBlurOf(MotionBlur{.shutter = 0.5F});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.shutter == 0.5F && motionBlurOf(MotionBlur{.shutter = 4}).shutter == 1);
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.motionBlur = MotionBlur{.shutter = 0.5F}}).motionBlur.enabled &&
                    !rig.frame({}).motionBlur.enabled);
}

RAWFRAME_TEST(ACamerasDepthOfFieldIsMadeSound) {
    RAWFRAME_EXPECT(!depthOfFieldOf(std::nullopt).enabled &&
                    !depthOfFieldOf(DepthOfField{.focus = 0, .aperture = 2}).enabled &&
                    !depthOfFieldOf(DepthOfField{.focus = 5, .aperture = 0}).enabled &&
                    !depthOfFieldOf(DepthOfField{.focus = std::nanf(""), .aperture = 2}).enabled &&
                    !depthOfFieldOf(DepthOfField{.focus = 5, .aperture = std::nanf("")}).enabled);
    const SceneDepthOfField kAsked = depthOfFieldOf(DepthOfField{.focus = 5, .aperture = 2.8F});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.focus == 5 && kAsked.aperture == 2.8F);
    const SceneDepthOfField kFar = depthOfFieldOf(DepthOfField{.focus = 1e6F, .aperture = 0.1F});
    RAWFRAME_EXPECT(kFar.focus == 10'000 && kFar.aperture == 0.5F);
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.depthOfField = DepthOfField{.focus = 5, .aperture = 2}}).depthOfField.enabled &&
                    !rig.frame({}).depthOfField.enabled);
}

RAWFRAME_TEST(ACamerasContactShadowsAreMadeSound) {
    RAWFRAME_EXPECT(!contactShadowsOf(std::nullopt).enabled && !contactShadowsOf(ContactShadows{.length = 0}).enabled &&
                    !contactShadowsOf(ContactShadows{.length = std::nanf("")}).enabled);
    const SceneContactShadows kAsked = contactShadowsOf(ContactShadows{.length = 0.5F});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.length == 0.5F &&
                    contactShadowsOf(ContactShadows{.length = 100}).length == 10 &&
                    contactShadowsOf(ContactShadows{.length = 0.001F}).length == 0.01F);
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.contactShadows = ContactShadows{.length = 0.5F}}).contactShadows.enabled &&
                    !rig.frame({}).contactShadows.enabled);
}

RAWFRAME_TEST(ACamerasBloomIsMadeSound) {
    RAWFRAME_EXPECT(!bloomOf(std::nullopt).enabled && !bloomOf(Bloom{.intensity = 0}).enabled &&
                    !bloomOf(Bloom{.intensity = std::nanf("")}).enabled);
    const SceneBloom kAsked = bloomOf(Bloom{.intensity = 0.15F});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.intensity == 0.15F && bloomOf(Bloom{.intensity = 3}).intensity == 1);
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.bloom = Bloom{.intensity = 0.2F}}).bloom.enabled && !rig.frame({}).bloom.enabled);
}

RAWFRAME_TEST(ACamerasAmbientOcclusionIsMadeSound) {
    RAWFRAME_EXPECT(!occlusionOf(std::nullopt).enabled);
    const SceneOcclusion kAsked = occlusionOf(AmbientOcclusion{.radius = 0.5F, .intensity = 1.5F});
    RAWFRAME_EXPECT(kAsked.enabled && kAsked.radius == 0.5F && kAsked.intensity == 1.5F);
    // Out of range, kept in it; not finite, or not above nought, none.
    const SceneOcclusion kFar = occlusionOf(AmbientOcclusion{.radius = 50, .intensity = 9});
    RAWFRAME_EXPECT(kFar.enabled && kFar.radius == 10 && kFar.intensity == 4);
    RAWFRAME_EXPECT(!occlusionOf(AmbientOcclusion{.radius = 0, .intensity = 1}).enabled &&
                    !occlusionOf(AmbientOcclusion{.radius = 1, .intensity = -1}).enabled &&
                    !occlusionOf(AmbientOcclusion{.radius = std::nanf(""), .intensity = 1}).enabled);
    // A camera that asks has it in its frame.
    Rig rig;
    RAWFRAME_EXPECT(rig.frame({.occlusion = AmbientOcclusion{.radius = 1, .intensity = 1}}).occlusion.enabled &&
                    !rig.frame({}).occlusion.enabled);
}

RAWFRAME_TEST(ACamerasPostProcessesRunInItsOrder) {
    // The game's two post processes, a warm one and a fade (D349).
    material::PostProcess warm{.insertion = material::Insertion::BeforeTonemap, .scene = {1, 0.9F, 0.8F}};
    warm.constant[3] = 1;
    const material::PostProcess kFade{.constant = {0, 0, 0, 1}};
    const auto kSchema = registry();
    auto scene =
        *Scene::create(*kSchema,
                       {.models = {kModelId},
                        .postProcesses = {{.id = 0xa1, .insertion = warm.insertion, .blob = material::blobOf(warm)},
                                          {.id = 0xa2, .insertion = kFade.insertion, .blob = material::blobOf(kFade)}},
                        .limits = {.maximumPostProcesses = 3}});
    SceneCamera camera;
    camera.postProcesses = {{.material = 0, .weight = 1},
                            {.material = 0xa2, .weight = 0.25F},
                            {.material = 0xbad, .weight = 1},
                            {.material = 0xa1, .weight = 0},
                            {.material = 0xa1, .weight = std::numeric_limits<float>::quiet_NaN()},
                            {.material = 0xa1, .weight = 3},
                            {.material = 0xa2, .weight = 1},
                            {.material = 0xa2, .weight = 1}};
    world::World world{kSchema};
    scene->extract(world);
    const SceneFrame& kFrame = scene->queue(camera);
    // The fade at a quarter, the warmth at most whole, the fade again; the
    // unknown, the NaN, and the one past the limit left out; material
    // nought and the one at nought run nothing.
    RAWFRAME_EXPECT(kFrame.postProcesses.size() == 3 && kFrame.postProcessesLeftOut == 3);
    if (kFrame.postProcesses.size() == 3) {
        RAWFRAME_EXPECT(kFrame.postProcesses[0].weight == 0.25F &&
                        kFrame.postProcesses[0].insertion == material::Insertion::AfterTonemap &&
                        kFrame.postProcesses[0].blob[3] == 1);
        RAWFRAME_EXPECT(kFrame.postProcesses[1].weight == 1 &&
                        kFrame.postProcesses[1].insertion == material::Insertion::BeforeTonemap &&
                        kFrame.postProcesses[1].blob[5] == 0.9F);
        RAWFRAME_EXPECT(kFrame.postProcesses[2].blob == material::blobOf(kFade));
    }
}

RAWFRAME_TEST(AFixedCameraStaysPutWhereverItsPlayerIs) {
    // A following camera is placed from its entity's pose; a fixed one
    // (D520) is its offsets in the World, whatever the pose, or none.
    const physics3d::Pose3D kPose{.x = 7, .y = 1, .z = -3};
    Camera camera{.offsetX = 0, .offsetY = 42, .offsetZ = 36};
    RAWFRAME_EXPECT((eyeOf(camera, &kPose) == std::array<double, 3>{7, 43, 33}));
    RAWFRAME_EXPECT(!eyeOf(camera, nullptr).has_value());
    camera.anchor = kCameraFixed;
    RAWFRAME_EXPECT((eyeOf(camera, &kPose) == std::array<double, 3>{0, 42, 36}));
    RAWFRAME_EXPECT((eyeOf(camera, nullptr) == std::array<double, 3>{0, 42, 36}));
}
