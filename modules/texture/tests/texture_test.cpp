// The cooked texture (D253): each format round trips through KTX 2.0 with
// its levels, the descriptor is Khronos's for the format, and a texture
// that breaks a rule, or bytes that are not exactly what the writer makes,
// are refused before anything past the limits is allocated.

#include "rawframe/test/test.h"
#include "rawframe/texture/errors.h"
#include "rawframe/texture/texture.h"

#include <cstdint>
#include <span>
#include <vector>

using namespace rawframe;
using namespace rawframe::texture;

namespace {

/// A full chain of levels for these sides, each byte telling its level and
/// place.
Texture chain(Format format, std::uint32_t width, std::uint32_t height, std::size_t levels = 0) {
    Texture texture{.format = format};
    for (std::uint32_t level = 0;; ++level) {
        Level made{.width = std::max(width >> level, 1U), .height = std::max(height >> level, 1U)};
        made.bytes.resize(levelBytes(format, made.width, made.height));
        for (std::size_t at = 0; at < made.bytes.size(); ++at) {
            made.bytes[at] = static_cast<std::byte>((at * 7) + level);
        }
        texture.levels.push_back(std::move(made));
        if ((levels != 0 && texture.levels.size() == levels) || (made.width == 1 && made.height == 1)) {
            break;
        }
    }
    return texture;
}

bool refusedWith(const result::Status& status, TextureError error) {
    return !status.has_value() && status.error().domain() == kTextureDomain && status.error().code() == code(error);
}

template <typename T> bool refusedWith(const result::Result<T>& outcome, TextureError error) {
    return !outcome.has_value() && outcome.error().domain() == kTextureDomain && outcome.error().code() == code(error);
}

std::uint32_t wordAt(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t each = 0; each < 4; ++each) {
        value |= std::to_integer<std::uint32_t>(bytes[at + each]) << (8 * each);
    }
    return value;
}

} // namespace

RAWFRAME_TEST(EveryFormatRoundTripsWithItsLevels) {
    for (const Format kFormat : {Format::Rgba8, Format::Rgba8Srgb, Format::Bc7, Format::Bc7Srgb}) {
        for (const Texture& kTexture : {chain(kFormat, 5, 3), chain(kFormat, 64, 16), chain(kFormat, 9, 9, 1)}) {
            const auto kBytes = encode(kTexture);
            RAWFRAME_EXPECT(kBytes.has_value());
            if (!kBytes.has_value()) {
                continue;
            }
            const auto kRead = decode(*kBytes);
            RAWFRAME_EXPECT(kRead.has_value() && *kRead == kTexture);
        }
    }
    // Five by three halves to two by one, then one by one; BC7 pads blocks.
    const Texture kOdd = chain(Format::Bc7, 5, 3);
    RAWFRAME_EXPECT(kOdd.levels.size() == 3 && kOdd.levels[1].width == 2 && kOdd.levels[1].height == 1 &&
                    kOdd.levels[0].bytes.size() == 2 * 1 * 16 && levelBytes(Format::Rgba8, 5, 3) == 60);
}

RAWFRAME_TEST(TheContainerIsKhronossForEachFormat) {
    // The Vulkan format, then the descriptor's color model, primaries, and
    // transfer, and for sRGB RGBA8 the alpha sample marked linear.
    struct Case {
        Format format;
        std::uint32_t vulkan;
        std::uint32_t model;
        std::uint32_t transfer;
    };
    for (const Case& each : {Case{Format::Rgba8, 37, 1, 1},
                             Case{Format::Rgba8Srgb, 43, 1, 2},
                             Case{Format::Bc7, 145, 134, 1},
                             Case{Format::Bc7Srgb, 146, 134, 2}}) {
        const auto kBytes = encode(chain(each.format, 8, 8));
        RAWFRAME_EXPECT(kBytes.has_value());
        if (!kBytes.has_value()) {
            continue;
        }
        const std::size_t kDescriptorAt = wordAt(*kBytes, 48);
        const std::uint32_t kModel = wordAt(*kBytes, kDescriptorAt + 12);
        RAWFRAME_EXPECT(wordAt(*kBytes, 12) == each.vulkan && (kModel & 0xFFU) == each.model &&
                        ((kModel >> 8U) & 0xFFU) == 1 && ((kModel >> 16U) & 0xFFU) == each.transfer);
        if (each.format == Format::Rgba8Srgb) {
            const std::uint32_t kAlpha = wordAt(*kBytes, kDescriptorAt + 28 + (3 * 16));
            RAWFRAME_EXPECT((kAlpha >> 24U) == (15U | 0x10U));
        }
        // Levels are smallest first, each on its block's alignment.
        const std::uint64_t kFirst = wordAt(*kBytes, 80);
        const std::uint64_t kLast = wordAt(*kBytes, 80 + (3 * 24));
        const std::uint64_t kAlignment = each.format == Format::Bc7 || each.format == Format::Bc7Srgb ? 16 : 4;
        RAWFRAME_EXPECT(kLast < kFirst && kFirst % kAlignment == 0 && kLast % kAlignment == 0);
    }
}

RAWFRAME_TEST(ATextureThatBreaksARuleIsRefused) {
    RAWFRAME_EXPECT(refusedWith(validate(Texture{}), TextureError::BadTexture));
    Texture unhalved = chain(Format::Rgba8, 8, 8);
    unhalved.levels[1].width = 3;
    RAWFRAME_EXPECT(refusedWith(validate(unhalved), TextureError::BadTexture));
    Texture shortLevel = chain(Format::Bc7, 8, 8);
    shortLevel.levels[2].bytes.pop_back();
    RAWFRAME_EXPECT(refusedWith(encode(shortLevel), TextureError::BadTexture));
    Texture tooMany = chain(Format::Rgba8, 4, 4);
    tooMany.levels.push_back(tooMany.levels.back());
    RAWFRAME_EXPECT(refusedWith(validate(tooMany), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(validate(chain(Format::Rgba8, 64, 2), {.maximumSide = 32}), TextureError::OverLimit));
    RAWFRAME_EXPECT(
        refusedWith(validate(chain(Format::Rgba8, 16, 16), {.maximumBytes = 1000}), TextureError::OverLimit));
}

RAWFRAME_TEST(BytesNotExactlyAsWrittenAreRefused) {
    const auto kBytes = encode(chain(Format::Rgba8Srgb, 6, 5));
    RAWFRAME_EXPECT(kBytes.has_value());
    if (!kBytes.has_value()) {
        return;
    }
    const auto kChanged = [&](std::size_t at, std::byte value) {
        std::vector<std::byte> changed = *kBytes;
        changed[at] = value;
        return decode(changed);
    };
    const std::size_t kDescriptorAt = wordAt(*kBytes, 48);
    RAWFRAME_EXPECT(refusedWith(kChanged(1, std::byte{'k'}), TextureError::BadTexture));
    // Another format, a type size, supercompression, the descriptor, the
    // key and value data, a level's offset.
    RAWFRAME_EXPECT(refusedWith(kChanged(12, std::byte{44}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(kChanged(16, std::byte{2}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(kChanged(44, std::byte{2}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(kChanged(kDescriptorAt + 14, std::byte{1}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(kChanged(60, std::byte{1}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(kChanged(80, std::byte{0xFF}), TextureError::BadTexture));
    // Cut short anywhere, or with a byte more.
    for (const std::size_t kKeep : {std::size_t{0}, std::size_t{40}, std::size_t{100}, kBytes->size() - 1}) {
        RAWFRAME_EXPECT(refusedWith(decode(std::span{*kBytes}.first(kKeep)), TextureError::BadTexture));
    }
    std::vector<std::byte> longer = *kBytes;
    longer.push_back(std::byte{0});
    RAWFRAME_EXPECT(refusedWith(decode(longer), TextureError::BadTexture));
    // Sides past the limits are refused before the levels are read.
    RAWFRAME_EXPECT(refusedWith(decode(*kBytes, {.maximumSide = 4}), TextureError::OverLimit));
}
