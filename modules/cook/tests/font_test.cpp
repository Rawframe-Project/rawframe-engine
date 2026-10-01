// Fonts cooked through the sanitizer (ADR-0078, D385): a font cooks into the
// rebuilt font a tree reads, and a file that is not one, or a web font, fails
// with the sanitizer's refusal; a font takes no settings.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/audio.h"
#include "rawframe/cook/font.h"
#include "rawframe/font_import/errors.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/font.h"
#include "rawframe/ui/tree.h"

#include <algorithm>
#include <span>
#include <vector>

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(AFontCooksIntoTheFontItsSanitizerRebuilt) {
    const Project kProject;
    const fs::path kFonts = kProject.sources / "fonts";
    fs::create_directories(kFonts);
    fs::copy_file(fs::path{RAWFRAME_UI_FONTS} / "Ahem.ttf", kFonts / "boxes.ttf");
    writeText(kFonts / "boxes.ttf.rfmeta", sidecar("000000000000000000000000000000f1", "", "rawframe.font"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), fontImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kReport = kCook();
    // The project's two sounds, and the font.
    RAWFRAME_EXPECT(kReport.cooked == 3 && kReport.failures.empty());
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    if (!kManifest.has_value()) {
        return;
    }
    const auto kEntry = std::ranges::find_if(*kManifest, [](const content::ManifestEntry& each) {
        return each.type.value == ui::kFontType;
    });
    if (kEntry == kManifest->end()) {
        RAWFRAME_EXPECT(false);
        return;
    }
    RAWFRAME_EXPECT(kEntry->representation.text() == ui::kFontRepresentation);
    const std::string kCooked = readText(kProject.output / kEntry->locator);
    const std::string kSource = readText(kFonts / "boxes.ttf");
    const auto kRebuilt = font_import::sanitize(std::as_bytes(std::span{kSource.data(), kSource.size()}));
    RAWFRAME_EXPECT(kRebuilt.has_value() &&
                    std::ranges::equal(std::as_bytes(std::span{kCooked.data(), kCooked.size()}), *kRebuilt));
    auto tree = ui::Tree::create(1, 1);
    RAWFRAME_EXPECT(tree.has_value() &&
                    (*tree)->addFont(std::as_bytes(std::span{kCooked.data(), kCooked.size()})).has_value());

    // Settings are refused; so is a web font, by the sanitizer.
    writeText(kFonts / "boxes.ttf.rfmeta", sidecar("000000000000000000000000000000f1", "\"face\": 0", "rawframe.font"));
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
    writeText(kFonts / "boxes.ttf.rfmeta", sidecar("000000000000000000000000000000f1", "", "rawframe.font"));
    std::string web = kSource;
    web.replace(0, 4, "wOF2");
    writeText(kFonts / "boxes.ttf", web);
    const CookReport kWeb = kCook();
    RAWFRAME_EXPECT(kWeb.failures.size() == 1 && std::ranges::any_of(kWeb.failures, [](const result::Error& each) {
                        return each.domain() == font_import::kFontImportDomain &&
                               each.code() == code(font_import::FontImportError::Unsupported);
                    }));
}
