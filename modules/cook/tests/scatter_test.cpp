// The scatter importer: a scatter document cooked into its
// placements, standing on the terrain it names, which is one of its inputs:
// the ground shaped again cooks the copies again. A scatter whose terrain
// is not there, or whose sidecar gives it settings, refused.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/scatter.h"
#include "rawframe/cook/terrain.h"
#include "rawframe/scatter/scatter.h"

#include <cmath>
#include <optional>

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

namespace {

/// The placed scatter the cook made of the source `id`.
std::optional<scatter::Placed> placedOf(const fs::path& output, base::Bits128 id) {
    const auto kManifest = content::readManifest(readText(output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.id.value == id && each.type.value == scatter::kPlacedType &&
            each.representation.text() == scatter::kPlacedRepresentation) {
            auto read = scatter::parsePlaced(readText(output / each.locator));
            RAWFRAME_EXPECT(read.has_value());
            return read.has_value() ? std::optional{*read} : std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

RAWFRAME_TEST(AScatterIsCookedIntoItsPlacementsOnItsGround) {
    const Project kProject;
    const fs::path kLevel = kProject.sources / "level";
    // A flat field a meter up, its scatter's copies over its middle.
    const auto kField = [&kLevel](std::string_view height) {
        std::string text = "terrain 1\ngrid 3 3\ncell 4\nlayer grass 00000000000000a1\nheights\n";
        for (int row = 0; row < 3; ++row) {
            text += std::string{height} + " " + std::string{height} + " " + std::string{height} + "\n";
        }
        writeText(kLevel / "field.terrain", text);
    };
    kField("1");
    writeText(kLevel / "field.terrain.rfmeta", sidecar("000000000000000000000000000000e1", "", "rawframe.terrain"));
    writeText(kLevel / "rocks.scatter",
              "scatter 1\nprefab rock.scene\nseed 5\narea 1 1 6 6\nspacing 1\ncount 12\non field.terrain at 0 0 0\n");
    writeText(kLevel / "rocks.scatter.rfmeta", sidecar("000000000000000000000000000000e2", "", "rawframe.scatter"));
    static const std::array<Importer, 3> kImporters = {audioImporter(), scatterImporter(), terrainImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    RAWFRAME_EXPECT(kCook().failures.empty());
    const auto kOnField = placedOf(kProject.output, base::Bits128{0, 0xe2});
    RAWFRAME_EXPECT(kOnField.has_value() && kOnField->prefab == "rock.scene" && kOnField->placements.size() == 12);
    for (const scatter::Placement& kEach : kOnField.value_or(scatter::Placed{}).placements) {
        RAWFRAME_EXPECT(std::abs(kEach.y - 1) < 1e-9);
    }
    // The field raised: the copies stand on it again, where they stood.
    kField("2.5");
    RAWFRAME_EXPECT(kCook().failures.empty());
    const auto kRaised = placedOf(kProject.output, base::Bits128{0, 0xe2});
    RAWFRAME_EXPECT(kRaised.has_value() && kRaised->placements.size() == 12 &&
                    std::abs(kRaised->placements[0].y - 2.5) < 1e-9 &&
                    kRaised->placements[0].x == kOnField.value_or(scatter::Placed{}).placements.at(0).x);
    // On a terrain that is not there, and given settings: refused.
    writeText(kLevel / "rocks.scatter",
              "scatter 1\nprefab rock.scene\narea 1 1 6 6\ncount 12\non hill.terrain at 0 0 0\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kLevel / "rocks.scatter", "scatter 1\nprefab rock.scene\narea 1 1 6 6\ncount 12\n");
    writeText(kLevel / "rocks.scatter.rfmeta",
              sidecar("000000000000000000000000000000e2", "\"exact\": true", "rawframe.scatter"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
