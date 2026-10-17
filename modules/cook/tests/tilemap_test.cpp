// The tilemap importer (D858): a tilemap document cooked into a resource
// of its own, its text as written; one that does not read, or whose
// sidecar gives it settings, refused.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/tilemap.h"
#include "rawframe/tilemap/tilemap.h"

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(ATilemapIsCookedAsItWasWritten) {
    const Project kProject;
    const fs::path kLevel = kProject.sources / "level";
    const std::string kText = "tilemap 1\ntileset a44ecb4a39ac5cc8 8 4\nsolid 0\ngrid 3 2\ntiles\n. 4 .\n0 0 0\n";
    writeText(kLevel / "floor.tilemap", kText);
    writeText(kLevel / "floor.tilemap.rfmeta", sidecar("000000000000000000000000000000f1", "", "rawframe.tilemap"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), tilemapImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    RAWFRAME_EXPECT(kCook().cooked == 3);
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    bool found = false;
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.id.value == base::Bits128{0, 0xf1}) {
            found = each.type.value == tilemap::kTilemapType &&
                    each.representation.text() == tilemap::kTilemapRepresentation &&
                    readText(kProject.output / each.locator) == kText;
        }
    }
    RAWFRAME_EXPECT(found);
    // A tile past its sheet, and a tilemap given settings: refused.
    writeText(kLevel / "floor.tilemap", "tilemap 1\ntileset a44ecb4a39ac5cc8 8 4\ngrid 1 1\ntiles\n32\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kLevel / "floor.tilemap", kText);
    writeText(kLevel / "floor.tilemap.rfmeta",
              sidecar("000000000000000000000000000000f1", "\"exact\": true", "rawframe.tilemap"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
