#pragma once

// A terrain document (SPEC-0051's terrain row, D853): a ground's heights on
// a grid and the layer each of its cells is painted with, as text. Made
// into one mesh that a scene draws and a static body collides with (a
// `rawframe.physics3d.body` of shape Mesh), each layer a part drawn with its
// material. Written by hand, by a tool, by the session's brushes, or by
// Studio's Terrain mode; cooked by the `rawframe.terrain` importer.
//
//     terrain 1
//     grid 4 3
//     cell 2
//     layer grass a1e844c906888b94
//     layer rock d42b8d7ca0f22201
//     heights
//     0 0 0.5 1
//     0 0.25 1 1.5
//     0 0.5 1.5 2
//     layers
//     001
//     011
//
// `grid` is the samples along X and along Z, `cell` the meters between
// them; sample (0, 0) is at the origin, a row runs along +X, and rows go
// along +Z. Heights are meters, kept to the millimeter. A layer names its
// material by the identity the game's description gives it (`material
// <identity> <path>`), sixteen hexadecimal digits, as a Model names one;
// each cell, between four samples, is painted with a layer, by its place
// among them as a hexadecimal digit. `#` begins a comment.

#include "rawframe/mesh/mesh.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::terrain {

struct Layer {
    std::string name;
    /// Its material, by the identity the game gives it.
    std::uint64_t material = 0;

    friend bool operator==(const Layer&, const Layer&) = default;
};

struct Terrain {
    /// Samples along X and along Z.
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    /// Meters between samples.
    double cell = 1;
    std::vector<Layer> layers;
    /// Each sample's height in millimeters, row by row, X fastest.
    std::vector<std::int32_t> heights;
    /// Each cell's layer, row by row, X fastest: (rows - 1) by
    /// (columns - 1).
    std::vector<std::uint8_t> painted;

    friend bool operator==(const Terrain&, const Terrain&) = default;
};

/// The explicit limit profile of one terrain: the grid one chunk of Maul3D's
/// native heightfield holds.
struct TerrainLimits {
    std::uint32_t mostSamples = 255;
    double mostCell = 100;
    std::int32_t mostMillimeters = 10'000'000;
    std::size_t mostLayers = 16;
};

/// Refuses (`BadSource`, naming the line) what is not a terrain document of
/// this form within `limits`: a grid of 2 to `mostSamples` each way, a cell
/// above nought and at most `mostCell`, at most `mostLayers` layers of
/// distinct names, each sample's height and each cell's layer, and a cell's
/// layer a layer it names.
[[nodiscard]] result::Result<Terrain> parseTerrain(std::string_view text, const TerrainLimits& limits = {});

/// The document `terrain` is, in the form `parseTerrain` reads back to it.
[[nodiscard]] std::string writeTerrain(const Terrain& terrain);

/// The terrain made into one mesh: two triangles a cell, facing up, normals
/// from the heights around each sample, texture coordinates across the
/// whole grid; one part a layer painted, in the layers' order, drawn with
/// its layer's material.
[[nodiscard]] result::Result<mesh::Mesh> meshOf(const Terrain& terrain);

/// Where a brush reaches: its middle on the ground's plane, in meters, and
/// how far.
struct Brush {
    double x = 0;
    double z = 0;
    double radius = 1;
};

/// Raises the samples within the brush by `meters` at its middle, falling
/// smoothly to nought at its edge (lowers them where negative), held within
/// the limits' heights; how many changed.
std::size_t raise(Terrain& terrain, const Brush& brush, double meters, const TerrainLimits& limits = {});

/// Moves the samples within the brush toward the mean of their neighbors,
/// by `strength` (nought to one) at its middle, falling to nought at its
/// edge; how many changed.
std::size_t smooth(Terrain& terrain, const Brush& brush, double strength);

/// Paints the cells whose middles are within the brush with `layer`, which
/// must be one the terrain names; how many changed.
std::size_t paint(Terrain& terrain, const Brush& brush, std::uint8_t layer);

} // namespace rawframe::terrain
