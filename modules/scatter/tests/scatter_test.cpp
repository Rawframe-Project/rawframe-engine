// Scatter documents: read and written back as read; placed alike
// from the same seed and otherwise from another, every copy within its
// area and its spacing from every other; stood on a terrain's ground and
// kept to its painted layer; and what is not a scatter refused by its line.

#include "rawframe/scatter/errors.h"
#include "rawframe/scatter/scatter.h"
#include "rawframe/terrain/terrain.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::scatter;

namespace {

constexpr std::string_view kRocks = "scatter 1\n"
                                    "prefab rock.scene\n"
                                    "seed 7\n"
                                    "area -10 -5 20 10\n"
                                    "spacing 1.5\n"
                                    "count 40\n"
                                    "turn\n";

Scatter read(std::string_view text) {
    auto made = parseScatter(text);
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? *made : Scatter{};
}

std::vector<Placement> placed(const Scatter& scatter, const terrain::Terrain* ground = nullptr) {
    auto made = placements(scatter, ground);
    RAWFRAME_EXPECT(made.has_value());
    return made.has_value() ? *made : std::vector<Placement>{};
}

} // namespace

RAWFRAME_TEST(AScatterIsWrittenAsItReads) {
    const Scatter kRead = read(kRocks);
    RAWFRAME_EXPECT(kRead.prefab == "rock.scene" && kRead.seed == 7 && kRead.width == 20 && kRead.count == 40 &&
                    kRead.turn && kRead.terrain.empty());
    RAWFRAME_EXPECT(read(writeScatter(kRead)) == kRead);
    const Scatter kOn = read("scatter 1\nprefab tree.scene\narea 0 0 4 4\ncount 3\non field.terrain at 1 2 3\n"
                             "layer grass\n");
    RAWFRAME_EXPECT(kOn.terrain == "field.terrain" && kOn.at[1] == 2 && kOn.layer == "grass" && kOn.seed == 0);
    RAWFRAME_EXPECT(read(writeScatter(kOn)) == kOn);
}

RAWFRAME_TEST(CopiesArePlacedAlikeWithinTheAreaAndApart) {
    const Scatter kRocks2 = read(kRocks);
    const std::vector<Placement> kFirst = placed(kRocks2);
    RAWFRAME_EXPECT(kFirst.size() == 40 && placed(kRocks2) == kFirst);
    for (std::size_t one = 0; one < kFirst.size(); ++one) {
        const Placement& kOne = kFirst[one];
        RAWFRAME_EXPECT(kOne.x >= -10 && kOne.x < 10 && kOne.z >= -5 && kOne.z < 5 && kOne.y == 0);
        RAWFRAME_EXPECT(kOne.yaw >= 0 && kOne.yaw < 6.2832);
        for (std::size_t other = one + 1; other < kFirst.size(); ++other) {
            RAWFRAME_EXPECT(std::hypot(kOne.x - kFirst[other].x, kOne.z - kFirst[other].z) >= 1.5);
        }
    }
    // Another seed places them elsewhere; no turn leaves them unturned.
    Scatter reseeded = kRocks2;
    reseeded.seed = 8;
    reseeded.turn = false;
    const std::vector<Placement> kOther = placed(reseeded);
    RAWFRAME_EXPECT(kOther.size() == 40 && kOther[0].x != kFirst[0].x && kOther[0].yaw == 0);
    // More than fit: as many as the spacing lets in, fewer than asked.
    Scatter crowded = kRocks2;
    crowded.count = 10000;
    crowded.spacing = 4;
    const std::size_t kFit = placed(crowded).size();
    RAWFRAME_EXPECT(kFit > 4 && kFit < 40);
}

RAWFRAME_TEST(CopiesStandOnATerrainsGroundAndLayer) {
    // A ramp rising a meter a sample along X, its far half painted rock,
    // its origin one meter up and ten along X.
    auto ground = terrain::parseTerrain("terrain 1\ngrid 5 2\ncell 1\nlayer grass 00000000000000a1\n"
                                        "layer rock 00000000000000b2\nheights\n0 1 2 3 4\n0 1 2 3 4\n"
                                        "layers\n0011\n");
    RAWFRAME_EXPECT(ground.has_value());
    if (!ground.has_value()) {
        return;
    }
    Scatter onRamp = read("scatter 1\nprefab tree.scene\nseed 3\narea 8 -2 8 5\nspacing 0.2\ncount 20\n"
                          "on ramp.terrain at 10 1 0\n");
    const std::vector<Placement> kAll = placed(onRamp, &*ground);
    RAWFRAME_EXPECT(kAll.size() == 20);
    for (const Placement& kEach : kAll) {
        // Only over the grid, ten to fourteen along X and nought to one
        // along Z, each on the ground.
        RAWFRAME_EXPECT(kEach.x >= 10 && kEach.x <= 14 && kEach.z >= 0 && kEach.z <= 1);
        RAWFRAME_EXPECT(std::abs(kEach.y - (1 + (kEach.x - 10))) < 1e-9);
    }
    onRamp.layer = "rock";
    for (const Placement& kEach : placed(onRamp, &*ground)) {
        RAWFRAME_EXPECT(kEach.x >= 12);
    }
    onRamp.layer = "sand";
    const auto kUnknown = placements(onRamp, &*ground);
    RAWFRAME_EXPECT(!kUnknown.has_value() && kUnknown.error().code() == code(ScatterError::UnknownLayer));
}

RAWFRAME_TEST(PlacedCopiesAreReadBackToTheBit) {
    const Placed kPlaced{.prefab = "rock.scene", .placements = placed(read(kRocks))};
    const auto kRead = parsePlaced(writePlaced(kPlaced));
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kPlaced);
    RAWFRAME_EXPECT(!parsePlaced("placed 1\nprefab a.scene\n1 2 3\n").has_value());
    RAWFRAME_EXPECT(!parsePlaced("placed 1\nprefab a.scene\n1 2 3 nan\n").has_value());
    RAWFRAME_EXPECT(!parsePlaced("placed 1\n").has_value());
}

RAWFRAME_TEST(WhatIsNotAScatterIsRefusedByItsLine) {
    for (const auto& [kText, kLine] :
         {std::pair{std::string_view{"scattered 1\n"}, std::string_view{"scatter line 1"}},
          std::pair{std::string_view{"scatter 1\nprefab a.scene\narea 0 0 0 4\ncount 1\n"}, std::string_view{"line 3"}},
          std::pair{std::string_view{"scatter 1\nprefab a.scene\narea 0 0 4 4\ncount 10001\n"},
                    std::string_view{"line 4"}},
          std::pair{std::string_view{"scatter 1\nprefab a.scene\narea 0 0 4 4\ncount 1\nspacing -1\n"},
                    std::string_view{"line 5"}},
          std::pair{std::string_view{"scatter 1\nprefab a.scene\narea 0 0 4 4\ncount 1\nlayer grass\n"},
                    std::string_view{"a layer is a terrain's"}},
          std::pair{std::string_view{"scatter 1\narea 0 0 4 4\ncount 1\n"}, std::string_view{"names its prefab"}},
          std::pair{std::string_view{"scatter 1\nprefab a.scene\nprefab b.scene\n"}, std::string_view{"line 3"}}}) {
        const auto kRead = parseScatter(kText);
        RAWFRAME_EXPECT(!kRead.has_value() && kRead.error().code() == code(ScatterError::BadSource) &&
                        kRead.error().description().find(kLine) != std::string_view::npos);
    }
}
