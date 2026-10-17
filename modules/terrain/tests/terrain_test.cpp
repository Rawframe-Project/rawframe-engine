// Terrain documents (D853): heights and painted cells read and written back
// as read; made into a mesh facing up, a part a layer painted, its normals
// leaning off a slope; brushes that raise, smooth, and paint where they
// reach and nowhere else; and what is not a terrain refused by its line.

#include "rawframe/terrain/terrain.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <string_view>

using namespace rawframe;
using namespace rawframe::terrain;

namespace {

constexpr std::string_view kSlope = "terrain 1\n"
                                    "grid 4 3\n"
                                    "cell 2\n"
                                    "layer grass 0000000000000011\n"
                                    "layer rock 0000000000000022\n"
                                    "heights\n"
                                    "0 0 0.5 1\n"
                                    "0 0.25 1 1.5\n"
                                    "0 0.5 1.5 2.125\n"
                                    "layers\n"
                                    "001\n"
                                    "011\n";

Terrain read(std::string_view text) {
    auto made = parseTerrain(text);
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? *made : Terrain{};
}

Terrain flat(std::uint32_t side) {
    std::string text = "terrain 1\ngrid " + std::to_string(side) + " " + std::to_string(side) +
                       "\ncell 1\nlayer a 00000000000000a1\nlayer b 00000000000000b2\nheights\n";
    for (std::uint32_t row = 0; row < side; ++row) {
        for (std::uint32_t column = 0; column < side; ++column) {
            text += column == 0 ? "0" : " 0";
        }
        text += "\n";
    }
    return read(text);
}

} // namespace

RAWFRAME_TEST(ATerrainIsWrittenAsItReads) {
    const Terrain kSlopeRead = read(kSlope);
    RAWFRAME_EXPECT(kSlopeRead.columns == 4 && kSlopeRead.rows == 3 && kSlopeRead.layers.size() == 2);
    RAWFRAME_EXPECT(kSlopeRead.heights.size() == 12 && kSlopeRead.heights[11] == 2125);
    RAWFRAME_EXPECT(kSlopeRead.painted == (std::vector<std::uint8_t>{0, 0, 1, 0, 1, 1}));
    RAWFRAME_EXPECT(writeTerrain(kSlopeRead) == kSlope);
    // Unpainted, every cell is its first layer's; with no layers, none's.
    const Terrain kPlain = read("terrain 1\ngrid 2 2\nheights\n0 0\n0 0 # flat\n");
    RAWFRAME_EXPECT(kPlain.painted == (std::vector<std::uint8_t>{0}) && kPlain.cell == 1);
}

RAWFRAME_TEST(ATerrainsMeshFacesUpAPartALayer) {
    const auto kMesh = meshOf(read(kSlope));
    RAWFRAME_EXPECT(kMesh.has_value());
    if (!kMesh.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kMesh->positions.size() == 12 && kMesh->indices.size() == 6 * 6);
    RAWFRAME_EXPECT(kMesh->parts.size() == 2 && kMesh->parts[0].material == 0x11 && kMesh->parts[1].material == 0x22);
    RAWFRAME_EXPECT(kMesh->parts[0].indexCount == 3 * 6 && kMesh->parts[1].indexCount == 3 * 6);
    // Each triangle faces up: counter-clockwise seen from above.
    for (std::size_t at = 0; at + 2 < kMesh->indices.size(); at += 3) {
        const auto& kA = kMesh->positions[kMesh->indices[at]];
        const auto& kB = kMesh->positions[kMesh->indices[at + 1]];
        const auto& kC = kMesh->positions[kMesh->indices[at + 2]];
        const float kUp = ((kB[2] - kA[2]) * (kC[0] - kA[0])) - ((kB[0] - kA[0]) * (kC[2] - kA[2]));
        RAWFRAME_EXPECT(kUp > 0);
    }
    // Rising along +X, a sample's normal leans back toward -X.
    RAWFRAME_EXPECT(kMesh->normals[5][0] < 0 && kMesh->normals[5][1] > 0);
    // A flat one's point straight up.
    const auto kFlat = meshOf(flat(3));
    RAWFRAME_EXPECT(kFlat.has_value() && kFlat->parts.size() == 1 && std::abs(kFlat->normals[4][1] - 1) < 1e-6F);
}

RAWFRAME_TEST(BrushesChangeOnlyWhatTheyReach) {
    Terrain ground = flat(9);
    // A meter up at the middle, falling to nought at two meters out.
    RAWFRAME_EXPECT(raise(ground, Brush{.x = 4, .z = 4, .radius = 2}, 1) > 0);
    RAWFRAME_EXPECT(ground.heights[(4 * 9) + 4] == 1000);
    RAWFRAME_EXPECT(ground.heights[(4 * 9) + 5] > 0 && ground.heights[(4 * 9) + 5] < 1000);
    RAWFRAME_EXPECT(ground.heights[(4 * 9) + 6] == 0 && ground.heights[0] == 0);
    // Smoothing lowers the peak toward its neighbors, and leaves the far
    // corner as it was.
    RAWFRAME_EXPECT(smooth(ground, Brush{.x = 4, .z = 4, .radius = 3}, 1) > 0);
    RAWFRAME_EXPECT(ground.heights[(4 * 9) + 4] < 1000 && ground.heights[(4 * 9) + 4] > 0 && ground.heights[0] == 0);
    // Painting reaches the cells whose middles it covers, and only a layer
    // the terrain names.
    RAWFRAME_EXPECT(paint(ground, Brush{.x = 0.5, .z = 0.5, .radius = 0.6}, 1) == 1);
    RAWFRAME_EXPECT(ground.painted[0] == 1 && ground.painted[1] == 0);
    RAWFRAME_EXPECT(paint(ground, Brush{.x = 0.5, .z = 0.5, .radius = 0.6}, 7) == 0);
    // And it writes and reads back as it stands.
    RAWFRAME_EXPECT(read(writeTerrain(ground)) == ground);
}

RAWFRAME_TEST(WhatIsNotATerrainIsRefusedByItsLine) {
    for (const std::string_view kText : {
             "grid 2 2\n",
             "terrain 2\n",
             "terrain 1\ngrid 1 2\nheights\n0\n0\n",
             "terrain 1\ngrid 256 2\n",
             "terrain 1\ngrid 2 2\ncell 0\n",
             "terrain 1\ngrid 2 2\nheights\n0 0\n",
             "terrain 1\ngrid 2 2\nheights\n0 0\n0 x\n",
             "terrain 1\ngrid 2 2\nheights\n0 0\n0 20000\n",
             "terrain 1\ngrid 2 2\nlayer a 0000000000000001\nlayer a 0000000000000002\n",
             "terrain 1\ngrid 2 2\nlayer a grass.rfmaterial\n",
             "terrain 1\ngrid 2 2\nlayer a 0000000000000001\nheights\n0 0\n0 0\nlayers\n1\n",
             "terrain 1\ngrid 3 2\nlayer a 0000000000000001\nheights\n0 0 0\n0 0 0\nlayers\n0\n",
             "terrain 1\ngrid 2 2\nheights\n0 0\n0 0\nlayers\n0\n",
             "terrain 1\nheights\n",
         }) {
        const auto kRead = parseTerrain(kText);
        RAWFRAME_EXPECT(!kRead.has_value());
        RAWFRAME_EXPECT(kRead.has_value() || kRead.error().description().find("terrain line") != std::string::npos);
    }
}
