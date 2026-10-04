#pragma once

// The cooked texture (ADR-0058's texture family, D253): an image's levels
// in a form the GPU takes as it is, written by import tooling and read by
// the runtime, which decodes no source format. Rows run top first
// (ADR-0046); color is sRGB or linear as ADR-0047 has it, stated by the
// format, never guessed.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::texture {

/// The resource type of every cooked texture.
inline constexpr base::Bits128 kTextureType = base::parseBits128Hex("0fcaee71b10db20952a106ff4b94b46c").value;
inline constexpr std::string_view kTextureRepresentation = "rawframe.texture";

/// How a level's texels are stored. The block-compressed BC7 is the desktop
/// form ADR-0058 names; RGBA8 is kept uncompressed, for what must stay
/// exact (pixel art, interface images); RGBA16F holds light past one, in
/// linear Rec. 709, for environments (D320); R8 holds one linear channel,
/// a glyph atlas's coverage (D398).
enum class Format : std::uint8_t {
    Rgba8,
    Rgba8Srgb,
    Bc7,
    Bc7Srgb,
    Rgba16Float,
    R8
};

/// Whether a format's color channels are sRGB-encoded (alpha never is).
[[nodiscard]] bool isSrgb(Format format) noexcept;
/// The bytes a face of a level of this size takes: one a texel for R8, four
/// for RGBA8, eight for RGBA16F, sixteen a 4x4 block (partial blocks whole) for BC7.
[[nodiscard]] std::size_t levelBytes(Format format, std::uint32_t width, std::uint32_t height) noexcept;

/// A float as the IEEE half an RGBA16F channel holds, rounded to nearest,
/// ties to even; past the half's range it is infinite.
[[nodiscard]] std::uint16_t halfOf(float value) noexcept;
/// An IEEE half as a float, exactly.
[[nodiscard]] float floatOf(std::uint16_t half) noexcept;

/// A level: its sides, and each face's texels in turn.
struct Level {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> bytes;
    friend bool operator==(const Level&, const Level&) noexcept = default;
};

/// Level nought is the image; each next one halves both sides, rounding
/// down and never below one, and the last may be any of them. A cube
/// (D320) has six square faces, +X, -X, +Y, -Y, +Z, -Z, as Vulkan's layers
/// are; anything else has one. A volume (D344) has `depth` slices of its
/// one level, front first, each a face's texels: one face, one level, and
/// an uncompressed format; anything else has a depth of one.
struct Texture {
    Format format = Format::Rgba8;
    std::vector<Level> levels;
    std::uint32_t faces = 1;
    std::uint32_t depth = 1;
    friend bool operator==(const Texture&, const Texture&) noexcept = default;
};

/// The explicit limit profile of one texture.
struct TextureLimits {
    std::uint32_t maximumSide = 16384;
    std::size_t maximumBytes = std::size_t{1} << 30U;
};

/// Refuses (`BadTexture`) a texture without levels, with a side of nought,
/// with levels that do not halve, with bytes that do not fill a level's
/// faces (or a volume's slices) exactly, with faces neither one nor six of
/// square sides, or a volume not as above; and (`OverLimit`) one past the
/// limits, its depth a side.
[[nodiscard]] result::Status validate(const Texture& texture, const TextureLimits& limits = {});

/// The cooked form is KTX 2.0 without supercompression: the Vulkan format,
/// the basic data format descriptor Khronos defines for it, its faces, no
/// key and value data, and the levels smallest first, each aligned to its
/// block.
/// Refuses what `validate` refuses.
[[nodiscard]] result::Result<std::vector<std::byte>> encode(const Texture& texture, const TextureLimits& limits = {});

/// Refuses (`BadTexture`) anything but a texture exactly as `encode` writes
/// one of the formats above, and (`OverLimit`) sides or bytes past the
/// limits, before anything is allocated.
[[nodiscard]] result::Result<Texture> decode(std::span<const std::byte> bytes, const TextureLimits& limits = {});

} // namespace rawframe::texture
