#include "rawframe/texture/errors.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>

namespace rawframe::texture_import {

namespace {

using Vec = std::array<double, 3>;
using Rgb = std::array<float, 3>;

std::unexpected<result::Error> badSource(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, texture::kTextureDomain, code(texture::TextureError::BadSource), why);
}

std::unexpected<result::Error> bad(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, texture::kTextureDomain, code(texture::TextureError::BadTexture), why);
}

// A header line longer than this is not a Radiance header.
constexpr std::size_t kLineLimit = 4096;
// New run-length scanlines are 8 to 32,767 texels wide.
constexpr std::uint32_t kRunsFrom = 8;
constexpr std::uint32_t kRunsBelow = 32768;
// A run's count byte covers at most 127 texels in two bytes.
constexpr std::uint32_t kLongestRun = 127;

/// Reads a source front to back, never past its end.
class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {
    }

    /// The next line, without its newline, or nothing if none ends within
    /// the limit.
    std::optional<std::string_view> line() noexcept {
        const std::size_t kEnd = std::min(bytes_.size(), at_ + kLineLimit);
        for (std::size_t index = at_; index < kEnd; ++index) {
            if (bytes_[index] == std::byte{'\n'}) {
                const std::string_view kLine{reinterpret_cast<const char*>(bytes_.data() + at_), index - at_};
                at_ = index + 1;
                return kLine;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint8_t> byte() noexcept {
        if (at_ >= bytes_.size()) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[at_++]);
    }

    /// The byte `ahead` places on, without reading it.
    [[nodiscard]] std::optional<std::uint8_t> peek(std::size_t ahead) const noexcept {
        if (at_ + ahead >= bytes_.size()) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[at_ + ahead]);
    }

    [[nodiscard]] std::size_t left() const noexcept {
        return bytes_.size() - at_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t at_ = 0;
};

/// `-Y <height> +X <width>`, the one orientation taken.
std::optional<std::array<std::uint32_t, 2>> resolutionOf(std::string_view line) noexcept {
    const auto kNumber = [&line](std::string_view prefix) -> std::optional<std::uint32_t> {
        if (!line.starts_with(prefix)) {
            return std::nullopt;
        }
        line.remove_prefix(prefix.size());
        std::uint32_t value = 0;
        const auto [kEnd, kError] = std::from_chars(line.data(), line.data() + line.size(), value);
        if (kError != std::errc{} || kEnd == line.data()) {
            return std::nullopt;
        }
        line.remove_prefix(static_cast<std::size_t>(kEnd - line.data()));
        return value;
    };
    const std::optional<std::uint32_t> kHeight = kNumber("-Y ");
    if (!kHeight.has_value()) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> kWidth = kNumber(" +X ");
    if (!kWidth.has_value() || !line.empty()) {
        return std::nullopt;
    }
    return std::array<std::uint32_t, 2>{*kWidth, *kHeight};
}

bool runLength(std::uint32_t width) noexcept {
    return width >= kRunsFrom && width < kRunsBelow;
}

/// The fewest bytes a scanline of this width can take.
std::size_t leastScanline(std::uint32_t width) noexcept {
    if (!runLength(width)) {
        return std::size_t{width} * 4;
    }
    return 4 + (std::size_t{4} * 2 * ((width + kLongestRun - 1) / kLongestRun));
}

/// A scanline's texels, RGBE a byte each, from run-length or flat bytes.
result::Status readScanline(Cursor& cursor, std::uint32_t width, std::span<std::uint8_t> texels) {
    const bool kRuns = runLength(width) && cursor.peek(0) == 2 && cursor.peek(1) == 2 && cursor.peek(2).has_value() &&
                       cursor.peek(3).has_value() &&
                       ((std::uint32_t{*cursor.peek(2)} << 8U) | *cursor.peek(3)) == width;
    if (!kRuns) {
        for (std::uint32_t x = 0; x < width; ++x) {
            for (std::uint32_t channel = 0; channel < 4; ++channel) {
                const std::optional<std::uint8_t> kByte = cursor.byte();
                if (!kByte.has_value()) {
                    return badSource("a Radiance picture ends inside a scanline");
                }
                texels[(x * 4) + channel] = *kByte;
            }
            // A normalized RGBE texel has a mantissa of 128 or more; three
            // ones mark the old run-length form, not taken.
            if (texels[x * 4] == 1 && texels[(x * 4) + 1] == 1 && texels[(x * 4) + 2] == 1) {
                return badSource("an old run-length Radiance scanline");
            }
        }
        return {};
    }
    for (std::uint32_t skip = 0; skip < 4; ++skip) {
        static_cast<void>(cursor.byte());
    }
    // Each channel in turn: a count past 128 repeats the next byte, one up
    // to 128 is that many bytes as they are.
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        std::uint32_t x = 0;
        while (x < width) {
            const std::optional<std::uint8_t> kCount = cursor.byte();
            if (!kCount.has_value()) {
                return badSource("a Radiance picture ends inside a scanline");
            }
            const bool kRun = *kCount > 128;
            const std::uint32_t kLength = kRun ? *kCount - 128U : *kCount;
            if (kLength == 0 || x + kLength > width) {
                return badSource("a Radiance run past its scanline");
            }
            std::optional<std::uint8_t> value;
            for (std::uint32_t index = 0; index < kLength; ++index) {
                if (!kRun || index == 0) {
                    value = cursor.byte();
                }
                if (!value.has_value()) {
                    return badSource("a Radiance picture ends inside a scanline");
                }
                texels[((x + index) * 4) + channel] = *value;
            }
            x += kLength;
        }
    }
    return {};
}

/// The direction through a face's point, `s` rightward and `t` downward
/// from -1 to 1, as Vulkan addresses a cube.
Vec directionOf(std::uint32_t face, double s, double t) noexcept {
    Vec direction{};
    switch (face) {
    case 0:
        direction = {1, -t, -s};
        break;
    case 1:
        direction = {-1, -t, s};
        break;
    case 2:
        direction = {s, 1, t};
        break;
    case 3:
        direction = {s, -1, -t};
        break;
    case 4:
        direction = {s, -t, 1};
        break;
    default:
        direction = {-s, -t, -1};
        break;
    }
    const double kLength = std::hypot(direction[0], direction[1], direction[2]);
    return {direction[0] / kLength, direction[1] / kLength, direction[2] / kLength};
}

/// The face a direction meets and where, from 0 to 1 across and down.
struct FacePoint {
    std::uint32_t face = 0;
    double u = 0;
    double v = 0;
};

FacePoint faceOf(const Vec& direction) noexcept {
    const double kX = std::abs(direction[0]);
    const double kY = std::abs(direction[1]);
    const double kZ = std::abs(direction[2]);
    FacePoint point;
    double major = 0;
    double s = 0;
    double t = 0;
    if (kX >= kY && kX >= kZ) {
        point.face = direction[0] > 0 ? 0 : 1;
        major = kX;
        s = direction[0] > 0 ? -direction[2] : direction[2];
        t = -direction[1];
    } else if (kY >= kZ) {
        point.face = direction[1] > 0 ? 2 : 3;
        major = kY;
        s = direction[0];
        t = direction[1] > 0 ? direction[2] : -direction[2];
    } else {
        point.face = direction[2] > 0 ? 4 : 5;
        major = kZ;
        s = direction[2] > 0 ? direction[0] : -direction[0];
        t = -direction[1];
    }
    point.u = 0.5 * ((s / major) + 1);
    point.v = 0.5 * ((t / major) + 1);
    return point;
}

/// The picture's light in a direction, bilinear, wrapping across and
/// clamped at the poles.
Rgb pictureAt(const LightImage& image, const Vec& direction) noexcept {
    const double kU = 0.5 + (std::atan2(direction[0], -direction[2]) / (2 * std::numbers::pi));
    const double kV = 0.5 - (std::asin(std::clamp(direction[1], -1.0, 1.0)) / std::numbers::pi);
    const double kX = (kU * image.width) - 0.5;
    const double kY = std::clamp((kV * image.height) - 0.5, 0.0, static_cast<double>(image.height - 1));
    const double kLeft = std::floor(kX);
    const double kTop = std::floor(kY);
    const double kAcross = kX - kLeft;
    const double kDown = kY - kTop;
    const auto kWidth = static_cast<std::int64_t>(image.width);
    const auto kColumn = [kWidth](std::int64_t x) {
        return static_cast<std::size_t>(((x % kWidth) + kWidth) % kWidth);
    };
    const std::array<std::size_t, 2> kXs = {kColumn(static_cast<std::int64_t>(kLeft)),
                                            kColumn(static_cast<std::int64_t>(kLeft) + 1)};
    const std::array<std::size_t, 2> kYs = {
        static_cast<std::size_t>(kTop), std::min(static_cast<std::size_t>(kTop) + 1, std::size_t{image.height} - 1)};
    Rgb light{};
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t column = 0; column < 2; ++column) {
            const double kWeight = (row == 0 ? 1 - kDown : kDown) * (column == 0 ? 1 - kAcross : kAcross);
            const float* const kTexel = &image.rgb[((kYs[row] * image.width) + kXs[column]) * 3];
            for (std::size_t channel = 0; channel < 3; ++channel) {
                light[channel] += static_cast<float>(kTexel[channel] * kWeight);
            }
        }
    }
    return light;
}

/// A level of a cube in floats, the faces one after another.
struct CubeLevel {
    std::uint32_t side = 0;
    std::vector<Rgb> texels;

    [[nodiscard]] const Rgb& at(std::uint32_t face, std::uint32_t x, std::uint32_t y) const noexcept {
        return texels[((std::size_t{face} * side + y) * side) + x];
    }
};

/// The level's light in a direction, bilinear within the face it meets.
Rgb levelAt(const CubeLevel& level, const Vec& direction) noexcept {
    const FacePoint kPoint = faceOf(direction);
    const double kLast = level.side - 1;
    const double kX = std::clamp((kPoint.u * level.side) - 0.5, 0.0, kLast);
    const double kY = std::clamp((kPoint.v * level.side) - 0.5, 0.0, kLast);
    const auto kLeft = static_cast<std::uint32_t>(kX);
    const auto kTop = static_cast<std::uint32_t>(kY);
    const std::uint32_t kRight = std::min(kLeft + 1, level.side - 1);
    const std::uint32_t kBottom = std::min(kTop + 1, level.side - 1);
    const double kAcross = kX - kLeft;
    const double kDown = kY - kTop;
    Rgb light{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double kUpper = (level.at(kPoint.face, kLeft, kTop)[channel] * (1 - kAcross)) +
                              (level.at(kPoint.face, kRight, kTop)[channel] * kAcross);
        const double kLower = (level.at(kPoint.face, kLeft, kBottom)[channel] * (1 - kAcross)) +
                              (level.at(kPoint.face, kRight, kBottom)[channel] * kAcross);
        light[channel] = static_cast<float>((kUpper * (1 - kDown)) + (kLower * kDown));
    }
    return light;
}

/// The chain's light in a direction at a fractional level, trilinear.
Rgb chainAt(const std::vector<CubeLevel>& chain, const Vec& direction, double level) noexcept {
    const double kLevel = std::clamp(level, 0.0, static_cast<double>(chain.size() - 1));
    const auto kLower = static_cast<std::size_t>(kLevel);
    const std::size_t kUpper = std::min(kLower + 1, chain.size() - 1);
    const double kBetween = kLevel - static_cast<double>(kLower);
    const Rgb kNear = levelAt(chain[kLower], direction);
    if (kBetween == 0 || kUpper == kLower) {
        return kNear;
    }
    const Rgb kFar = levelAt(chain[kUpper], direction);
    Rgb light{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        light[channel] = static_cast<float>((kNear[channel] * (1 - kBetween)) + (kFar[channel] * kBetween));
    }
    return light;
}

/// Each face halved, a texel the average of the four it covers.
CubeLevel halve(const CubeLevel& level) {
    CubeLevel half{.side = level.side / 2};
    half.texels.resize(std::size_t{6} * half.side * half.side);
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < half.side; ++y) {
            for (std::uint32_t x = 0; x < half.side; ++x) {
                Rgb& out = half.texels[((std::size_t{face} * half.side + y) * half.side) + x];
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    out[channel] =
                        0.25F * (level.at(face, 2 * x, 2 * y)[channel] + level.at(face, (2 * x) + 1, 2 * y)[channel] +
                                 level.at(face, 2 * x, (2 * y) + 1)[channel] +
                                 level.at(face, (2 * x) + 1, (2 * y) + 1)[channel]);
                }
            }
        }
    }
    return half;
}

/// The i-th of n points of the Hammersley set on the unit square.
std::array<double, 2> hammersley(std::uint32_t index, std::uint32_t count) noexcept {
    std::uint32_t bits = index;
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return {static_cast<double>(index) / count, static_cast<double>(bits) * 0x1p-32};
}

double dot(const Vec& a, const Vec& b) noexcept {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

Vec cross(const Vec& a, const Vec& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

Vec normalized(const Vec& vector) noexcept {
    const double kLength = std::sqrt(dot(vector, vector));
    return {vector[0] / kLength, vector[1] / kLength, vector[2] / kLength};
}

/// A level at a roughness: for each texel's direction, as the normal and
/// the view both, the light GGX reflects, weighted by its cosine. Each
/// sample reads the chain at the level whose texels cover the solid angle
/// it stands for (Colbert and Krivanek, GPU Gems 3, 20), so few samples
/// take no noise.
CubeLevel prefilter(const std::vector<CubeLevel>& chain, std::uint32_t side, double roughness, std::uint32_t samples) {
    const double kAlpha = roughness * roughness;
    const double kAlpha2 = kAlpha * kAlpha;
    const double kTexelAngle = 4 * std::numbers::pi / (6.0 * chain.front().side * chain.front().side);
    CubeLevel level{.side = side};
    level.texels.resize(std::size_t{6} * side * side);
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const Vec kNormal = directionOf(face, (2.0 * (x + 0.5) / side) - 1, (2.0 * (y + 0.5) / side) - 1);
                const Vec kUp = std::abs(kNormal[2]) < 0.999 ? Vec{0, 0, 1} : Vec{1, 0, 0};
                const Vec kTangent = normalized(cross(kUp, kNormal));
                const Vec kBitangent = cross(kNormal, kTangent);
                std::array<double, 3> sum{};
                double weight = 0;
                for (std::uint32_t sample = 0; sample < samples; ++sample) {
                    const std::array<double, 2> kPoint = hammersley(sample, samples);
                    const double kPhi = 2 * std::numbers::pi * kPoint[0];
                    const double kCos = std::sqrt((1 - kPoint[1]) / (1 + ((kAlpha2 - 1) * kPoint[1])));
                    const double kSin = std::sqrt(std::max(0.0, 1 - (kCos * kCos)));
                    Vec half{};
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        half[axis] = (kTangent[axis] * kSin * std::cos(kPhi)) +
                                     (kBitangent[axis] * kSin * std::sin(kPhi)) + (kNormal[axis] * kCos);
                    }
                    const double kHalfCos = dot(kNormal, half);
                    Vec light{};
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        light[axis] = (2 * kHalfCos * half[axis]) - kNormal[axis];
                    }
                    const double kLightCos = dot(kNormal, light);
                    if (kLightCos <= 0) {
                        continue;
                    }
                    // With the view along the normal, the half vector's pdf
                    // over the light's directions is D / 4.
                    const double kDenominator = (kHalfCos * kHalfCos * (kAlpha2 - 1)) + 1;
                    const double kDistribution = kAlpha2 / (std::numbers::pi * kDenominator * kDenominator);
                    const double kSampleAngle = 4 / (samples * kDistribution);
                    const double kLevel = (0.5 * std::log2(kSampleAngle / kTexelAngle)) + 1;
                    const Rgb kLight = chainAt(chain, normalized(light), kLevel);
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        sum[channel] += kLight[channel] * kLightCos;
                    }
                    weight += kLightCos;
                }
                Rgb& out = level.texels[((std::size_t{face} * side + y) * side) + x];
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    out[channel] = weight > 0 ? static_cast<float>(sum[channel] / weight) : 0.0F;
                }
            }
        }
    }
    return level;
}

/// A level's six faces as RGBA16F, alpha one.
texture::Level bytesOf(const CubeLevel& level) {
    texture::Level out{.width = level.side, .height = level.side};
    out.bytes.reserve(level.texels.size() * 8);
    const auto kPut = [&out](float value) {
        const std::uint16_t kHalf = texture::halfOf(value);
        out.bytes.push_back(static_cast<std::byte>(kHalf & 0xFFU));
        out.bytes.push_back(static_cast<std::byte>(kHalf >> 8U));
    };
    for (const Rgb& kTexel : level.texels) {
        // Light past the half's range is held at its largest.
        for (const float kChannel : kTexel) {
            kPut(std::min(kChannel, 65504.0F));
        }
        kPut(1.0F);
    }
    return out;
}

} // namespace

result::Result<LightImage> decodeRadiance(std::span<const std::byte> source, const DecodeLimits& limits) {
    Cursor cursor{source};
    const std::optional<std::string_view> kMagic = cursor.line();
    if (!kMagic.has_value() || (*kMagic != "#?RADIANCE" && *kMagic != "#?RGBE")) {
        return badSource("not a Radiance picture");
    }
    for (;;) {
        const std::optional<std::string_view> kLine = cursor.line();
        if (!kLine.has_value()) {
            return badSource("a Radiance header without its end");
        }
        if (kLine->empty()) {
            break;
        }
        if (kLine->starts_with("FORMAT=") && *kLine != "FORMAT=32-bit_rle_rgbe") {
            return badSource("a Radiance picture not in RGBE");
        }
    }
    const std::optional<std::string_view> kResolution = cursor.line();
    const std::optional<std::array<std::uint32_t, 2>> kSides =
        kResolution.has_value() ? resolutionOf(*kResolution) : std::nullopt;
    if (!kSides.has_value()) {
        return badSource("a Radiance picture not top first and left to right, or without its sides");
    }
    LightImage image{.width = (*kSides)[0], .height = (*kSides)[1]};
    if (image.width == 0 || image.height == 0) {
        return badSource("a source image with no texels");
    }
    if (image.width > limits.maximumSide || image.height > limits.maximumSide) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            texture::kTextureDomain,
                            code(texture::TextureError::OverLimit),
                            "a source image's side is past the limit");
    }
    // A file too short for its sides is refused before its texels exist.
    if (cursor.left() / leastScanline(image.width) < image.height) {
        return badSource("a Radiance picture shorter than its sides");
    }
    image.rgb.resize(std::size_t{image.width} * image.height * 3);
    std::vector<std::uint8_t> texels(std::size_t{image.width} * 4);
    for (std::uint32_t row = 0; row < image.height; ++row) {
        RAWFRAME_TRY(readScanline(cursor, image.width, texels));
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::uint8_t* const kTexel = &texels[std::size_t{x} * 4];
            float* const kOut = &image.rgb[((std::size_t{row} * image.width) + x) * 3];
            // An exponent of nought is black; otherwise each mantissa is
            // a fraction of 256 scaled by two to the exponent less 128.
            const float kScale = kTexel[3] == 0 ? 0.0F : std::ldexp(1.0F, static_cast<int>(kTexel[3]) - (128 + 8));
            for (std::size_t channel = 0; channel < 3; ++channel) {
                kOut[channel] = static_cast<float>(kTexel[channel]) * kScale;
            }
        }
    }
    return image;
}

result::Result<texture::Texture> cookEnvironment(const LightImage& image, const EnvironmentSettings& settings) {
    if (image.width == 0 || image.height == 0 || image.rgb.size() != std::size_t{image.width} * image.height * 3) {
        return bad("a picture's light does not fill its sides");
    }
    if (settings.side < 4 || settings.side > 2048 || !std::has_single_bit(settings.side)) {
        return bad("an environment's side is a power of two from 4 to 2048");
    }
    if (settings.levels == 0 || settings.levels > static_cast<std::uint32_t>(std::bit_width(settings.side))) {
        return bad("an environment has from one level to its halvings down to one texel");
    }
    if (settings.samples == 0 || settings.samples > 4096) {
        return bad("an environment's levels are filtered from 1 to 4096 samples");
    }
    // Level nought: each texel the average over a grid of points in it,
    // enough to cover the picture's texels it spans.
    const std::uint32_t kGrid =
        std::clamp((image.width + (4 * settings.side) - 1) / (4 * settings.side), std::uint32_t{1}, std::uint32_t{8});
    std::vector<CubeLevel> chain(1);
    CubeLevel& top = chain.front();
    top.side = settings.side;
    top.texels.resize(std::size_t{6} * top.side * top.side);
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < top.side; ++y) {
            for (std::uint32_t x = 0; x < top.side; ++x) {
                std::array<double, 3> sum{};
                for (std::uint32_t down = 0; down < kGrid; ++down) {
                    for (std::uint32_t across = 0; across < kGrid; ++across) {
                        const double kS = (2.0 * (x + ((across + 0.5) / kGrid)) / top.side) - 1;
                        const double kT = (2.0 * (y + ((down + 0.5) / kGrid)) / top.side) - 1;
                        const Rgb kLight = pictureAt(image, directionOf(face, kS, kT));
                        for (std::size_t channel = 0; channel < 3; ++channel) {
                            sum[channel] += kLight[channel];
                        }
                    }
                }
                Rgb& out = top.texels[((std::size_t{face} * top.side + y) * top.side) + x];
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    out[channel] = static_cast<float>(sum[channel] / (kGrid * kGrid));
                }
            }
        }
    }
    // The chain the samples read: the picture's light plainly halved.
    while (chain.back().side > 1) {
        chain.push_back(halve(chain.back()));
    }
    texture::Texture cooked{.format = texture::Format::Rgba16Float, .faces = 6};
    cooked.levels.push_back(bytesOf(chain.front()));
    for (std::uint32_t level = 1; level < settings.levels; ++level) {
        const double kRoughness = static_cast<double>(level) / (settings.levels - 1);
        cooked.levels.push_back(bytesOf(prefilter(chain, settings.side >> level, kRoughness, settings.samples)));
    }
    RAWFRAME_TRY(texture::validate(cooked));
    return cooked;
}

} // namespace rawframe::texture_import
