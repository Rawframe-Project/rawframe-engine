#pragma once

// The sky's picture on the device (D322): a cooked environment, a cube of
// half floats whose levels are its light as ever rougher surfaces reflect
// it (D321), sampled behind everything by the sky's pass and along each
// reflection by the lit shader; and its irradiance, taken on the CPU once
// for each picture, as nine spherical harmonics.

#include "blocks.h"
#include "rawframe/texture/texture.h"

#include <array>
#include <memory>

namespace rawframe::render_scene_gpu {

/// The id the stand-in cube is held as among the frame's textures: bound
/// where the frame has no environment, since a cube's slot takes a cube.
inline constexpr std::uint64_t kNoEnvironment = 0x9d3c5a0e71b2f486ULL;

/// A black cube of one texel a face.
[[nodiscard]] std::shared_ptr<const texture::Texture> darkCube();

/// Whether a texture is an environment: a half-float cube.
[[nodiscard]] bool isEnvironment(const texture::Texture& texture) noexcept;

/// The irradiance over π a surface facing each way takes from the cube,
/// per unit of its light: nine coefficients a channel, over the real
/// spherical harmonics to the second band, in the order the shaders read
/// them (y, z, x; xy, yz, 3z² - 1, xz, x² - y²), each already times its
/// band's share of the cosine lobe (Ramamoorthi and Hanrahan). Taken from
/// level nought, each texel weighed by the solid angle it covers.
[[nodiscard]] std::array<std::array<float, 4>, 9> irradianceOf(const texture::Texture& cube) noexcept;

/// The inverse of a column-major matrix, found in doubles; nought for one
/// that has none.
[[nodiscard]] Matrix4 inverseOf(const Matrix4& matrix) noexcept;

} // namespace rawframe::render_scene_gpu
