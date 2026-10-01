// A split-screen layout's regions placed in a window's pixels (D362), and
// a region constrained to an aspect, centered between its bars (D369).

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

RAWFRAME_TEST(AConstrainedViewIsCenteredBetweenItsBars) {
    const world_kest::GameAspect kWide{.width = 16, .height = 9};
    // A square window: bars above and below.
    RAWFRAME_EXPECT(
        placed(world_kest::constrainedTo({.x = 0, .y = 0, .width = 800, .height = 800}, kWide), 0, 175, 800, 450));
    // A wide window: bars at the sides; an odd bar total gives the left the
    // smaller.
    RAWFRAME_EXPECT(
        placed(world_kest::constrainedTo({.x = 0, .y = 0, .width = 2561, .height = 1080}, kWide), 320, 0, 1920, 1080));
    // A region of the shape is itself; a region placed off the top left
    // keeps its place.
    RAWFRAME_EXPECT(
        placed(world_kest::constrainedTo({.x = 0, .y = 0, .width = 1280, .height = 720}, kWide), 0, 0, 1280, 720));
    RAWFRAME_EXPECT(placed(world_kest::constrainedTo({.x = 640, .y = 0, .width = 640, .height = 720},
                                                     world_kest::GameAspect{.width = 4, .height = 3}),
                           640,
                           120,
                           640,
                           480));
    // A region with no pixels has none.
    RAWFRAME_EXPECT(world_kest::constrainedTo({}, kWide).width == 0);
}
