// A split-screen layout's regions in a window's pixels (D362), and a
// region constrained to an aspect between its bars (D369): what a game's
// `layout` and `aspect` lines mean on a client's screen.

#include "rawframe/world_kest/game.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rawframe::world_kest {

RegionPixels pixelsOf(const GameRegion& region, std::uint32_t width, std::uint32_t height) noexcept {
    const auto kEdge = [](float at, std::uint32_t side) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(at, 0.0F, 1.0F) * static_cast<float>(side)));
    };
    const std::uint32_t kLeft = kEdge(region.x, width);
    const std::uint32_t kTop = kEdge(region.y, height);
    const std::uint32_t kRight = std::max(kLeft, kEdge(region.x + region.width, width));
    const std::uint32_t kBottom = std::max(kTop, kEdge(region.y + region.height, height));
    return RegionPixels{.x = kLeft, .y = kTop, .width = kRight - kLeft, .height = kBottom - kTop};
}

RegionPixels constrainedTo(const RegionPixels& region, const GameAspect& aspect) noexcept {
    if (aspect.width == 0 || aspect.height == 0) {
        return region;
    }
    const std::uint64_t kWide = std::uint64_t{region.width} * aspect.height;
    const std::uint64_t kTall = std::uint64_t{region.height} * aspect.width;
    RegionPixels inner = region;
    if (kWide > kTall) {
        // Wider than the shape: bars at the sides.
        inner.width = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(region.width, (kTall + (aspect.height / 2)) / aspect.height));
        inner.x += (region.width - inner.width) / 2;
    } else if (kTall > kWide) {
        // Taller: bars above and below.
        inner.height = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(region.height, (kWide + (aspect.width / 2)) / aspect.width));
        inner.y += (region.height - inner.height) / 2;
    }
    return inner;
}

} // namespace rawframe::world_kest
