#include "rawframe/texture_import/import.h"

#include "rawframe/texture/errors.h"

#include <algorithm>
#include <array>
#include <bc7decomp.h>
#include <bc7enc.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <wuffs-v0.4.c>

namespace rawframe::texture_import {

namespace {

using texture::Format;

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, texture::kTextureDomain, code(texture::TextureError::BadSource), why);
}

std::unexpected<result::Error> bad(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, texture::kTextureDomain, code(texture::TextureError::BadTexture), why);
}

/// Every source decodes to straight RGBA, a byte a channel.
class Straight final : public wuffs_aux::DecodeImageCallbacks {
    wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config& /*config*/) override {
        return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL);
    }
};

/// Each sRGB byte's linear value, from the standard's formula.
const std::array<float, 256>& linearTable() {
    static const std::array<float, 256> kTable = [] {
        std::array<float, 256> table{};
        for (std::size_t index = 0; index < table.size(); ++index) {
            const double kValue = static_cast<double>(index) / 255.0;
            table[index] =
                static_cast<float>(kValue <= 0.04045 ? kValue / 12.92 : std::pow((kValue + 0.055) / 1.055, 2.4));
        }
        return table;
    }();
    return kTable;
}

std::byte byteOf(double value) noexcept {
    return static_cast<std::byte>(static_cast<int>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5));
}

/// A linear value as an sRGB byte.
std::byte srgbByte(double linear) noexcept {
    const double kClamped = std::clamp(linear, 0.0, 1.0);
    return byteOf(kClamped <= 0.0031308 ? kClamped * 12.92 : (1.055 * std::pow(kClamped, 1.0 / 2.4)) - 0.055);
}

/// The image halved: each texel the alpha-weighted average of the up to
/// four it covers (an odd last row or column is covered once more), color
/// in linear light when it is sRGB.
Image halve(const Image& image, bool srgb) {
    Image half{.width = std::max(image.width / 2, 1U), .height = std::max(image.height / 2, 1U)};
    half.rgba.resize(std::size_t{half.width} * half.height * 4);
    const std::array<float, 256>& kLinear = linearTable();
    const auto kChannel = [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return std::to_integer<std::uint32_t>(image.rgba[((std::size_t{y} * image.width + x) * 4) + channel]);
    };
    for (std::uint32_t y = 0; y < half.height; ++y) {
        for (std::uint32_t x = 0; x < half.width; ++x) {
            const std::array<std::uint32_t, 2> kXs = {std::min(x * 2, image.width - 1),
                                                      std::min((x * 2) + 1, image.width - 1)};
            const std::array<std::uint32_t, 2> kYs = {std::min(y * 2, image.height - 1),
                                                      std::min((y * 2) + 1, image.height - 1)};
            std::array<double, 3> weighted{};
            std::array<double, 3> plain{};
            double alpha = 0;
            for (const std::uint32_t kY : kYs) {
                for (const std::uint32_t kX : kXs) {
                    const double kAlpha = kChannel(kX, kY, 3) / 255.0;
                    alpha += kAlpha;
                    for (std::uint32_t channel = 0; channel < 3; ++channel) {
                        const std::uint32_t kByte = kChannel(kX, kY, channel);
                        const double kValue = srgb ? kLinear[kByte] : kByte / 255.0;
                        weighted[channel] += kValue * kAlpha;
                        plain[channel] += kValue;
                    }
                }
            }
            std::byte* const kOut = &half.rgba[(std::size_t{y} * half.width + x) * 4];
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                // Fully transparent texels keep their plain average.
                const double kValue = alpha > 0 ? weighted[channel] / alpha : plain[channel] / 4.0;
                kOut[channel] = srgb ? srgbByte(kValue) : byteOf(kValue);
            }
            kOut[3] = byteOf(alpha / 4.0);
        }
    }
    return half;
}

/// A level of the image, as it is or in BC7 blocks; a partial block at an
/// edge repeats the edge's texels.
texture::Level levelOf(const Image& image, Format format) {
    texture::Level level{.width = image.width, .height = image.height};
    if (format == Format::Rgba8 || format == Format::Rgba8Srgb) {
        level.bytes = image.rgba;
        return level;
    }
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        bc7enc_compress_block_init();
    });
    bc7enc_compress_block_params params{};
    bc7enc_compress_block_params_init(&params);
    if (format == Format::Bc7) {
        bc7enc_compress_block_params_init_linear_weights(&params);
    }
    // Alpha weighs as much as the heaviest color channel: a sprite's edge is
    // its alpha, which the perceptual weights would make the lightest.
    params.m_weights[3] = std::max({params.m_weights[0], params.m_weights[1], params.m_weights[2]});
    level.bytes.resize(texture::levelBytes(format, image.width, image.height));
    const std::uint32_t kBlocksWide = (image.width + 3) / 4;
    const std::uint32_t kBlocksHigh = (image.height + 3) / 4;
    std::array<std::byte, 16 * 4> texels{};
    for (std::uint32_t by = 0; by < kBlocksHigh; ++by) {
        for (std::uint32_t bx = 0; bx < kBlocksWide; ++bx) {
            for (std::uint32_t row = 0; row < 4; ++row) {
                for (std::uint32_t column = 0; column < 4; ++column) {
                    const std::uint32_t kX = std::min((bx * 4) + column, image.width - 1);
                    const std::uint32_t kY = std::min((by * 4) + row, image.height - 1);
                    std::memcpy(
                        &texels[((row * 4) + column) * 4], &image.rgba[(std::size_t{kY} * image.width + kX) * 4], 4);
                }
            }
            static_cast<void>(bc7enc_compress_block(
                &level.bytes[((std::size_t{by} * kBlocksWide) + bx) * 16], texels.data(), &params));
        }
    }
    return level;
}

} // namespace

result::Result<Image> decodeImage(std::span<const std::byte> source, const DecodeLimits& limits) {
    Straight callbacks;
    wuffs_aux::sync_io::MemoryInput input(reinterpret_cast<const std::uint8_t*>(source.data()), source.size());
    wuffs_aux::DecodeImageResult decoded =
        wuffs_aux::DecodeImage(callbacks,
                               input,
                               wuffs_aux::DecodeImageArgQuirks::DefaultValue(),
                               wuffs_aux::DecodeImageArgFlags::DefaultValue(),
                               wuffs_aux::DecodeImageArgPixelBlend::DefaultValue(),
                               wuffs_aux::DecodeImageArgBackgroundColor::DefaultValue(),
                               wuffs_aux::DecodeImageArgMaxInclDimension(limits.maximumSide));
    if (decoded.error_message == wuffs_aux::DecodeImage_MaxInclDimensionExceeded) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            texture::kTextureDomain,
                            code(texture::TextureError::OverLimit),
                            "a source image's side is past the limit");
    }
    // A partial image (a truncated file) is refused like a broken one.
    if (!decoded.error_message.empty() || !decoded.pixbuf.pixcfg.is_valid()) {
        return std::unexpected<result::Error>{
            badSource("a source image that is not PNG, JPEG, BMP, or TGA, or is broken")
                .error()
                .withContext("reason", decoded.error_message)};
    }
    Image image{.width = decoded.pixbuf.pixcfg.width(), .height = decoded.pixbuf.pixcfg.height()};
    if (image.width == 0 || image.height == 0) {
        return badSource("a source image with no texels");
    }
    const wuffs_base__table_u8 kPlane = decoded.pixbuf.plane(0);
    const std::size_t kRowBytes = std::size_t{image.width} * 4;
    image.rgba.resize(kRowBytes * image.height);
    for (std::uint32_t row = 0; row < image.height; ++row) {
        std::memcpy(&image.rgba[row * kRowBytes], kPlane.ptr + (row * kPlane.stride), kRowBytes);
    }
    return image;
}

result::Result<texture::Texture> cookTexture(const Image& image, const CookSettings& settings) {
    if (image.width == 0 || image.height == 0 || image.rgba.size() != std::size_t{image.width} * image.height * 4) {
        return bad("an image's bytes do not fill its sides");
    }
    const Format kFormat = settings.exact ? (settings.srgb ? Format::Rgba8Srgb : Format::Rgba8)
                                          : (settings.srgb ? Format::Bc7Srgb : Format::Bc7);
    texture::Texture cooked{.format = kFormat};
    cooked.levels.push_back(levelOf(image, kFormat));
    Image current;
    const Image* above = &image;
    while (settings.levels && (above->width > 1 || above->height > 1)) {
        current = halve(*above, settings.srgb);
        cooked.levels.push_back(levelOf(current, kFormat));
        above = &current;
    }
    RAWFRAME_TRY(texture::validate(cooked));
    return cooked;
}

result::Result<Image> texelsOf(const texture::Texture& cooked, std::size_t level) {
    if (level >= cooked.levels.size()) {
        return bad("no such level");
    }
    const texture::Level& kLevel = cooked.levels[level];
    Image image{.width = kLevel.width, .height = kLevel.height};
    if (cooked.format == Format::Rgba8 || cooked.format == Format::Rgba8Srgb) {
        image.rgba = kLevel.bytes;
        return image;
    }
    image.rgba.resize(std::size_t{image.width} * image.height * 4);
    const std::uint32_t kBlocksWide = (image.width + 3) / 4;
    std::array<bc7decomp::color_rgba, 16> block{};
    for (std::uint32_t by = 0; by < (image.height + 3) / 4; ++by) {
        for (std::uint32_t bx = 0; bx < kBlocksWide; ++bx) {
            if (!bc7decomp::unpack_bc7(&kLevel.bytes[((std::size_t{by} * kBlocksWide) + bx) * 16], block.data())) {
                return bad("a BC7 block that does not decode");
            }
            for (std::uint32_t row = 0; row < 4 && (by * 4) + row < image.height; ++row) {
                for (std::uint32_t column = 0; column < 4 && (bx * 4) + column < image.width; ++column) {
                    std::memcpy(&image.rgba[((std::size_t{(by * 4) + row} * image.width) + (bx * 4) + column) * 4],
                                &block[(row * 4) + column],
                                4);
                }
            }
        }
    }
    return image;
}

} // namespace rawframe::texture_import
