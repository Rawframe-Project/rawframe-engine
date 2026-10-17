// The sequence importer (D869): a sequence document cooked into a resource
// of its own, its text as written; one that does not read, or whose
// sidecar gives it settings, refused.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/sequence.h"
#include "rawframe/sequence/sequence.h"

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(ASequenceIsCookedAsItWasWritten) {
    const Project kProject;
    const fs::path kLevel = kProject.sources / "level";
    const std::string kText = "sequence 1\nduration 4\ntrack gate\nplay 1 gate 5a1e6c0d2b9f8374\n"
                              "event 2 5f3a0c2d9e81b746 opened\n";
    writeText(kLevel / "opening.sequence", kText);
    writeText(kLevel / "opening.sequence.rfmeta", sidecar("000000000000000000000000000000f2", "", "rawframe.sequence"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), sequenceImporter()};
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
        if (each.id.value == base::Bits128{0, 0xf2}) {
            found = each.type.value == sequence::kSequenceType &&
                    each.representation.text() == sequence::kSequenceRepresentation &&
                    readText(kProject.output / each.locator) == kText;
        }
    }
    RAWFRAME_EXPECT(found);
    // A key past the duration, and a sequence given settings: refused.
    writeText(kLevel / "opening.sequence", "sequence 1\nduration 4\ntrack gate\nplay 5 gate 5a1e6c0d2b9f8374\n");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kLevel / "opening.sequence", kText);
    writeText(kLevel / "opening.sequence.rfmeta",
              sidecar("000000000000000000000000000000f2", "\"exact\": true", "rawframe.sequence"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
