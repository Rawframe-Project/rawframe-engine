// A split-screen layout's regions placed in a window's pixels (D362).

#include "rawframe/test/test.h"
#include "rawframe/world_kest/game.h"

using namespace rawframe;

namespace {

bool placed(const world_kest::RegionPixels& region,
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t width,
            std::uint32_t height) noexcept {
    return region.x == x && region.y == y && region.width == width && region.height == height;
}

world_kest::RegionPixels in(float x, float y, float width, float height, std::uint32_t across, std::uint32_t down) {
    return world_kest::pixelsOf(world_kest::GameRegion{.x = x, .y = y, .width = width, .height = height}, across, down);
}

} // namespace

RAWFRAME_TEST(RegionsSharingAnEdgeTileTheWindow) {
    // Two side by side in an odd width: the shared edge at the nearest
    // pixel, the two covering it whole.
    const auto kLeft = in(0, 0, 0.5F, 1, 1281, 720);
    const auto kRight = in(0.5F, 0, 0.5F, 1, 1281, 720);
    RAWFRAME_EXPECT(placed(kLeft, 0, 0, 641, 720) && placed(kRight, 641, 0, 640, 720));
    // Three in a column of thirds.
    std::uint32_t covered = 0;
    for (const float kTop : {0.0F, 1.0F / 3.0F, 2.0F / 3.0F}) {
        const auto kThird = in(0, kTop, 1, 1.0F / 3.0F, 640, 100);
        RAWFRAME_EXPECT(kThird.y == covered);
        covered += kThird.height;
    }
    RAWFRAME_EXPECT(covered == 100);
    // A region too small for a pixel has none.
    RAWFRAME_EXPECT(in(0.1F, 0.1F, 0.001F, 0.5F, 100, 100).width == 0);
}
