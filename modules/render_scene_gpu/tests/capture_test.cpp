// A frame's light read back (D326): a glowing box reads as its emission in
// candela per square meter, whatever the exposure it was drawn at, and the
// unlit sky around it as nought; a capture is given once.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <cstdio>

using namespace rawframe;
using namespace rawframe::scene_fixture;

RAWFRAME_TEST(AFramesLightIsReadBackInPhysicalUnits) {
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
    // A black box glowing 1000, 500, and 250 nits, four meters ahead, in
    // no other light.
    render_scene::MaterialBlob glowing = render_scene::noMaterial();
    glowing[0] = 0;
    glowing[1] = 0;
    glowing[2] = 0;
    glowing[8] = 1000;
    glowing[9] = 500;
    glowing[10] = 250;
    for (const float kExposure : {9.0F, 14.0F}) {
        render_scene::SceneFrame frame = looking();
        frame.exposure = kExposure;
        frame.shadows.count = 0;
        frame.lights.sun = {0, 0, 0};
        frame.lights.sky = {0, 0, 0};
        frame.lights.ground = {0, 0, 0};
        render_scene::SceneDraw glow = box(4, 1.5F, {1, 1, 1, 1});
        glow.material = 1;
        frame.materials = {render_scene::noMaterial(), glowing};
        frame.draws = {glow};
        (*made)->capture();
        RAWFRAME_EXPECT(drawn(**framer, **made, frame, kMeshes).has_value());
        const auto kLight = (*made)->captured();
        RAWFRAME_EXPECT(kLight.has_value() && kLight->width == kSide && kLight->height == kSide &&
                        kLight->light.size() == std::size_t{kSide} * kSide * 3);
        if (!kLight.has_value() || kLight->light.size() != std::size_t{kSide} * kSide * 3) {
            return;
        }
        const std::size_t kCenter = ((std::size_t{kSide} / 2 * kSide) + (kSide / 2)) * 3;
        std::printf("EV %.0f: center %.1f %.1f %.1f, corner %.3f\n",
                    kExposure,
                    kLight->light[kCenter],
                    kLight->light[kCenter + 1],
                    kLight->light[kCenter + 2],
                    kLight->light[0]);
        RAWFRAME_EXPECT(std::abs(kLight->light[kCenter] - 1000) < 10 &&
                        std::abs(kLight->light[kCenter + 1] - 500) < 5 &&
                        std::abs(kLight->light[kCenter + 2] - 250) < 2.5F && kLight->light[0] < 0.01F);
        RAWFRAME_EXPECT(!(*made)->captured().has_value());
    }
}
