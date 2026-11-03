// Texture import: each admitted source format decodes to the texels
// it holds, run-length BMPs as encoders end them included; other formats,
// broken files, and sides past the limit are refused; levels halve in
// linear light weighted by alpha; BC7 keeps the image close and cooks to
// the same bytes every time.

#include "rawframe/test/files.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/errors.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::texture_import;

namespace {

std::vector<std::byte> source(const std::string& name) {
    const std::string kText = test::readFile(std::string{RAWFRAME_TEXTURE_IMPORT_DATA} + name);
    const auto* const kBytes = reinterpret_cast<const std::byte*>(kText.data());
    return {kBytes, kBytes + kText.size()};
}

/// The pattern the PNG, BMP, and TGA files hold (tests/data, made by a
/// script from this formula): red and blue ramps across, green down, and
/// the last two columns half and fully transparent.
std::array<std::uint8_t, 4> pattern(std::uint32_t x, std::uint32_t y) {
    return {static_cast<std::uint8_t>(x * 32),
            static_cast<std::uint8_t>(y * 64),
            static_cast<std::uint8_t>(255 - (x * 32)),
            static_cast<std::uint8_t>(x < 6 ? 255 : (x == 6 ? 128 : 0))};
}

std::array<std::uint8_t, 4> texel(const Image& image, std::uint32_t x, std::uint32_t y) {
    std::array<std::uint8_t, 4> value{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        value[channel] = std::to_integer<std::uint8_t>(image.rgba[((y * image.width + x) * 4) + channel]);
    }
    return value;
}

bool holdsPattern(const Image& image, bool alpha) {
    if (image.width != 8 || image.height != 4) {
        return false;
    }
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            std::array<std::uint8_t, 4> expected = pattern(x, y);
            expected[3] = alpha ? expected[3] : 255;
            if (texel(image, x, y) != expected) {
                return false;
            }
        }
    }
    return true;
}

template <typename T> bool refusedWith(const result::Result<T>& outcome, texture::TextureError error) {
    return !outcome.has_value() && outcome.error().domain() == texture::kTextureDomain &&
           outcome.error().code() == code(error);
}

Image solid(std::uint32_t width, std::uint32_t height, const std::vector<std::array<std::uint8_t, 4>>& texels) {
    Image image{.width = width, .height = height};
    for (const auto& kTexel : texels) {
        for (const std::uint8_t kChannel : kTexel) {
            image.rgba.push_back(static_cast<std::byte>(kChannel));
        }
    }
    return image;
}

/// A run-length encoded BMP: RLE8 (8 bits a texel) or RLE4 (4), a negative
/// height for rows top down, a palette of red then blue, and the encoded
/// rows as they are.
std::vector<std::byte>
rleBmp(std::int32_t width, std::int32_t height, std::uint16_t bits, const std::vector<std::uint8_t>& rows) {
    std::vector<std::byte> out;
    const auto kPut = [&out](std::uint32_t value, std::size_t bytes) {
        for (std::size_t at = 0; at < bytes; ++at) {
            out.push_back(static_cast<std::byte>((value >> (at * 8)) & 0xFFU));
        }
    };
    constexpr std::uint32_t kOffset = 14 + 40 + 8;
    out.push_back(std::byte{'B'});
    out.push_back(std::byte{'M'});
    kPut(kOffset + static_cast<std::uint32_t>(rows.size()), 4);
    kPut(0, 4);
    kPut(kOffset, 4);
    kPut(40, 4);
    kPut(static_cast<std::uint32_t>(width), 4);
    kPut(static_cast<std::uint32_t>(height), 4);
    kPut(1, 2);
    kPut(bits, 2);
    kPut(bits == 8 ? 1 : 2, 4);
    kPut(static_cast<std::uint32_t>(rows.size()), 4);
    kPut(2835, 4);
    kPut(2835, 4);
    kPut(2, 4);
    kPut(0, 4);
    // Blue, green, red, and a byte unused: red, then blue.
    kPut(0x00FF0000U, 4);
    kPut(0x000000FFU, 4);
    for (const std::uint8_t kByte : rows) {
        out.push_back(static_cast<std::byte>(kByte));
    }
    return out;
}

} // namespace

RAWFRAME_TEST(EachAdmittedFormatDecodesToItsTexels) {
    const auto kPng = decodeImage(source("pattern.png"));
    const auto kTga = decodeImage(source("pattern.tga"));
    const auto kBmp = decodeImage(source("pattern.bmp"));
    RAWFRAME_EXPECT(kPng.has_value() && holdsPattern(*kPng, true));
    RAWFRAME_EXPECT(kTga.has_value() && holdsPattern(*kTga, true));
    // A 24-bit BMP has no alpha: every texel is opaque.
    RAWFRAME_EXPECT(kBmp.has_value() && holdsPattern(*kBmp, false));
    // JPEG is lossy: its four quadrants are near red, green, blue, and white.
    const auto kJpeg = decodeImage(source("quadrants.jpg"));
    RAWFRAME_EXPECT(kJpeg.has_value() && kJpeg->width == 16 && kJpeg->height == 16);
    if (kJpeg.has_value()) {
        const auto kNear = [&](std::uint32_t x, std::uint32_t y, std::array<int, 3> expected) {
            const auto kFound = texel(*kJpeg, x, y);
            return std::abs(kFound[0] - expected[0]) < 24 && std::abs(kFound[1] - expected[1]) < 24 &&
                   std::abs(kFound[2] - expected[2]) < 24 && kFound[3] == 255;
        };
        RAWFRAME_EXPECT(kNear(3, 3, {255, 0, 0}) && kNear(12, 3, {0, 255, 0}) && kNear(3, 12, {0, 0, 255}) &&
                        kNear(12, 12, {255, 255, 255}));
    }
}

RAWFRAME_TEST(RunLengthBmpsThatEndALineThenTheBitmapDecode) {
    // Each ends its last row with an end-of-line escape (0 0) and then the
    // bitmap (0 1), as encoders commonly write it. The decoder is then past
    // the image when the end of the bitmap clears the rest of its row.
    constexpr std::array<std::uint8_t, 4> kRed = {255, 0, 0, 255};
    constexpr std::array<std::uint8_t, 4> kBlue = {0, 0, 255, 255};
    // RLE8, rows bottom up: two blue texels, then two red above them.
    const auto kEight = decodeImage(rleBmp(2, 2, 8, {2, 1, 0, 0, 2, 0, 0, 0, 0, 1}));
    RAWFRAME_EXPECT(kEight.has_value() && kEight->width == 2 && kEight->height == 2 && texel(*kEight, 0, 0) == kRed &&
                    texel(*kEight, 1, 0) == kRed && texel(*kEight, 0, 1) == kBlue && texel(*kEight, 1, 1) == kBlue);
    // RLE8 of one texel, bottom up and top down.
    for (const std::int32_t kHeight : {1, -1}) {
        const auto kOne = decodeImage(rleBmp(1, kHeight, 8, {1, 1, 0, 0, 0, 1}));
        RAWFRAME_EXPECT(kOne.has_value() && kOne->width == 1 && kOne->height == 1 && texel(*kOne, 0, 0) == kBlue);
    }
    // RLE4: one texel of the high nibble's color.
    const auto kFour = decodeImage(rleBmp(1, 1, 4, {1, 0x10, 0, 0, 0, 1}));
    RAWFRAME_EXPECT(kFour.has_value() && kFour->width == 1 && kFour->height == 1 && texel(*kFour, 0, 0) == kBlue);
}

RAWFRAME_TEST(OtherFormatsBrokenFilesAndLargeSidesAreRefused) {
    RAWFRAME_EXPECT(refusedWith(decodeImage({}), texture::TextureError::BadSource));
    const std::string kText = "not an image at all";
    RAWFRAME_EXPECT(refusedWith(decodeImage(std::as_bytes(std::span{kText})), texture::TextureError::BadSource));
    // GIF is a format Wuffs knows and the engine does not admit.
    const std::string kGif = std::string{"GIF89a"} + std::string(20, '\0');
    RAWFRAME_EXPECT(refusedWith(decodeImage(std::as_bytes(std::span{kGif})), texture::TextureError::BadSource));
    // A PNG cut short is refused, not taken as far as it goes.
    std::vector<std::byte> cut = source("pattern.png");
    cut.resize(cut.size() - 20);
    RAWFRAME_EXPECT(refusedWith(decodeImage(cut), texture::TextureError::BadSource));
    RAWFRAME_EXPECT(
        refusedWith(decodeImage(source("pattern.png"), {.maximumSide = 4}), texture::TextureError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(cookTexture(Image{.width = 2, .height = 2}), texture::TextureError::BadTexture));
}

RAWFRAME_TEST(LevelsHalveInLinearLightWeightedByAlpha) {
    // An opaque red beside a transparent green: the half is red, half
    // covered, not a dark blend of the two.
    const auto kEdge = cookTexture(solid(2, 1, {{255, 0, 0, 255}, {0, 255, 0, 0}}), {.exact = true});
    RAWFRAME_EXPECT(kEdge.has_value() && kEdge->levels.size() == 2 && kEdge->format == texture::Format::Rgba8Srgb);
    if (kEdge.has_value() && kEdge->levels.size() == 2) {
        RAWFRAME_EXPECT(texel(*texelsOf(*kEdge, 1), 0, 0) == (std::array<std::uint8_t, 4>{255, 0, 0, 128}));
    }
    // Black and white average to middle gray in light: 188 in sRGB, 128 in
    // linear data.
    const Image kChecker = solid(2, 2, {{0, 0, 0, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {0, 0, 0, 255}});
    const auto kSrgb = cookTexture(kChecker, {.exact = true});
    const auto kLinear = cookTexture(kChecker, {.srgb = false, .exact = true});
    RAWFRAME_EXPECT(kSrgb.has_value() && kLinear.has_value() && kLinear->format == texture::Format::Rgba8);
    if (kSrgb.has_value() && kLinear.has_value()) {
        RAWFRAME_EXPECT(texel(*texelsOf(*kSrgb, 1), 0, 0)[0] == 188 && texel(*texelsOf(*kLinear, 1), 0, 0)[0] == 128);
    }
    // The pattern halves to four levels, the first the image itself.
    const auto kPattern = cookTexture(*decodeImage(source("pattern.png")), {.exact = true});
    RAWFRAME_EXPECT(kPattern.has_value() && kPattern->levels.size() == 4 && kPattern->levels[3].width == 1 &&
                    holdsPattern(*texelsOf(*kPattern, 0), true));
    const auto kOne = cookTexture(*decodeImage(source("pattern.png")), {.levels = false});
    RAWFRAME_EXPECT(kOne.has_value() && kOne->levels.size() == 1);
}

RAWFRAME_TEST(BlockCompressionStaysCloseAndCooksTheSameEveryTime) {
    // What sprites are made of: flat color (the JPEG's quadrants, decoded),
    // and a ramp across with its alpha fading the same way, an edge.
    Image ramp{.width = 32, .height = 8};
    for (std::uint32_t y = 0; y < ramp.height; ++y) {
        for (std::uint32_t x = 0; x < ramp.width; ++x) {
            for (const std::uint32_t kValue : {x * 8, 255 - (x * 8), 96U, std::min(x * 16, 255U)}) {
                ramp.rgba.push_back(static_cast<std::byte>(kValue));
            }
        }
    }
    const auto kJpeg = decodeImage(source("quadrants.jpg"));
    RAWFRAME_EXPECT(kJpeg.has_value());
    if (!kJpeg.has_value()) {
        return;
    }
    for (const Image& kImage : {*kJpeg, ramp}) {
        const auto kFirst = cookTexture(kImage);
        const auto kSecond = cookTexture(kImage);
        RAWFRAME_EXPECT(kFirst.has_value() && kFirst->format == texture::Format::Bc7Srgb && kSecond.has_value() &&
                        *kFirst == *kSecond);
        if (!kFirst.has_value()) {
            continue;
        }
        const auto kBack = texelsOf(*kFirst, 0);
        RAWFRAME_EXPECT(kBack.has_value());
        int worst = 0;
        for (std::size_t at = 0; kBack.has_value() && at < kImage.rgba.size(); ++at) {
            worst = std::max(worst,
                             std::abs(std::to_integer<int>(kBack->rgba[at]) - std::to_integer<int>(kImage.rgba[at])));
        }
        RAWFRAME_EXPECT(worst <= 8);
        if (worst > 8) {
            std::fprintf(stderr, "  %ux%u: worst %d\n", kImage.width, kImage.height, worst);
        }
    }
    // A pattern BC7 cannot hold exactly still cooks, every level, and a
    // level smaller than a block still fills one.
    const auto kPattern = cookTexture(*decodeImage(source("pattern.png")));
    RAWFRAME_EXPECT(kPattern.has_value() && kPattern->levels.size() == 4 && kPattern->levels[3].bytes.size() == 16 &&
                    texelsOf(*kPattern, 3).has_value());
}
