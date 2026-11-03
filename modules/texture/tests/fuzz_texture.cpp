// Coverage-guided fuzzing of a cooked texture read from content, which
// a mod's Build can hold. The reader is exact both ways: a texture it
// accepts encodes to the very same bytes, and a texture the writer takes,
// made here from the input, reads back as itself.

#include "rawframe/texture/texture.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>
#include <vector>

using namespace rawframe;

namespace {

/// A texture of the shape the input's first four bytes give (format,
/// sides, levels, faces, depth), its levels filled from the rest. A side is
/// up to 64, or, for a long thin texture, a power of two up to the reader's
/// 16384 beside one of up to four, so every count of levels is reached
/// without a texture of gigabytes.
std::optional<texture::Texture> madeFrom(std::span<const std::byte> bytes) {
    if (bytes.size() < 4) {
        return std::nullopt;
    }
    const auto kByte = [&bytes](std::size_t at) {
        return std::to_integer<std::uint32_t>(bytes[at]);
    };
    const std::uint32_t kShape = kByte(3);
    texture::Texture made{.format = static_cast<texture::Format>(kByte(0) % 6),
                          .levels = {},
                          .faces = (kShape & 8U) != 0 ? 6U : 1U,
                          .depth = (kShape & 16U) != 0 ? 2U + (kShape >> 5U) : 1U};
    const bool kLong = (kByte(1) & 0x80U) != 0;
    const std::uint32_t kWidth = kLong ? 1U << (kByte(1) % 15) : (kByte(1) % 64) + 1;
    const std::uint32_t kHeight = kLong ? (kByte(2) % 4) + 1 : (kByte(2) % 64) + 1;
    const std::span<const std::byte> kFill = bytes.subspan(4);
    std::size_t at = 0;
    // Up to 16 levels: past what the sides allow is the writer's to refuse.
    const std::uint32_t kLevels = 1 + ((kShape & 7U) | ((kByte(0) >> 4U) & 8U));
    for (std::uint32_t index = 0; index < kLevels; ++index) {
        texture::Level level{.width = std::max(kWidth >> index, 1U), .height = std::max(kHeight >> index, 1U)};
        level.bytes.resize(std::size_t{made.faces} * made.depth *
                           texture::levelBytes(made.format, level.width, level.height));
        for (std::byte& each : level.bytes) {
            each = kFill.empty() ? std::byte{0} : kFill[at++ % kFill.size()];
        }
        made.levels.push_back(std::move(level));
    }
    return made;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kTexture = texture::decode(kBytes);
    if (kTexture.has_value()) {
        const auto kEncoded = texture::encode(*kTexture);
        if (!kEncoded.has_value() || !std::ranges::equal(*kEncoded, kBytes)) {
            std::abort();
        }
    }
    const std::optional<texture::Texture> kMade = madeFrom(kBytes);
    if (!kMade.has_value()) {
        return 0;
    }
    const auto kWritten = texture::encode(*kMade);
    if (kWritten.has_value() && texture::decode(*kWritten) != *kMade) {
        std::abort();
    }
    return 0;
}
