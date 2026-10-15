#include "rawframe/render/capture.h"

#include <algorithm>
#include <array>

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

namespace {

constexpr std::array<char, 8> kStreamMark = {'r', 'f', 'p', 'i', 'c', '1', '\0', '\0'};
constexpr std::size_t kStreamHeader = 8 + 8 + 4 + 4;
constexpr std::uint32_t kStreamSide = 8192;

void put(std::vector<std::byte>& into, std::uint64_t value, int bytes) {
    for (int each = 0; each < bytes; ++each) {
        into.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(each))) & 0xFFU));
    }
}

std::uint64_t got(std::span<const std::byte> from, std::size_t at, int bytes) {
    std::uint64_t made = 0;
    for (int each = 0; each < bytes; ++each) {
        made |= std::to_integer<std::uint64_t>(from[at + static_cast<std::size_t>(each)])
                << (8U * static_cast<unsigned>(each));
    }
    return made;
}

} // namespace

std::vector<std::byte> streamedOf(const StreamedPicture& picture) {
    std::vector<std::byte> made;
    made.reserve(kStreamHeader + picture.pixels.size());
    for (const char kEach : kStreamMark) {
        made.push_back(static_cast<std::byte>(kEach));
    }
    put(made, picture.sequence, 8);
    put(made, picture.width, 4);
    put(made, picture.height, 4);
    made.insert(made.end(), picture.pixels.begin(), picture.pixels.end());
    return made;
}

std::optional<StreamedPicture> streamedFrom(std::span<const std::byte> bytes) {
    if (bytes.size() < kStreamHeader ||
        !std::ranges::equal(bytes.first(kStreamMark.size()), kStreamMark, [](std::byte held, char wanted) {
            return held == static_cast<std::byte>(wanted);
        })) {
        return std::nullopt;
    }
    StreamedPicture made{.sequence = got(bytes, 8, 8),
                         .width = static_cast<std::uint32_t>(got(bytes, 16, 4)),
                         .height = static_cast<std::uint32_t>(got(bytes, 20, 4)),
                         .pixels = {}};
    if (made.width == 0 || made.height == 0 || made.width > kStreamSide || made.height > kStreamSide ||
        bytes.size() - kStreamHeader != std::size_t{made.width} * made.height * 4) {
        return std::nullopt;
    }
    made.pixels.assign(bytes.begin() + kStreamHeader, bytes.end());
    return made;
}

} // namespace rawframe::render
