// Environments (D321): a Radiance picture decodes alike from flat and
// run-length scanlines, and broken or other pictures are refused; its cube
// looks each way the picture does, holds a uniform sky at every level, and
// its rougher levels spread the light without adding or losing any.

#include "rawframe/test/test.h"
#include "rawframe/texture/errors.h"
#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::texture_import;

namespace {

using Rgbe = std::array<std::uint8_t, 4>;

bool refusedWith(const auto& outcome, texture::TextureError error) {
    return !outcome.has_value() && outcome.error().code() == texture::code(error);
}

void put(std::vector<std::byte>& bytes, std::string_view text) {
    for (const char kCharacter : text) {
        bytes.push_back(static_cast<std::byte>(kCharacter));
    }
}

/// A Radiance picture of these texels, rows top first; `runs` writes each
/// scanline run-length encoded, a run for each repeat and a literal of one
/// for each change.
std::vector<std::byte> radiance(std::uint32_t width,
                                std::uint32_t height,
                                const std::vector<Rgbe>& texels,
                                bool runs,
                                std::string_view header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n") {
    std::vector<std::byte> bytes;
    put(bytes, header);
    put(bytes, "\n-Y " + std::to_string(height) + " +X " + std::to_string(width) + "\n");
    for (std::uint32_t row = 0; row < height; ++row) {
        const Rgbe* const kRow = &texels[std::size_t{row} * width];
        if (!runs) {
            for (std::uint32_t x = 0; x < width; ++x) {
                for (const std::uint8_t kByte : kRow[x]) {
                    bytes.push_back(std::byte{kByte});
                }
            }
            continue;
        }
        for (const std::uint8_t kByte : {2U, 2U, width >> 8U, width & 0xFFU}) {
            bytes.push_back(static_cast<std::byte>(kByte));
        }
        for (std::size_t channel = 0; channel < 4; ++channel) {
            std::uint32_t x = 0;
            while (x < width) {
                std::uint32_t length = 1;
                while (x + length < width && length < 127 && kRow[x + length][channel] == kRow[x][channel]) {
                    ++length;
                }
                bytes.push_back(static_cast<std::byte>(length > 1 ? 128 + length : 1));
                bytes.push_back(std::byte{kRow[x][channel]});
                x += length;
            }
        }
    }
    return bytes;
}

/// A texel of a cooked cube, as floats.
std::array<float, 4>
lightAt(const texture::Texture& cube, std::size_t level, std::uint32_t face, std::uint32_t x, std::uint32_t y) {
    const texture::Level& kLevel = cube.levels[level];
    const std::size_t kAt = ((std::size_t{face} * kLevel.height + y) * kLevel.width + x) * 8;
    std::array<float, 4> light{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        const auto kLow = std::to_integer<std::uint16_t>(kLevel.bytes[kAt + (channel * 2)]);
        const auto kHigh = std::to_integer<std::uint16_t>(kLevel.bytes[kAt + (channel * 2) + 1]);
        light[channel] = texture::floatOf(static_cast<std::uint16_t>(kLow | (kHigh << 8U)));
    }
    return light;
}

/// A picture of all directions, each texel's light from its place.
LightImage picture(std::uint32_t width, std::uint32_t height, auto lightOf) {
    LightImage image{.width = width, .height = height};
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::array<float, 3> kLight =
                lightOf((x + 0.5) / static_cast<double>(width), (y + 0.5) / static_cast<double>(height));
            image.rgb.insert(image.rgb.end(), kLight.begin(), kLight.end());
        }
    }
    return image;
}

/// The mean of a level's red over all directions, each texel weighed by
/// the solid angle it covers.
double meanRed(const texture::Texture& cube, std::size_t level) {
    const std::uint32_t kSide = cube.levels[level].width;
    double sum = 0;
    double angle = 0;
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                const double kS = (2.0 * (x + 0.5) / kSide) - 1;
                const double kT = (2.0 * (y + 0.5) / kSide) - 1;
                const double kAngle = 1 / std::pow(1 + (kS * kS) + (kT * kT), 1.5);
                sum += lightAt(cube, level, face, x, y)[0] * kAngle;
                angle += kAngle;
            }
        }
    }
    return sum / angle;
}

} // namespace

RAWFRAME_TEST(RadiancePicturesDecodeAlikeFromFlatAndRunLengthScanlines) {
    // Mantissas are fractions of 256 scaled by two to the exponent less 128.
    std::vector<Rgbe> texels;
    for (std::uint32_t index = 0; index < 20; ++index) {
        texels.push_back(
            index < 6 ? Rgbe{128, 64, 32, 129}
                      : (index % 3 == 0 ? Rgbe{0, 0, 0, 0} : Rgbe{200, 100, static_cast<std::uint8_t>(index), 140}));
    }
    const auto kFlat = decodeRadiance(radiance(10, 2, texels, false));
    const auto kRuns = decodeRadiance(radiance(10, 2, texels, true));
    RAWFRAME_EXPECT(kFlat.has_value() && kRuns.has_value());
    if (!kFlat.has_value() || !kRuns.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kFlat->width == 10 && kFlat->height == 2 && kFlat->rgb.size() == 60);
    RAWFRAME_EXPECT(kFlat->rgb == kRuns->rgb);
    RAWFRAME_EXPECT(kFlat->rgb[0] == 1.0F && kFlat->rgb[1] == 0.5F && kFlat->rgb[2] == 0.25F);
    RAWFRAME_EXPECT(kFlat->rgb[9 * 3] == 0.0F && kFlat->rgb[(7 * 3) + 0] == 3200.0F &&
                    kFlat->rgb[(7 * 3) + 2] == 112.0F);
    // An old header's magic, and no format line, are taken too.
    const auto kOld = decodeRadiance(radiance(10, 2, texels, true, "#?RGBE\n"));
    RAWFRAME_EXPECT(kOld.has_value() && kOld->rgb == kFlat->rgb);
    // Narrow pictures are always flat.
    const auto kNarrow = decodeRadiance(radiance(2, 2, {texels.begin(), texels.begin() + 4}, false));
    RAWFRAME_EXPECT(kNarrow.has_value() && kNarrow->rgb[0] == 1.0F);
}

RAWFRAME_TEST(OtherAndBrokenRadiancePicturesAreRefused) {
    const std::vector<Rgbe> kTexels(16, Rgbe{128, 128, 128, 128});
    const std::vector<std::byte> kGood = radiance(8, 2, kTexels, true);
    RAWFRAME_EXPECT(decodeRadiance(kGood).has_value());
    using texture::TextureError;
    RAWFRAME_EXPECT(
        refusedWith(decodeRadiance(radiance(8, 2, kTexels, true, "#?RADIANCX\n")), TextureError::BadSource));
    RAWFRAME_EXPECT(refusedWith(decodeRadiance(radiance(8, 2, kTexels, true, "#?RADIANCE\nFORMAT=32-bit_rle_xyze\n")),
                                TextureError::BadSource));
    // Another orientation.
    std::string flipped{reinterpret_cast<const char*>(kGood.data()), kGood.size()};
    flipped.replace(flipped.find("-Y"), 2, "+Y");
    const auto* const kFlipped = reinterpret_cast<const std::byte*>(flipped.data());
    RAWFRAME_EXPECT(refusedWith(decodeRadiance({kFlipped, flipped.size()}), TextureError::BadSource));
    // Truncated, inside the last scanline.
    RAWFRAME_EXPECT(refusedWith(decodeRadiance({kGood.data(), kGood.size() - 3}), TextureError::BadSource));
    // A run past its scanline: the first channel's run of nine in eight.
    std::vector<std::byte> long_ = kGood;
    const std::size_t kFirstRun = kGood.size() - (2 * (4 + (4 * 2)));
    RAWFRAME_EXPECT(long_[kFirstRun] == std::byte{2} && long_[kFirstRun + 4] == std::byte{128 + 8});
    long_[kFirstRun + 4] = std::byte{128 + 9};
    RAWFRAME_EXPECT(refusedWith(decodeRadiance(long_), TextureError::BadSource));
    // A run of nought.
    long_[kFirstRun + 4] = std::byte{128};
    RAWFRAME_EXPECT(refusedWith(decodeRadiance(long_), TextureError::BadSource));
    // The old run-length form.
    RAWFRAME_EXPECT(refusedWith(decodeRadiance(radiance(2, 1, {Rgbe{128, 0, 0, 128}, Rgbe{1, 1, 1, 3}}, false)),
                                TextureError::BadSource));
    // Sides past the limit, and sides the bytes cannot fill, are refused
    // before the texels are allocated.
    RAWFRAME_EXPECT(refusedWith(decodeRadiance(kGood, {.maximumSide = 4}), TextureError::OverLimit));
    std::string tall{reinterpret_cast<const char*>(kGood.data()), kGood.size()};
    tall.replace(tall.find("-Y 2"), 4, "-Y 8000");
    const auto* const kTall = reinterpret_cast<const std::byte*>(tall.data());
    RAWFRAME_EXPECT(refusedWith(decodeRadiance({kTall, tall.size()}), TextureError::BadSource));
}

RAWFRAME_TEST(AUniformSkyCooksToTheSameLightAtEveryLevel) {
    const LightImage kSky = picture(32, 16, [](double, double) {
        return std::array<float, 3>{0.5F, 1.0F, 2.0F};
    });
    const auto kCube = cookEnvironment(kSky, {.side = 16, .levels = 5, .samples = 32});
    RAWFRAME_EXPECT(kCube.has_value());
    if (!kCube.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kCube->format == texture::Format::Rgba16Float && kCube->faces == 6 && kCube->levels.size() == 5);
    bool uniform = true;
    for (std::size_t level = 0; level < kCube->levels.size(); ++level) {
        const std::uint32_t kSide = kCube->levels[level].width;
        uniform = uniform && kSide == (16U >> level);
        for (std::uint32_t face = 0; face < 6; ++face) {
            for (std::uint32_t y = 0; y < kSide; ++y) {
                for (std::uint32_t x = 0; x < kSide; ++x) {
                    const std::array<float, 4> kLight = lightAt(*kCube, level, face, x, y);
                    uniform = uniform && std::abs(kLight[0] - 0.5F) < 0.002F && std::abs(kLight[1] - 1.0F) < 0.002F &&
                              std::abs(kLight[2] - 2.0F) < 0.004F && kLight[3] == 1.0F;
                }
            }
        }
    }
    RAWFRAME_EXPECT(uniform);
    // The encoded cube is a texture its reader takes back.
    const auto kBytes = texture::encode(*kCube);
    RAWFRAME_EXPECT(kBytes.has_value() && texture::decode(*kBytes) == *kCube);
}

RAWFRAME_TEST(EachDirectionSamplesTheCubeAsItWouldThePicture) {
    // Red where the picture looks along +X (three quarters across), green
    // along -Z (its middle), blue straight up (its top rows).
    const LightImage kMarks = picture(64, 32, [](double u, double v) {
        std::array<float, 3> light{};
        if (v < 0.1) {
            light[2] = 1;
        } else if (std::abs(v - 0.5) < 0.1 && std::abs(u - 0.75) < 0.05) {
            light[0] = 1;
        } else if (std::abs(v - 0.5) < 0.1 && std::abs(u - 0.5) < 0.05) {
            light[1] = 1;
        }
        return light;
    });
    const auto kCube = cookEnvironment(kMarks, {.side = 8, .levels = 1});
    RAWFRAME_EXPECT(kCube.has_value());
    if (!kCube.has_value()) {
        return;
    }
    // Each face's middle four texels, +X, -X, +Y, -Y, +Z, -Z.
    const auto kMiddle = [&](std::uint32_t face) {
        std::array<float, 3> sum{};
        for (std::uint32_t y = 3; y < 5; ++y) {
            for (std::uint32_t x = 3; x < 5; ++x) {
                const std::array<float, 4> kLight = lightAt(*kCube, 0, face, x, y);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    sum[channel] += kLight[channel] / 4;
                }
            }
        }
        return sum;
    };
    RAWFRAME_EXPECT(kMiddle(0)[0] > 0.9F && kMiddle(0)[1] == 0 && kMiddle(0)[2] == 0);
    RAWFRAME_EXPECT(kMiddle(5)[1] > 0.9F && kMiddle(5)[0] == 0 && kMiddle(5)[2] == 0);
    RAWFRAME_EXPECT(kMiddle(2)[2] > 0.9F && kMiddle(2)[0] == 0 && kMiddle(2)[1] == 0);
    for (const std::uint32_t kFace : {1U, 3U, 4U}) {
        RAWFRAME_EXPECT(kMiddle(kFace) == (std::array<float, 3>{}));
    }
}

RAWFRAME_TEST(RougherLevelsSpreadTheLightWithoutAddingOrLosingAny) {
    // Light above the horizon, none below it.
    const LightImage kHalf = picture(128, 64, [](double, double v) {
        return std::array<float, 3>{v < 0.5 ? 1.0F : 0.0F, 0, 0};
    });
    const EnvironmentSettings kSettings{.side = 32, .levels = 6, .samples = 64};
    const auto kCube = cookEnvironment(kHalf, kSettings);
    RAWFRAME_EXPECT(kCube.has_value());
    if (!kCube.has_value()) {
        return;
    }
    // Every level holds the same light in all, half of all directions'.
    for (std::size_t level = 0; level < kCube->levels.size(); ++level) {
        RAWFRAME_EXPECT(std::abs(meanRed(*kCube, level) - 0.5) < 0.02);
    }
    // Just below the horizon on +X: dark in a mirror, lit on a rough
    // surface, more so the rougher.
    const auto kBelow = [&](std::size_t level) {
        const std::uint32_t kSide = kCube->levels[level].width;
        return lightAt(*kCube, level, 0, kSide / 2, (kSide / 2) + (kSide / 8))[0];
    };
    RAWFRAME_EXPECT(kBelow(0) == 0.0F && kBelow(2) > 0.05F && kBelow(4) > kBelow(2));
    // Straight down sees almost no light at any roughness, straight up
    // almost all: the few samples of the roughest level read the chain's
    // smallest levels, whose texels average across the horizon.
    RAWFRAME_EXPECT(lightAt(*kCube, 5, 3, 0, 0)[0] < 0.1F && lightAt(*kCube, 5, 2, 0, 0)[0] > 0.9F);
    RAWFRAME_EXPECT(lightAt(*kCube, 2, 3, 4, 4)[0] < 0.02F && lightAt(*kCube, 2, 2, 4, 4)[0] > 0.98F);
    // The same picture cooks to the same bytes.
    const auto kAgain = cookEnvironment(kHalf, kSettings);
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kCube);
}

RAWFRAME_TEST(EnvironmentSettingsOutsideTheirRangesAreRefused) {
    const LightImage kSky = picture(8, 4, [](double, double) {
        return std::array<float, 3>{1, 1, 1};
    });
    using texture::TextureError;
    RAWFRAME_EXPECT(refusedWith(cookEnvironment(kSky, {.side = 12}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(cookEnvironment(kSky, {.side = 2, .levels = 1}), TextureError::BadTexture));
    RAWFRAME_EXPECT(refusedWith(cookEnvironment(kSky, {.side = 16, .levels = 6}), TextureError::BadTexture));
    RAWFRAME_EXPECT(cookEnvironment(kSky, {.side = 16, .levels = 5, .samples = 4}).has_value());
    RAWFRAME_EXPECT(
        refusedWith(cookEnvironment(kSky, {.side = 16, .levels = 5, .samples = 0}), TextureError::BadTexture));
    LightImage shortSky = kSky;
    shortSky.rgb.pop_back();
    RAWFRAME_EXPECT(refusedWith(cookEnvironment(shortSky, {.side = 16, .levels = 5}), TextureError::BadTexture));
}
