// The grading table on lavapipe (D344): an unlit red face's color, looked
// up in a table that swaps red and green, shows green; the plain grade
// without a table keeps it red; and a table named but not a volume is
// passed over.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/texture.h"

#include <cstdio>
#include <memory>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// A table of side two whose entry for each corner swaps red and green.
std::shared_ptr<const texture::Texture> swapping() {
    texture::Texture made{.format = texture::Format::Rgba16Float, .depth = 2};
    texture::Level level{.width = 2, .height = 2};
    for (std::uint32_t blue = 0; blue < 2; ++blue) {
        for (std::uint32_t green = 0; green < 2; ++green) {
            for (std::uint32_t red = 0; red < 2; ++red) {
                for (const std::uint32_t kChannel : {green, red, blue, 1U}) {
                    const std::uint16_t kHalf = texture::halfOf(static_cast<float>(kChannel));
                    level.bytes.push_back(static_cast<std::byte>(kHalf & 0xFFU));
                    level.bytes.push_back(static_cast<std::byte>(kHalf >> 8U));
                }
            }
        }
    }
    made.levels.push_back(std::move(level));
    return std::make_shared<const texture::Texture>(std::move(made));
}

} // namespace

RAWFRAME_TEST(AGradingTableLooksUpTheGradedColor) {
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
    const auto kSwap = swapping();
    texture::Texture flat{.format = texture::Format::Rgba8};
    flat.levels.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{0xFF})});
    const auto kFlat = std::make_shared<const texture::Texture>(std::move(flat));
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        if (id == 0x7a) {
            return kSwap;
        }
        return id == 0x7b ? kFlat : nullptr;
    };
    // An unlit red face filling the view, graded plainly.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.dither = false;
    render_scene::SceneDraw shown = box(4, 3, {1, 0, 0, 1});
    shown.material = 1;
    frame.draws = {shown};
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1;
    frame.materials = {render_scene::noMaterial(), unlit};
    frame.grading = render_scene::gradingOf(render_scene::Grading{});
    const auto kCenter = [&](const SceneFrame& asked) {
        const auto kPixels = drawn(**framer, **made, asked, kMeshes, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, kSide / 2, kSide / 2) : std::array<int, 3>{};
    };
    const auto kPlain = kCenter(frame);
    frame.grading.table = 0x7a;
    const auto kSwapped = kCenter(frame);
    frame.grading.table = 0x7b;
    const auto kNotAVolume = kCenter(frame);
    std::printf("plain %d %d %d, swapped %d %d %d, not a volume %d %d %d\n",
                kPlain[0],
                kPlain[1],
                kPlain[2],
                kSwapped[0],
                kSwapped[1],
                kSwapped[2],
                kNotAVolume[0],
                kNotAVolume[1],
                kNotAVolume[2]);
    RAWFRAME_EXPECT(kPlain[0] > kPlain[1] + 100 && kSwapped[1] > kSwapped[0] + 100 &&
                    kNotAVolume[0] > kNotAVolume[1] + 100);
}
