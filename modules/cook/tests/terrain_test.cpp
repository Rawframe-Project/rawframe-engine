// The terrain importer (D854): a terrain document cooked into a mesh the
// runtime reads, a part a layer painted drawn with its layer's material;
// one that does not read, or whose sidecar gives it settings, refused.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/terrain.h"
#include "rawframe/mesh/mesh.h"

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(ATerrainIsCookedIntoAMesh) {
    const Project kProject;
    const fs::path kGround = kProject.sources / "ground";
    writeText(kGround / "hill.terrain",
              "terrain 1\ngrid 3 3\ncell 4\nlayer grass 00000000000000a1\nlayer rock 00000000000000b2\n"
              "heights\n0 0 0\n0 2 0\n0 0 0\nlayers\n01\n00\n");
    writeText(kGround / "hill.terrain.rfmeta", sidecar("000000000000000000000000000000e1", "", "rawframe.terrain"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), terrainImporter()};
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
        if (each.id.value != base::Bits128{0, 0xe1}) {
            continue;
        }
        found = each.type.value == mesh::kMeshType && each.representation.text() == mesh::kMeshRepresentation;
        // Read as the runtime reads it: eight triangles, the grass's six
        // and the rock's two, each part its layer's material.
        const std::string kBytes = readText(kProject.output / each.locator);
        const auto kMesh = mesh::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
        RAWFRAME_EXPECT(kMesh.has_value() && kMesh->indices.size() == 24 && kMesh->parts.size() == 2);
        RAWFRAME_EXPECT(kMesh.has_value() && kMesh->parts.size() == 2 && kMesh->parts[0].material == 0xa1 &&
                        kMesh->parts[0].indexCount == 18 && kMesh->parts[1].material == 0xb2);
    }
    RAWFRAME_EXPECT(found);
    // A height that is no number, and a terrain given settings: refused.
    writeText(kGround / "hill.terrain", "terrain 1\ngrid 2 2\nheights\n0 0\n0 high\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kGround / "hill.terrain", "terrain 1\ngrid 2 2\nheights\n0 0\n0 0\n");
    writeText(kGround / "hill.terrain.rfmeta",
              sidecar("000000000000000000000000000000e1", "\"exact\": true", "rawframe.terrain"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
