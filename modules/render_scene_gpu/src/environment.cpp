#include "environment.h"

#include <cmath>
#include <numbers>

namespace rawframe::render_scene_gpu {

namespace {

/// The direction through a face's point, `s` rightward and `t` downward
/// from -1 to 1, as Vulkan addresses a cube, unnormalized.
std::array<double, 3> directionOf(std::uint32_t face, double s, double t) noexcept {
    switch (face) {
    case 0:
        return {1, -t, -s};
    case 1:
        return {-1, -t, s};
    case 2:
        return {s, 1, t};
    case 3:
        return {s, -1, -t};
    case 4:
        return {s, -t, 1};
    default:
        return {-s, -t, -1};
    }
}

} // namespace

std::shared_ptr<const texture::Texture> darkCube() {
    texture::Texture made{.format = texture::Format::Rgba16Float, .faces = 6};
    texture::Level level{.width = 1, .height = 1};
    for (std::uint32_t face = 0; face < 6; ++face) {
        // Black, alpha one: 0x3C00 is one as a half.
        for (const std::uint8_t kByte : {0, 0, 0, 0, 0, 0, 0x00, 0x3C}) {
            level.bytes.push_back(std::byte{kByte});
        }
    }
    made.levels.push_back(std::move(level));
    return std::make_shared<const texture::Texture>(std::move(made));
}

bool isEnvironment(const texture::Texture& texture) noexcept {
    return texture.faces == 6 && texture.format == texture::Format::Rgba16Float && !texture.levels.empty();
}

std::array<std::array<float, 4>, 9> irradianceOf(const texture::Texture& cube) noexcept {
    std::array<std::array<double, 3>, 9> sums{};
    double angles = 0;
    if (!isEnvironment(cube)) {
        return {};
    }
    const texture::Level& kLevel = cube.levels.front();
    const std::uint32_t kSide = kLevel.width;
    if (kLevel.bytes.size() != std::size_t{6} * kSide * kSide * 8) {
        return {};
    }
    for (std::uint32_t face = 0; face < 6; ++face) {
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                const double kS = (2.0 * (x + 0.5) / kSide) - 1;
                const double kT = (2.0 * (y + 0.5) / kSide) - 1;
                const std::array<double, 3> kToward = directionOf(face, kS, kT);
                const double kLength = std::hypot(kToward[0], kToward[1], kToward[2]);
                const double kX = kToward[0] / kLength;
                const double kY = kToward[1] / kLength;
                const double kZ = kToward[2] / kLength;
                // A texel's solid angle: its area on the face over the cube
                // of its distance.
                const double kAngle = 4.0 / (static_cast<double>(kSide) * kSide * kLength * kLength * kLength);
                const std::array<double, 9> kBasis = {0.282095,
                                                      0.488603 * kY,
                                                      0.488603 * kZ,
                                                      0.488603 * kX,
                                                      1.092548 * kX * kY,
                                                      1.092548 * kY * kZ,
                                                      0.315392 * ((3 * kZ * kZ) - 1),
                                                      1.092548 * kX * kZ,
                                                      0.546274 * ((kX * kX) - (kY * kY))};
                const std::size_t kAt = ((std::size_t{face} * kSide + y) * kSide + x) * 8;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const auto kLow = std::to_integer<std::uint16_t>(kLevel.bytes[kAt + (channel * 2)]);
                    const auto kHigh = std::to_integer<std::uint16_t>(kLevel.bytes[kAt + (channel * 2) + 1]);
                    const float kLight = texture::floatOf(static_cast<std::uint16_t>(kLow | (kHigh << 8U)));
                    // A texel not a finite light gives none.
                    if (!std::isfinite(kLight)) {
                        continue;
                    }
                    for (std::size_t term = 0; term < 9; ++term) {
                        sums[term][channel] += kLight * kBasis[term] * kAngle;
                    }
                }
                angles += kAngle;
            }
        }
    }
    // The texels' angles, which approximate, made to cover the sphere; the
    // cosine lobe's bands, π, 2π/3, and π/4, over π.
    const double kCover = 4 * std::numbers::pi / angles;
    std::array<std::array<float, 4>, 9> made{};
    for (std::size_t term = 0; term < 9; ++term) {
        const double kBand = term == 0 ? 1.0 : (term < 4 ? 2.0 / 3.0 : 0.25);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            made[term][channel] = static_cast<float>(sums[term][channel] * kCover * kBand);
        }
    }
    return made;
}

Matrix4 inverseOf(const Matrix4& matrix) noexcept {
    std::array<double, 16> m{};
    for (std::size_t at = 0; at < 16; ++at) {
        m[at] = matrix[at];
    }
    // The adjugate by cofactors, over the determinant (the layout is the
    // same whether read by rows or columns).
    std::array<double, 16> inverse{};
    inverse[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
                 m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inverse[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
                 m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inverse[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
                 m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inverse[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
                  m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inverse[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
                 m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inverse[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
                 m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inverse[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
                 m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inverse[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
                  m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inverse[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
                 m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inverse[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
                 m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inverse[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
                  m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inverse[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
                  m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inverse[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
                 m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inverse[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
                 m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inverse[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
                  m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inverse[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
                  m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const double kDeterminant = m[0] * inverse[0] + m[1] * inverse[4] + m[2] * inverse[8] + m[3] * inverse[12];
    Matrix4 made{};
    if (kDeterminant == 0 || !std::isfinite(kDeterminant)) {
        return made;
    }
    for (std::size_t at = 0; at < 16; ++at) {
        made[at] = static_cast<float>(inverse[at] / kDeterminant);
    }
    return made;
}

Reflections reflectionsOf(const render_scene::SceneFrame& frame,
                          std::uint64_t skyCube,
                          std::uint32_t skyLevels,
                          const std::function<std::uint32_t(std::uint64_t)>& levelsOf) {
    Reflections made;
    const ProbeBlock kSky{.place = {0, 0, 0, static_cast<float>(skyLevels)},
                          .light = {frame.lights.sky[0], frame.lights.sky[1], frame.lights.sky[2], 0}};
    made.blocks.push_back(kSky);
    made.cubes.push_back(skyCube);
    for (const render_scene::SceneProbe& kProbe : frame.probes) {
        const std::uint32_t kLevels = levelsOf(kProbe.environment);
        if (kLevels == 0) {
            made.blocks.push_back(kSky);
            made.cubes.push_back(skyCube);
            continue;
        }
        made.blocks.push_back(ProbeBlock{
            .place = {kProbe.position[0], kProbe.position[1], kProbe.position[2], static_cast<float>(kLevels)},
            .extent = {kProbe.half[0], kProbe.half[1], kProbe.half[2], 0},
            .light = {kProbe.intensity, kProbe.intensity, kProbe.intensity, 0}});
        made.cubes.push_back(kProbe.environment);
    }
    return made;
}

} // namespace rawframe::render_scene_gpu
