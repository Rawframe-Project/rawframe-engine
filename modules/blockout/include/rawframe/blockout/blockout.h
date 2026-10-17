#pragma once

// A blockout document (SPEC-0051's blockout row, D846): a level's rough
// shapes as text, one solid a line, made into one mesh that a scene draws
// and a static body collides with (a `rawframe.physics3d.body` of shape
// Mesh). Written by hand, by a tool, or by Studio's Blockout mode; cooked
// by the `rawframe.blockout` importer.
//
//     blockout 1
//     box floor at 0 0 0 size 20 0.5 20
//     ramp up at 4 0.5 0 size 2 1 4 turn 90
//     cylinder pillar at -3 0.5 2 radius 0.5 height 3 sides 12
//     extrude stage at 0 0.5 -6 height 0.4 points -2 -1 -2 1 2 1 2 -1
//     cut box door at 0 0.5 -8 size 1.2 2 1
//
// `at` is the middle of a solid's base, in meters with +Y up; `turn` is
// degrees about +Y. A ramp rises from its -Z edge to its +Z edge. An
// extrusion's points are its base's corners on the level plane (x z
// pairs, relative to `at`), convex and counter-clockwise seen from above.
// A line beginning `cut` takes its solid out of every solid above it.
// Solids listed together are their union. `#` begins a comment.

#include "rawframe/mesh/mesh.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::blockout {

enum class Shape : std::uint8_t {
    Box,
    Ramp,
    Cylinder,
    Extrude
};

/// One solid of a blockout, as its line says.
struct Solid {
    Shape shape = Shape::Box;
    std::string name;
    bool cut = false;
    std::array<double, 3> at{};
    /// A box's or a ramp's width (X), height (Y), and depth (Z); a
    /// cylinder's radius twice and height; an extrusion's height.
    std::array<double, 3> size{};
    double turn = 0;
    std::uint32_t sides = 16;
    /// An extrusion's base, x z pairs relative to `at`.
    std::vector<std::array<double, 2>> points;

    friend bool operator==(const Solid&, const Solid&) = default;
};

struct Blockout {
    std::vector<Solid> solids;

    friend bool operator==(const Blockout&, const Blockout&) = default;
};

/// The bounds of a blockout: how many solids, a solid's extent, its sides,
/// an extrusion's points, and the triangles it makes.
struct BlockoutLimits {
    std::size_t solids = 256;
    double extent = 10'000;
    std::uint32_t mostSides = 64;
    std::size_t points = 64;
    std::size_t triangles = 65'536;
};

/// The document's solids, in its order. Refuses (`BadSource` of the mesh
/// family, naming the line) a first line that is not `blockout 1`, a word
/// it does not know, a number that does not read or is not finite, a size
/// not above nought or past the extent, two solids of one name, sides
/// outside 3 to the most, an extrusion that is not convex and
/// counter-clockwise, and more solids than the limit.
[[nodiscard]] result::Result<Blockout> parseBlockout(std::string_view text, const BlockoutLimits& limits = {});

/// The document written back, a line a solid, as `parseBlockout` reads it.
[[nodiscard]] std::string writeBlockout(const Blockout& blockout);

/// The mesh the solids make: each solid's faces, every `cut` taken out of
/// the solids above it, flat normals, coordinates projected on the face's
/// main axis a meter a tile, one part drawn with material nought. Refuses
/// (`OverLimit` of the mesh family) more triangles than the limit.
[[nodiscard]] result::Result<mesh::Mesh> meshOf(const Blockout& blockout, const BlockoutLimits& limits = {});

} // namespace rawframe::blockout
