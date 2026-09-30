#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::texture_import {

namespace {

/// A texel's light as RGBE: a shared exponent, each channel a fraction of
/// 256 of two to it.
std::array<std::uint8_t, 4> rgbeOf(const float* light) noexcept {
    std::array<float, 3> channels{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float kValue = light[channel];
        channels[channel] = std::isnan(kValue) || kValue < 0 ? 0.0F : std::min(kValue, 1.7e38F);
    }
    const float kMost = std::max({channels[0], channels[1], channels[2]});
    int exponent = 0;
    static_cast<void>(std::frexp(kMost, &exponent));
    if (kMost == 0) {
        return {0, 0, 0, 0};
    }
    // Fainter than the least exponent RGBE holds: that exponent, its
    // mantissas less than half, and black when they are nought.
    exponent = std::max(exponent, -127);
    // Each channel over two to the exponent, times 256: scaled by a power
    // of two, exactly, and never past 256 however faint the light.
    std::array<std::uint8_t, 4> made{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        made[channel] = static_cast<std::uint8_t>(std::min(std::ldexp(channels[channel], 8 - exponent), 255.0F));
    }
    if (made[0] == 0 && made[1] == 0 && made[2] == 0) {
        return {0, 0, 0, 0};
    }
    made[3] = static_cast<std::uint8_t>(exponent + 128);
    return made;
}

/// One channel of a scanline, run-length encoded: runs of four or more of
/// a byte as a run, the rest as literals, each at most 127 or 128 long.
void encodePlane(const std::vector<std::uint8_t>& plane, std::vector<std::byte>& out) {
    std::size_t at = 0;
    while (at < plane.size()) {
        // The next run of four or more from here, if any before 128 more.
        std::size_t runStart = at;
        std::size_t runLength = 0;
        while (runStart < plane.size()) {
            runLength = 1;
            while (runStart + runLength < plane.size() && runLength < 127 &&
                   plane[runStart + runLength] == plane[runStart]) {
                ++runLength;
            }
            if (runLength >= 4 || runStart - at >= 128) {
                break;
            }
            runStart += runLength;
        }
        const std::size_t kLiterals = std::min<std::size_t>(runStart - at, 128);
        if (kLiterals > 0) {
            out.push_back(static_cast<std::byte>(kLiterals));
            for (std::size_t each = 0; each < kLiterals; ++each) {
                out.push_back(static_cast<std::byte>(plane[at + each]));
            }
            at += kLiterals;
            continue;
        }
        out.push_back(static_cast<std::byte>(128 + runLength));
        out.push_back(static_cast<std::byte>(plane[at]));
        at += runLength;
    }
}

} // namespace

std::vector<std::byte> encodeRadiance(const LightImage& image) {
    const std::string kHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(image.height) + " +X " +
                                std::to_string(image.width) + "\n";
    std::vector<std::byte> out;
    for (const char kByte : kHeader) {
        out.push_back(static_cast<std::byte>(kByte));
    }
    const bool kEncoded = image.width >= 8 && image.width <= 32767;
    std::array<std::vector<std::uint8_t>, 4> planes;
    for (std::uint32_t row = 0; row < image.height; ++row) {
        for (std::vector<std::uint8_t>& plane : planes) {
            plane.clear();
        }
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t kAt = ((std::size_t{row} * image.width) + x) * 3;
            const std::array<std::uint8_t, 4> kTexel =
                kAt + 3 <= image.rgb.size() ? rgbeOf(&image.rgb[kAt]) : std::array<std::uint8_t, 4>{};
            for (std::size_t channel = 0; channel < 4; ++channel) {
                planes[channel].push_back(kTexel[channel]);
            }
        }
        if (!kEncoded) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                for (const std::vector<std::uint8_t>& plane : planes) {
                    out.push_back(static_cast<std::byte>(plane[x]));
                }
            }
            continue;
        }
        out.push_back(std::byte{2});
        out.push_back(std::byte{2});
        out.push_back(static_cast<std::byte>(image.width >> 8U));
        out.push_back(static_cast<std::byte>(image.width & 0xFFU));
        for (const std::vector<std::uint8_t>& plane : planes) {
            encodePlane(plane, out);
        }
    }
    return out;
}

} // namespace rawframe::texture_import
