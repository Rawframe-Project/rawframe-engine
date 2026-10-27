// Blockout documents: solids read from their lines and written back
// as read; each made into closed faces facing out, measured by the volume
// they bound, so a face turned the wrong way or a cut that leaves a hole
// open shows; and what is not a blockout refused by its line.

#include "rawframe/blockout/blockout.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <numbers>
#include <string_view>

using namespace rawframe;
using namespace rawframe::blockout;

namespace {

bool near(double value, double expected, double within = 1e-4) {
    return std::abs(value - expected) < within;
}

/// The volume a closed mesh bounds, by the divergence theorem: each
/// triangle's signed tetrahedron with the origin. Positive where its faces
/// face out.
double volumeOf(const mesh::Mesh& made) {
    double volume = 0;
    for (std::size_t at = 0; at + 2 < made.indices.size(); at += 3) {
        const auto& kA = made.positions[made.indices[at]];
        const auto& kB = made.positions[made.indices[at + 1]];
        const auto& kC = made.positions[made.indices[at + 2]];
        volume += ((kA[0] * ((kB[1] * kC[2]) - (kB[2] * kC[1]))) - (kA[1] * ((kB[0] * kC[2]) - (kB[2] * kC[0]))) +
                   (kA[2] * ((kB[0] * kC[1]) - (kB[1] * kC[0])))) /
                  6.0;
    }
    return volume;
}

mesh::Mesh made(std::string_view text) {
    auto read = parseBlockout(text);
    RAWFRAME_EXPECT(read.has_value());
    auto built = read.has_value() ? meshOf(*read) : result::Result<mesh::Mesh>{};
    RAWFRAME_EXPECT(built.has_value());
    return built.has_value() ? *built : mesh::Mesh{};
}

} // namespace

RAWFRAME_TEST(ABoxIsTwelveTrianglesBoundingItsVolume) {
    const mesh::Mesh kBox = made("blockout 1\nbox floor at 1 0 2 size 4 1 2\n");
    RAWFRAME_EXPECT(kBox.indices.size() == 36 && kBox.parts.size() == 1 && kBox.parts[0].indexCount == 36);
    RAWFRAME_EXPECT(near(volumeOf(kBox), 8));
    // Its base on the plane `at` is on, its top a meter up.
    float lowest = 1e9F;
    float highest = -1e9F;
    for (const auto& kAt : kBox.positions) {
        lowest = std::min(lowest, kAt[1]);
        highest = std::max(highest, kAt[1]);
    }
    RAWFRAME_EXPECT(near(lowest, 0) && near(highest, 1));
    // Turned, it bounds the same.
    RAWFRAME_EXPECT(near(volumeOf(made("blockout 1\nbox b at 0 0 0 size 4 1 2 turn 30\n")), 8));
}

RAWFRAME_TEST(RampsCylindersAndExtrusionsBoundTheirVolumes) {
    RAWFRAME_EXPECT(near(volumeOf(made("blockout 1\nramp up at 0 0 0 size 2 1 4 turn 90\n")), 4));
    // A cylinder of twelve sides: its base the polygon's area.
    const double kTwelve = 6 * 0.25 * std::sin(2 * std::numbers::pi / 12);
    RAWFRAME_EXPECT(
        near(volumeOf(made("blockout 1\ncylinder p at 0 0 0 radius 0.5 height 3 sides 12\n")), kTwelve * 3));
    RAWFRAME_EXPECT(near(volumeOf(made("blockout 1\nextrude s at 0 0 -6 height 0.5 points -2 -1 -2 1 2 1 2 -1\n")), 4));
}

RAWFRAME_TEST(ACutTakesItsSolidOutOfThoseAbove) {
    // A hole through the middle of a 2 m cube: 8 less the column's 2.
    const mesh::Mesh kHoled = made("blockout 1\nbox cube at 0 0 0 size 2 2 2\ncut box hole at 0 -1 0 size 1 4 1\n");
    RAWFRAME_EXPECT(near(volumeOf(kHoled), 6));
    // A cut past every solid takes nothing; one that holds it takes all.
    RAWFRAME_EXPECT(near(volumeOf(made("blockout 1\nbox a at 0 0 0 size 1 1 1\ncut box b at 9 0 0 size 1 1 1\n")), 1));
    RAWFRAME_EXPECT(near(volumeOf(made("blockout 1\nbox a at 0 0 0 size 1 1 1\ncut box b at 0 -1 0 size 3 3 3\n")), 0));
    // A solid after the cut is whole: the cut is of those above it.
    RAWFRAME_EXPECT(
        near(volumeOf(made(
                 "blockout 1\nbox a at 0 0 0 size 1 1 1\ncut box b at 0 -1 0 size 3 3 3\nbox c at 0 0 0 size 1 1 1\n")),
             1));
}

RAWFRAME_TEST(ABlockoutIsWrittenAsItReads) {
    const std::string_view kText = "blockout 1\n"
                                   "box floor at 0 0 0 size 20 0.5 20\n"
                                   "ramp up at 4 0.5 0 size 2 1 4 turn 90\n"
                                   "cylinder pillar at -3 0.5 2 radius 0.5 height 3 sides 12\n"
                                   "extrude stage at 0 0.5 -6 height 0.4 points -2 -1 -2 1 2 1 2 -1\n"
                                   "cut box door at 0 0.5 -8 size 1.2 2 1\n";
    const auto kRead = parseBlockout(kText);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->solids.size() == 5 && kRead->solids[4].cut);
    RAWFRAME_EXPECT(kRead.has_value() && writeBlockout(*kRead) == kText);
    // Comments and blank lines are nothing.
    const auto kCommented = parseBlockout("# a level\nblockout 1\n\nbox a at 0 0 0 size 1 1 1 # the floor\n");
    RAWFRAME_EXPECT(kCommented.has_value() && kCommented->solids.size() == 1);
}

RAWFRAME_TEST(WhatIsNotABlockoutIsRefusedByItsLine) {
    for (const std::string_view kText : {
             "box a at 0 0 0 size 1 1 1\n",
             "blockout 2\n",
             "blockout 1\nsphere a at 0 0 0\n",
             "blockout 1\nbox a at 0 0 0 size 1 0 1\n",
             "blockout 1\nbox a at 0 0 0 size 1 nan 1\n",
             "blockout 1\nbox a at 0 0 0 size 1 1 1\nbox a at 2 0 0 size 1 1 1\n",
             "blockout 1\ncylinder c at 0 0 0 radius 1 height 1 sides 2\n",
             "blockout 1\nextrude e at 0 0 0 height 1 points -2 -1 2 -1 2 1 -2 1\n",
             "blockout 1\nbox a at 0 0 0 size 1 1 1 radius 2\n",
             "blockout 1\nbox a at 0 0 0 size 20000 1 1\n",
         }) {
        const auto kRead = parseBlockout(kText);
        RAWFRAME_EXPECT(!kRead.has_value());
        RAWFRAME_EXPECT(kRead.has_value() || kRead.error().description().find("blockout line") != std::string::npos);
    }
}

RAWFRAME_TEST(ABlockoutPastItsTrianglesIsRefused) {
    const auto kRead = parseBlockout("blockout 1\ncylinder c at 0 0 0 radius 1 height 1 sides 64\n");
    RAWFRAME_EXPECT(kRead.has_value());
    RAWFRAME_EXPECT(kRead.has_value() && !meshOf(*kRead, BlockoutLimits{.triangles = 100}).has_value());
}

RAWFRAME_TEST(ASolidsFootprintIsWhereItsCornersReachFromAbove) {
    const auto kRead = parseBlockout("blockout 1\nbox a at 1 0 2 size 4 1 2\nbox b at 1 0 2 size 4 1 2 turn 90\n"
                                     "cylinder c at 0 0 0 radius 1 height 1 sides 4\n");
    RAWFRAME_EXPECT(kRead.has_value() && kRead->solids.size() == 3);
    if (!kRead.has_value() || kRead->solids.size() != 3) {
        return;
    }
    const Footprint kA = footprintOf(kRead->solids[0]);
    RAWFRAME_EXPECT(near(kA.minX, -1) && near(kA.maxX, 3) && near(kA.minZ, 1) && near(kA.maxZ, 3));
    // Turned a quarter, its long side runs along Z.
    const Footprint kB = footprintOf(kRead->solids[1]);
    RAWFRAME_EXPECT(near(kB.minX, 0) && near(kB.maxX, 2) && near(kB.minZ, 0) && near(kB.maxZ, 4));
    // Four sides: corners on the axes, a meter out.
    const Footprint kC = footprintOf(kRead->solids[2]);
    RAWFRAME_EXPECT(near(kC.minX, -1) && near(kC.maxX, 1) && near(kC.minZ, -1) && near(kC.maxZ, 1));
}
