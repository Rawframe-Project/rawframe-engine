#pragma once

// A scatter document: copies of one of a
// game's prefabs placed over an area, none nearer another than a spacing,
// as text. Written by hand, by a tool, or through the session's lines;
// expanded where a game is loaded into the prefab's entities, each placed.
//
//     scatter 1
//     prefab rock.scene
//     seed 7
//     area -20 -20 40 40
//     spacing 1.5
//     count 200
//     turn
//     on field.terrain at 0 0 0
//     layer grass
//
// `prefab` names one of the game's prefabs by its file. `area` is the
// ground's rectangle, its least X and Z then its width and depth in meters
// (in a 2D game, X and Y). At most `count` copies are placed, as many as
// fit `spacing` apart, drawn in the order `seed` gives. `turn` turns each
// about the vertical by its own angle. `on` stands each on a terrain of
// the game, its origin where `at` puts it, and leaves out what is off its
// grid; `layer` leaves out what is not on a cell painted with that layer.
// `#` begins a comment.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/terrain/terrain.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::scatter {

/// A cooked scatter's resource type and representation: its prefab
/// and its placements, so what a game reads from its content places the
/// copies exactly where its sources would.
inline constexpr base::Bits128 kPlacedType = base::parseBits128Hex("9b831bba4e262e6b8ed95b93187104d6").value;
inline constexpr std::string_view kPlacedRepresentation = "rawframe.scatter.placed";

struct Scatter {
    std::string prefab;
    std::uint64_t seed = 0;
    /// The area's least X and Z, and its width and depth, meters.
    double x = 0;
    double z = 0;
    double width = 0;
    double depth = 0;
    double spacing = 1;
    std::uint32_t count = 0;
    bool turn = false;
    /// The terrain each stands on, by its file, none if empty, and where
    /// its origin is; the layer each must be on, any if empty.
    std::string terrain;
    std::array<double, 3> at{};
    std::string layer;

    friend bool operator==(const Scatter&, const Scatter&) = default;
};

/// The explicit limit profile of one scatter: its copies, its area's sides,
/// and the draws a copy is tried with before the scatter is full.
struct ScatterLimits {
    std::uint32_t mostCount = 10000;
    double mostSide = 100000;
    std::uint32_t triesEach = 30;
};

/// Refuses (`BadSource`, naming the line) what is not a scatter document of
/// this form within `limits`: a prefab, an area of sides above nought and
/// at most `mostSide`, a spacing above nought, a count of 1 to `mostCount`,
/// and a layer only on a terrain.
[[nodiscard]] result::Result<Scatter> parseScatter(std::string_view text, const ScatterLimits& limits = {});

/// The document `scatter` is, in the form `parseScatter` reads back to it.
[[nodiscard]] std::string writeScatter(const Scatter& scatter);

/// Where one copy stands, meters, and its turn about the vertical, radians.
struct Placement {
    double x = 0;
    double y = 0;
    double z = 0;
    double yaw = 0;

    friend bool operator==(const Placement&, const Placement&) = default;
};

/// Where each copy stands, the same for the same scatter and ground on
/// every machine: draws from the seed alone, each kept where it is
/// `spacing` from every one kept before it and, on `ground`, over its grid
/// and its layer, until `count` are kept or each has had its tries. `ground`
/// is the terrain the scatter names, read; refused (`UnknownLayer`) when it
/// does not name the scatter's layer.
[[nodiscard]] result::Result<std::vector<Placement>>
placements(const Scatter& scatter, const terrain::Terrain* ground, const ScatterLimits& limits = {});

/// A scatter placed: its prefab, by its file as the scatter names it, and
/// where each copy stands.
struct Placed {
    std::string prefab;
    std::vector<Placement> placements;

    friend bool operator==(const Placed&, const Placed&) = default;
};

/// The cooked form of `placed`, every number to the bit, and its reading
/// back, refused (`BadSource`) as anything else or past `limits`' count.
[[nodiscard]] std::string writePlaced(const Placed& placed);
[[nodiscard]] result::Result<Placed> parsePlaced(std::string_view text, const ScatterLimits& limits = {});

} // namespace rawframe::scatter
