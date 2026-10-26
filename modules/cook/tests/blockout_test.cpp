// The blockout importer: a blockout document cooked into a mesh the
// runtime reads, its solids' faces in it; one that does not read, or whose
// sidecar gives it settings, refused.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/blockout.h"
#include "rawframe/mesh/mesh.h"

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(ABlockoutIsCookedIntoAMesh) {
    const Project kProject;
    const fs::path kLevel = kProject.sources / "level";
    writeText(kLevel / "yard.blockout",
              "blockout 1\nbox floor at 0 0 0 size 8 0.5 8\ncut box pit at 0 -1 0 size 2 2 2\n");
    writeText(kLevel / "yard.blockout.rfmeta", sidecar("000000000000000000000000000000d1", "", "rawframe.blockout"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), blockoutImporter()};
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
        if (each.id.value != base::Bits128{0, 0xd1}) {
            continue;
        }
        found = each.type.value == mesh::kMeshType && each.representation.text() == mesh::kMeshRepresentation;
        // Read as the runtime reads it: the floor with a pit through it,
        // more than a box's twelve triangles, in one part.
        const std::string kBytes = readText(kProject.output / each.locator);
        const auto kMesh = mesh::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
        RAWFRAME_EXPECT(kMesh.has_value() && kMesh->parts.size() == 1 && kMesh->indices.size() > 36);
    }
    RAWFRAME_EXPECT(found);
    // A line that is not a solid, and a blockout given settings: refused.
    writeText(kLevel / "yard.blockout", "blockout 1\nsphere ball at 0 0 0\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kLevel / "yard.blockout", "blockout 1\nbox floor at 0 0 0 size 8 0.5 8\n");
    writeText(kLevel / "yard.blockout.rfmeta",
              sidecar("000000000000000000000000000000d1", "\"exact\": true", "rawframe.blockout"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
