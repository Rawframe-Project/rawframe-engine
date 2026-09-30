#include "rawframe/texture/texture.h"

#include "rawframe/texture/errors.h"

#include <algorithm>
#include <bit>
#include <optional>

namespace rawframe::texture {

namespace {

// KTX 2.0's identifier, then its header: nine 32-bit fields, the index's
// four 32-bit and two 64-bit fields, and 24 bytes a level.
constexpr std::array<std::uint8_t, 12> kIdentifier = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
constexpr std::size_t kHeaderBytes = 12 + (9 * 4) + (4 * 4) + (2 * 8);
constexpr std::size_t kLevelIndexBytes = 3 * 8;

// Khronos Data Format constants (khr_df.h) for the formats here.
constexpr std::uint32_t kModelRgbsda = 1;
constexpr std::uint32_t kModelBc7 = 134;
constexpr std::uint32_t kPrimariesBt709 = 1;
constexpr std::uint32_t kTransferLinear = 1;
constexpr std::uint32_t kTransferSrgb = 2;
constexpr std::uint32_t kVersion13 = 2;
constexpr std::uint32_t kChannelAlpha = 15;
constexpr std::uint32_t kSampleLinear = 1U << 4U;
constexpr std::uint32_t kSampleSigned = 1U << 6U;
constexpr std::uint32_t kSampleFloat = 1U << 7U;
// A float sample's range, as the descriptor gives it: -1 and 1 as floats.
constexpr std::uint32_t kFloatLower = 0xBF800000U;
constexpr std::uint32_t kFloatUpper = 0x3F800000U;

std::unexpected<result::Error> bad(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kTextureDomain, code(TextureError::BadTexture), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::ResourceExhausted, kTextureDomain, code(TextureError::OverLimit), why);
}

bool compressed(Format format) noexcept {
    return format == Format::Bc7 || format == Format::Bc7Srgb;
}

std::uint32_t vulkanFormat(Format format) noexcept {
    switch (format) {
    case Format::Rgba8:
        return 37; // VK_FORMAT_R8G8B8A8_UNORM
    case Format::Rgba8Srgb:
        return 43; // VK_FORMAT_R8G8B8A8_SRGB
    case Format::Bc7:
        return 145; // VK_FORMAT_BC7_UNORM_BLOCK
    case Format::Bc7Srgb:
        return 146; // VK_FORMAT_BC7_SRGB_BLOCK
    case Format::Rgba16Float:
        return 97; // VK_FORMAT_R16G16B16A16_SFLOAT
    }
    return 0;
}

/// Bytes a texel of an uncompressed format takes.
std::uint32_t texelBytes(Format format) noexcept {
    return format == Format::Rgba16Float ? 8 : 4;
}

/// KTX 2.0's type size: a channel's bytes, for what a reader swaps.
std::uint32_t typeSize(Format format) noexcept {
    return format == Format::Rgba16Float ? 2 : 1;
}

std::optional<Format> formatOfVulkan(std::uint32_t value) noexcept {
    for (const Format kFormat : {Format::Rgba8, Format::Rgba8Srgb, Format::Bc7, Format::Bc7Srgb, Format::Rgba16Float}) {
        if (vulkanFormat(kFormat) == value) {
            return kFormat;
        }
    }
    return std::nullopt;
}

/// A level's alignment in the file: its texel block's bytes and four,
/// whose least common multiple is the larger here.
std::size_t alignmentOf(Format format) noexcept {
    return compressed(format) ? 16 : texelBytes(format);
}

/// The data format descriptor, total size first, as 32-bit words.
std::vector<std::uint32_t> descriptorOf(Format format) {
    const std::uint32_t kTransfer = isSrgb(format) ? kTransferSrgb : kTransferLinear;
    std::vector<std::uint32_t> words;
    if (compressed(format)) {
        constexpr std::uint32_t kBlockBytes = 24 + 16;
        words = {4 + kBlockBytes,
                 0,
                 kVersion13 | (kBlockBytes << 16U),
                 kModelBc7 | (kPrimariesBt709 << 8U) | (kTransfer << 16U),
                 3U | (3U << 8U),
                 16,
                 0,
                 // One sample: 128 bits of BC7 color.
                 0U | (127U << 16U),
                 0,
                 0,
                 0xFFFFFFFFU};
        return words;
    }
    constexpr std::uint32_t kBlockBytes = 24 + (4 * 16);
    const std::uint32_t kBits = texelBytes(format) * 2;
    const bool kFloat = format == Format::Rgba16Float;
    words = {4 + kBlockBytes,
             0,
             kVersion13 | (kBlockBytes << 16U),
             kModelRgbsda | (kPrimariesBt709 << 8U) | (kTransfer << 16U),
             0,
             texelBytes(format),
             0};
    // Red, green, blue, and alpha, a byte or a half float each; alpha is
    // linear even in an sRGB format.
    constexpr std::array<std::uint32_t, 4> kChannels = {0, 1, 2, kChannelAlpha};
    for (std::uint32_t index = 0; index < 4; ++index) {
        const std::uint32_t kType = kChannels[index] |
                                    (kChannels[index] == kChannelAlpha && isSrgb(format) ? kSampleLinear : 0U) |
                                    (kFloat ? kSampleFloat | kSampleSigned : 0U);
        for (const std::uint32_t kWord : {(index * kBits) | ((kBits - 1) << 16U) | (kType << 24U),
                                          0U,
                                          kFloat ? kFloatLower : 0U,
                                          kFloat ? kFloatUpper : 255U}) {
            words.push_back(kWord);
        }
    }
    return words;
}

void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void putU64(std::vector<std::byte>& out, std::uint64_t value) {
    putU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
    putU32(out, static_cast<std::uint32_t>(value >> 32U));
}

std::uint32_t getU32(std::span<const std::byte> bytes, std::size_t at) noexcept {
    std::uint32_t value = 0;
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
        value |= std::to_integer<std::uint32_t>(bytes[at++]) << shift;
    }
    return value;
}

std::uint64_t getU64(std::span<const std::byte> bytes, std::size_t at) noexcept {
    return getU32(bytes, at) | (std::uint64_t{getU32(bytes, at + 4)} << 32U);
}

std::size_t alignedUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) / alignment * alignment;
}

/// The most levels a texture of these sides may have.
std::size_t mostLevels(std::uint32_t width, std::uint32_t height) noexcept {
    return static_cast<std::size_t>(std::bit_width(std::max(width, height)));
}

/// The sum of every level's bytes, or nothing past the limit.
std::optional<std::size_t>
totalBytes(Format format, std::uint32_t width, std::uint32_t height, std::size_t levels, std::size_t limit) noexcept {
    std::size_t total = 0;
    for (std::size_t level = 0; level < levels; ++level) {
        total += levelBytes(format, std::max(width >> level, 1U), std::max(height >> level, 1U));
        if (total > limit) {
            return std::nullopt;
        }
    }
    return total;
}

} // namespace

std::uint16_t halfOf(float value) noexcept {
    const std::uint32_t kBits = std::bit_cast<std::uint32_t>(value);
    const auto kSign = static_cast<std::uint16_t>((kBits >> 16U) & 0x8000U);
    const std::uint32_t kExponent = (kBits >> 23U) & 0xFFU;
    std::uint32_t mantissa = kBits & 0x7FFFFFU;
    if (kExponent == 0xFFU) {
        // Infinity stays infinite; a NaN stays a quiet NaN.
        return static_cast<std::uint16_t>(kSign | 0x7C00U | (mantissa != 0 ? 0x200U : 0U));
    }
    const int kHalfExponent = static_cast<int>(kExponent) - 127 + 15;
    if (kHalfExponent >= 31) {
        return static_cast<std::uint16_t>(kSign | 0x7C00U);
    }
    // A subnormal half keeps the mantissa's leading one, shifted into it.
    std::uint32_t shift = 13;
    std::uint32_t half = 0;
    if (kHalfExponent <= 0) {
        if (kHalfExponent < -10) {
            return kSign;
        }
        mantissa |= 0x800000U;
        shift = static_cast<std::uint32_t>(14 - kHalfExponent);
        half = mantissa >> shift;
    } else {
        half = (static_cast<std::uint32_t>(kHalfExponent) << 10U) | (mantissa >> shift);
    }
    const std::uint32_t kRemainder = mantissa & ((1U << shift) - 1U);
    const std::uint32_t kHalfway = 1U << (shift - 1U);
    // A carry out of the mantissa raises the exponent, as rounding should.
    if (kRemainder > kHalfway || (kRemainder == kHalfway && (half & 1U) != 0)) {
        ++half;
    }
    return static_cast<std::uint16_t>(kSign | half);
}

float floatOf(std::uint16_t half) noexcept {
    const std::uint32_t kSign = (std::uint32_t{half} & 0x8000U) << 16U;
    const std::uint32_t kExponent = (std::uint32_t{half} >> 10U) & 0x1FU;
    const std::uint32_t kMantissa = std::uint32_t{half} & 0x3FFU;
    if (kExponent == 0) {
        // Nought or subnormal: the mantissa in units of two to the -24.
        const float kValue = static_cast<float>(kMantissa) * 0x1p-24F;
        return kSign != 0 ? -kValue : kValue;
    }
    if (kExponent == 31) {
        return std::bit_cast<float>(kSign | 0x7F800000U | (kMantissa << 13U));
    }
    return std::bit_cast<float>(kSign | ((kExponent + 112U) << 23U) | (kMantissa << 13U));
}

bool isSrgb(Format format) noexcept {
    return format == Format::Rgba8Srgb || format == Format::Bc7Srgb;
}

std::size_t levelBytes(Format format, std::uint32_t width, std::uint32_t height) noexcept {
    if (compressed(format)) {
        return std::size_t{(width + 3) / 4} * ((height + 3) / 4) * 16;
    }
    return std::size_t{width} * height * texelBytes(format);
}

result::Status validate(const Texture& texture, const TextureLimits& limits) {
    if (texture.levels.empty()) {
        return bad("a texture has at least one level");
    }
    const std::uint32_t kWidth = texture.levels[0].width;
    const std::uint32_t kHeight = texture.levels[0].height;
    if (kWidth == 0 || kHeight == 0) {
        return bad("a texture's sides are not nought");
    }
    if ((texture.faces != 1 && texture.faces != 6) || (texture.faces == 6 && kWidth != kHeight)) {
        return bad("a texture has one face, or six square ones");
    }
    if (kWidth > limits.maximumSide || kHeight > limits.maximumSide) {
        return overLimit("a texture's side is past its limits");
    }
    if (texture.levels.size() > mostLevels(kWidth, kHeight)) {
        return bad("a texture has more levels than halving its sides makes");
    }
    if (!totalBytes(texture.format, kWidth, kHeight, texture.levels.size(), limits.maximumBytes / texture.faces)
             .has_value()) {
        return overLimit("a texture holds more bytes than its limits allow");
    }
    for (std::size_t index = 0; index < texture.levels.size(); ++index) {
        const Level& kLevel = texture.levels[index];
        if (kLevel.width != std::max(kWidth >> index, 1U) || kLevel.height != std::max(kHeight >> index, 1U)) {
            return bad("each level halves the one before, never below one");
        }
        if (kLevel.bytes.size() != texture.faces * levelBytes(texture.format, kLevel.width, kLevel.height)) {
            return bad("a level's bytes do not fill its faces exactly");
        }
    }
    return {};
}

result::Result<std::vector<std::byte>> encode(const Texture& texture, const TextureLimits& limits) {
    RAWFRAME_TRY(validate(texture, limits));
    const std::vector<std::uint32_t> kDescriptor = descriptorOf(texture.format);
    const std::size_t kLevels = texture.levels.size();
    const std::size_t kDescriptorAt = kHeaderBytes + (kLevels * kLevelIndexBytes);
    const std::size_t kAlignment = alignmentOf(texture.format);
    // The levels go smallest first, each at its block's alignment.
    std::vector<std::size_t> offsets(kLevels);
    std::size_t end = kDescriptorAt + (kDescriptor.size() * 4);
    for (std::size_t index = kLevels; index-- > 0;) {
        offsets[index] = alignedUp(end, kAlignment);
        end = offsets[index] + texture.levels[index].bytes.size();
    }

    std::vector<std::byte> out;
    out.reserve(end);
    for (const std::uint8_t kByte : kIdentifier) {
        out.push_back(static_cast<std::byte>(kByte));
    }
    // The format, its type size, the sides, no depth, no layers, the
    // faces, the levels, and no supercompression.
    for (const std::uint32_t kField : {vulkanFormat(texture.format),
                                       typeSize(texture.format),
                                       texture.levels[0].width,
                                       texture.levels[0].height,
                                       0U,
                                       0U,
                                       texture.faces,
                                       static_cast<std::uint32_t>(kLevels),
                                       0U}) {
        putU32(out, kField);
    }
    putU32(out, static_cast<std::uint32_t>(kDescriptorAt));
    putU32(out, static_cast<std::uint32_t>(kDescriptor.size() * 4));
    putU32(out, 0);
    putU32(out, 0);
    putU64(out, 0);
    putU64(out, 0);
    for (std::size_t index = 0; index < kLevels; ++index) {
        putU64(out, offsets[index]);
        putU64(out, texture.levels[index].bytes.size());
        putU64(out, texture.levels[index].bytes.size());
    }
    for (const std::uint32_t kWord : kDescriptor) {
        putU32(out, kWord);
    }
    for (std::size_t index = kLevels; index-- > 0;) {
        out.resize(offsets[index], std::byte{0});
        out.insert(out.end(), texture.levels[index].bytes.begin(), texture.levels[index].bytes.end());
    }
    return out;
}

result::Result<Texture> decode(std::span<const std::byte> bytes, const TextureLimits& limits) {
    if (bytes.size() < kHeaderBytes ||
        !std::ranges::equal(bytes.first(kIdentifier.size()), kIdentifier, {}, {}, [](std::uint8_t each) {
            return static_cast<std::byte>(each);
        })) {
        return bad("not a KTX 2.0 texture");
    }
    const std::optional<Format> kFormat = formatOfVulkan(getU32(bytes, 12));
    const std::uint32_t kWidth = getU32(bytes, 20);
    const std::uint32_t kHeight = getU32(bytes, 24);
    const std::uint32_t kLevels = getU32(bytes, 40);
    const std::uint32_t kFaces = getU32(bytes, 36);
    // Its type size, no depth, no layers, one face or a cube's six, no
    // supercompression.
    if (!kFormat.has_value() || getU32(bytes, 16) != typeSize(*kFormat) || getU32(bytes, 28) != 0 ||
        getU32(bytes, 32) != 0 || (kFaces != 1 && kFaces != 6) || getU32(bytes, 44) != 0) {
        return bad("a texture of a format or shape this reader does not take");
    }
    if (kWidth == 0 || kHeight == 0 || kLevels == 0 || kLevels > mostLevels(kWidth, kHeight)) {
        return bad("a texture's sides or levels are not a texture's");
    }
    if (kWidth > limits.maximumSide || kHeight > limits.maximumSide) {
        return overLimit("a texture's side is past its limits");
    }
    if (!totalBytes(*kFormat, kWidth, kHeight, kLevels, limits.maximumBytes / kFaces).has_value()) {
        return overLimit("a texture holds more bytes than its limits allow");
    }
    if (bytes.size() < kHeaderBytes + (std::size_t{kLevels} * kLevelIndexBytes)) {
        return bad("a texture shorter than its level index");
    }
    Texture texture{.format = *kFormat, .levels = std::vector<Level>(kLevels), .faces = kFaces};
    for (std::size_t index = 0; index < kLevels; ++index) {
        const std::size_t kAt = kHeaderBytes + (index * kLevelIndexBytes);
        const std::uint64_t kOffset = getU64(bytes, kAt);
        const std::uint64_t kLength = getU64(bytes, kAt + 8);
        Level& level = texture.levels[index];
        level.width = std::max(kWidth >> index, 1U);
        level.height = std::max(kHeight >> index, 1U);
        if (kLength != kFaces * levelBytes(*kFormat, level.width, level.height) || kOffset > bytes.size() ||
            kLength > bytes.size() - kOffset) {
            return bad("a level that is not where its index says, or not its size");
        }
        const auto kLevelBytes = bytes.subspan(static_cast<std::size_t>(kOffset), static_cast<std::size_t>(kLength));
        level.bytes.assign(kLevelBytes.begin(), kLevelBytes.end());
    }
    // Only what this format writes is read: the descriptor, the index, and
    // the padding are the ones encode would write, byte for byte.
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kWritten, encode(texture, limits));
    if (!std::ranges::equal(kWritten, bytes)) {
        return bad("a texture that is not exactly as this format writes it");
    }
    return texture;
}

} // namespace rawframe::texture
