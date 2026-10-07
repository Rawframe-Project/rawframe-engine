// Particles on lavapipe (D353): a burst is born into its emitter's ring
// and drawn where it starts, stays drawn the frames after with nothing
// spawned, and is gone past its life; a particle moving with drag is
// drawn where the closed form puts it; one behind a model is hidden; an
// emitting material with no opacity adds its light over what is behind;
// an emitter whose ring the pool cannot hold is left out and counted; and
// a ribbon is drawn across its points, its width about them (D354).

#include "fixture.h"
#include "rawframe/material/material.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// An unlit red material, or one that only emits green light.
render_scene::MaterialBlob red() {
    material::Material made{.shading = material::Shading::Unlit};
    made.surface.baseColor = {1, 0, 0};
    return material::blobOf(made);
}

render_scene::MaterialBlob glowing() {
    material::Material made;
    made.surface.geometryOpacity = 0;
    made.surface.emissionColor = {0, 1, 0};
    // About one, times the exposure at EV 15.
    made.surface.emissionLuminance = 20000;
    return material::blobOf(made);
}

/// An emitter five meters ahead, its ring of `capacity`, spawning `spawned`
/// at once at the clock's now, a meter across, living two seconds.
particles::EmitterDraw emitterOf(std::uint32_t capacity, std::uint32_t spawned) {
    return particles::EmitterDraw{.key = 7,
                                  .anchor = {0, 0, -5},
                                  .lifetime = 2,
                                  .sizeStart = 1,
                                  .sizeEnd = 1,
                                  .capacity = capacity,
                                  .spawned = spawned,
                                  .seed = 99};
}

/// The view of the sky through a linear tonemapper, with `emitter` drawn
/// with `material` at the particle clock `clock`.
SceneFrame seen(const particles::EmitterDraw& emitter, render_scene::MaterialBlob material, float clock) {
    SceneFrame made = looking();
    made.tonemapper = render_scene::Tonemapper::Linear;
    made.materials = {material};
    made.textures = {render_scene::SceneTextures{}};
    made.particles.emitters = {emitter};
    made.particles.clock = clock;
    return made;
}

} // namespace

RAWFRAME_TEST(ParticlesAreBornDrawnAndGone) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    const auto kSky = drawn(**framer, **made, seen({}, red(), 0), kMeshes);
    RAWFRAME_EXPECT(kSky.has_value());
    if (!kSky.has_value()) {
        return;
    }
    const auto kBlue = at(*kSky, 32, 32);
    std::printf("sky: %d %d %d\n", kBlue[0], kBlue[1], kBlue[2]);

    // A burst of ten at the clock's now: red where they start, the sky
    // beside them.
    const auto kBorn = drawnWith(**framer,
                                 **made,
                                 seen(emitterOf(16, 10), red(), 100),
                                 kMeshes,
                                 &render_scene_gpu::RendererStatistics::emittersDrawn);
    RAWFRAME_EXPECT(kBorn.has_value() && (*made)->statistics().particlesSpawned >= 10);
    if (kBorn.has_value()) {
        const auto kMiddle = at(*kBorn, 32, 32);
        std::printf("born: %d %d %d, beside %d\n", kMiddle[0], kMiddle[1], kMiddle[2], at(*kBorn, 50, 32)[0]);
        RAWFRAME_EXPECT(kMiddle[0] == 255 && kMiddle[1] == 0 && kMiddle[2] == 0);
        RAWFRAME_EXPECT(at(*kBorn, 50, 32) == at(*kSky, 50, 32));
    }
    // A second on, nothing spawned: still drawn from the ring.
    const auto kKept = drawn(**framer, **made, seen(emitterOf(16, 0), red(), 101), kMeshes);
    RAWFRAME_EXPECT(kKept.has_value());
    if (kKept.has_value()) {
        RAWFRAME_EXPECT(at(*kKept, 32, 32)[0] == 255);
    }
    // Past their two seconds' life: gone.
    const auto kGone = drawn(**framer, **made, seen(emitterOf(16, 0), red(), 102.5F), kMeshes);
    RAWFRAME_EXPECT(kGone.has_value());
    if (kGone.has_value()) {
        RAWFRAME_EXPECT(at(*kGone, 32, 32) == kBlue);
    }

    // Born a second ago heading right at two meters a second, slowed by a
    // drag of one: 2 (1 - 1/e) = 1.26 meters right, eight pixels at five
    // meters ahead (a quarter turn high over 64 pixels), small enough to
    // leave the middle.
    particles::EmitterDraw moving = emitterOf(16, 1);
    moving.key = 8;
    moving.ring = 1;
    moving.direction = {1, 0, 0};
    moving.speed = 2;
    moving.drag = 1;
    moving.sizeStart = 0.6F;
    moving.sizeEnd = 0.6F;
    moving.born = 199;
    moving.steady = 1;
    moving.step = 0;
    const auto kMoving = drawnWith(
        **framer, **made, seen(moving, red(), 200), kMeshes, &render_scene_gpu::RendererStatistics::particlesSpawned);
    RAWFRAME_EXPECT(kMoving.has_value());
    if (kMoving.has_value()) {
        const float kPixels = 2 * (1 - std::exp(-1.0F)) / 10 * 64;
        const auto kThere = at(*kMoving, 32 + static_cast<std::uint32_t>(std::lround(kPixels)), 31);
        std::printf("moved %.2f pixels: %d %d %d, middle %d\n",
                    kPixels,
                    kThere[0],
                    kThere[1],
                    kThere[2],
                    at(*kMoving, 32, 31)[0]);
        RAWFRAME_EXPECT(kThere[0] == 255 && kThere[1] == 0);
        RAWFRAME_EXPECT(at(*kMoving, 32, 31) == at(*kSky, 32, 31));
    }

    // Behind a grey box between it and the eye: hidden.
    SceneFrame hidden = seen(emitterOf(16, 10), red(), 300);
    hidden.particles.emitters[0].key = 9;
    hidden.draws = {box(3, 0.5F, {0.5F, 0.5F, 0.5F, 1})};
    const auto kHidden = drawn(**framer, **made, hidden, kMeshes);
    hidden.particles.emitters.clear();
    const auto kBox = drawn(**framer, **made, hidden, kMeshes);
    RAWFRAME_EXPECT(kHidden.has_value() && kBox.has_value());
    if (kHidden.has_value() && kBox.has_value()) {
        RAWFRAME_EXPECT(at(*kHidden, 32, 32) == at(*kBox, 32, 32));
    }

    // Emitting with no opacity: its green added over the sky, the sky's
    // red and blue kept.
    particles::EmitterDraw glow = emitterOf(16, 10);
    glow.key = 10;
    const auto kGlow = drawn(**framer, **made, seen(glow, glowing(), 400), kMeshes);
    RAWFRAME_EXPECT(kGlow.has_value());
    if (kGlow.has_value()) {
        const auto kAdded = at(*kGlow, 32, 32);
        std::printf("glow: %d %d %d\n", kAdded[0], kAdded[1], kAdded[2]);
        RAWFRAME_EXPECT(kAdded[1] > kBlue[1] + 40 && std::abs(kAdded[0] - kBlue[0]) <= 2 &&
                        std::abs(kAdded[2] - kBlue[2]) <= 2);
    }
}

RAWFRAME_TEST(AnEmitterThePoolCannotHoldIsLeftOut) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_scene_gpu::SceneRenderer::create(*kDevice, {.maximumParticles = 100});
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    // Sixty, then a second emitter's sixty more: the second left out.
    particles::EmitterDraw second = emitterOf(60, 10);
    second.key = 2;
    SceneFrame both = seen(emitterOf(60, 10), red(), 10);
    both.particles.emitters.push_back(second);
    const auto kDrawn =
        drawnWith(**framer, **made, both, kMeshes, &render_scene_gpu::RendererStatistics::emittersDrawn);
    RAWFRAME_EXPECT(kDrawn.has_value());
    const render_scene_gpu::RendererStatistics& kCounted = (*made)->statistics();
    RAWFRAME_EXPECT(kCounted.emittersLeftOut >= 1 && kCounted.emittersDrawn >= 1);
    RAWFRAME_EXPECT(kCounted.emittersDrawn == kCounted.emittersLeftOut);
}

RAWFRAME_TEST(ARibbonIsDrawnAcrossItsPoints) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    const auto kSky = drawn(**framer, **made, seen({}, red(), 0), kMeshes);
    // Across the view five meters ahead, bending down at its right, red
    // and a meter wide: three pixels either side of its line, joined where
    // it bends.
    SceneFrame crossing = seen({}, red(), 0);
    crossing.particles.emitters.clear();
    crossing.particles.ribbonPoints = {
        {.place = {-3, 0, -5}, .width = 1}, {.place = {0, 0, -5}, .width = 1}, {.place = {3, -1, -5}, .width = 1}};
    crossing.particles.ribbons = {{.material = 0, .first = 0, .count = 3}};
    const auto kCrossed =
        drawnWith(**framer, **made, crossing, kMeshes, &render_scene_gpu::RendererStatistics::ribbonsDrawn);
    RAWFRAME_EXPECT(kSky.has_value() && kCrossed.has_value());
    if (kSky.has_value() && kCrossed.has_value()) {
        const auto kMiddle = at(*kCrossed, 32, 31);
        std::printf("ribbon: middle %d %d %d, left %d, above %d, right below %d\n",
                    kMiddle[0],
                    kMiddle[1],
                    kMiddle[2],
                    at(*kCrossed, 16, 31)[0],
                    at(*kCrossed, 32, 20)[0],
                    at(*kCrossed, 40, 34)[0]);
        // Within a step of red: Metal on Apple's GPU rounds green to 1 (D406).
        RAWFRAME_EXPECT(kMiddle[0] == 255 && kMiddle[1] <= 1 && kMiddle[2] <= 1);
        RAWFRAME_EXPECT(at(*kCrossed, 16, 31)[0] == 255 && at(*kCrossed, 40, 34)[0] == 255);
        RAWFRAME_EXPECT(at(*kCrossed, 32, 20) == at(*kSky, 32, 20));
    }
}

RAWFRAME_TEST(ALineIsDrawnInItsOwnColor) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    // A line (D464) across the view five meters ahead, green, on the
    // material after the frame's one, which the renderer makes: its color
    // stands as it is.
    SceneFrame lined = seen({}, red(), 0);
    lined.particles.emitters.clear();
    lined.particles.ribbonPoints = {{.place = {-3, 0, -5}, .width = 0.5F, .color = {0, 1, 0, 1}},
                                    {.place = {3, 0, -5}, .width = 0.5F, .color = {0, 1, 0, 1}, .along = 1}};
    lined.particles.ribbons = {{.material = 1, .first = 0, .count = 2}};
    const auto kLined =
        drawnWith(**framer, **made, lined, kMeshes, &render_scene_gpu::RendererStatistics::ribbonsDrawn);
    RAWFRAME_EXPECT(kLined.has_value());
    if (kLined.has_value()) {
        const auto kMiddle = at(*kLined, 32, 31);
        std::printf("line: middle %d %d %d\n", kMiddle[0], kMiddle[1], kMiddle[2]);
        RAWFRAME_EXPECT(kMiddle[0] <= 1 && kMiddle[1] == 255 && kMiddle[2] <= 1);
    }
}
