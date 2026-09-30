#include "rawframe/render/capture.h"

namespace rawframe::render {

std::vector<std::byte> tgaOf(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height) {
    std::vector<std::byte> image(18 + pixels.size());
    image[2] = std::byte{2};
    image[12] = static_cast<std::byte>(width & 0xFFU);
    image[13] = static_cast<std::byte>(width >> 8U);
    image[14] = static_cast<std::byte>(height & 0xFFU);
    image[15] = static_cast<std::byte>(height >> 8U);
    image[16] = std::byte{32};
    // Eight bits of alpha, rows top first.
    image[17] = std::byte{0x28};
    for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
        image[18 + at] = pixels[at + 2];
        image[18 + at + 1] = pixels[at + 1];
        image[18 + at + 2] = pixels[at];
        image[18 + at + 3] = pixels[at + 3];
    }
    return image;
}

} // namespace rawframe::render
