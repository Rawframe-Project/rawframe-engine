// Images, Radiance pictures, and grading tables cooked into textures as
// their sidecars' settings say (D253, D321, D344): a grading table into a
// volume, refused with other settings or for a file that is not one.

#include "fixture.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/texture.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <span>
#include <vector>

using namespace rawframe;
using namespace rawframe::cook;
using namespace rawframe::cook_fixture;

RAWFRAME_TEST(AnImageCooksIntoATextureAsItsSettingsSay) {
    const Project kProject;
    const fs::path kImages = kProject.sources / "images";
    fs::create_directories(kImages);
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "pattern.png", kImages / "sprite.png");
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "pattern.tga", kImages / "mask.tga");
    writeText(kImages / "sprite.png.rfmeta", sidecar("000000000000000000000000000000b1", "", "rawframe.texture"));
    writeText(kImages / "mask.tga.rfmeta",
              sidecar("000000000000000000000000000000b2",
                      "\"color\": \"linear\",\n    \"exact\": true,\n    \"levels\": false",
                      "rawframe.texture"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), textureImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kReport = kCook();
    RAWFRAME_EXPECT(kReport.cooked == 4 && kReport.failures.empty());
    std::vector<texture::Texture> textures;
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.type.value == texture::kTextureType && each.representation.text() == texture::kTextureRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            auto read = texture::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            RAWFRAME_EXPECT(read.has_value());
            if (read.has_value()) {
                textures.push_back(std::move(*read));
            }
        }
    }
    // The sprite as BC7 in sRGB with every level; the mask exact, linear,
    // and alone.
    RAWFRAME_EXPECT(textures.size() == 2 &&
                    std::ranges::any_of(textures,
                                        [](const texture::Texture& each) {
                                            return each.format == texture::Format::Bc7Srgb && each.levels.size() == 4;
                                        }) &&
                    std::ranges::any_of(textures, [](const texture::Texture& each) {
                        return each.format == texture::Format::Rgba8 && each.levels.size() == 1;
                    }));
    // A default written out, or a color neither, fails its sidecar.
    writeText(kImages / "mask.tga.rfmeta",
              sidecar("000000000000000000000000000000b2", "\"color\": \"srgb\"", "rawframe.texture"));
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    writeText(kImages / "mask.tga.rfmeta",
              sidecar("000000000000000000000000000000b2", "\"color\": \"rgb\"", "rawframe.texture"));
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
}

RAWFRAME_TEST(ARadiancePictureCooksIntoAnEnvironment) {
    const Project kProject;
    const fs::path kSkies = kProject.sources / "skies";
    fs::create_directories(kSkies);
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "../seeds/radiance/runs.hdr", kSkies / "dusk.hdr");
    const auto kSidecar = [&kSkies](std::string_view settings) {
        writeText(kSkies / "dusk.hdr.rfmeta",
                  sidecar("000000000000000000000000000000c1", settings, "rawframe.texture"));
    };
    kSidecar("\"environment\": {\n      \"side\": 8,\n      \"levels\": 3,\n      \"samples\": 16\n    }");
    static const std::array<Importer, 2> kImporters = {audioImporter(), textureImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kReport = kCook();
    RAWFRAME_EXPECT(kReport.cooked == 3 && kReport.failures.empty());
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    std::size_t cubes = 0;
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.type.value == texture::kTextureType) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kCube = texture::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            // A cube of half floats, its three levels three roughnesses.
            cubes += kCube.has_value() && kCube->format == texture::Format::Rgba16Float && kCube->faces == 6 &&
                     kCube->levels.size() == 3 && kCube->levels[0].width == 8;
        }
    }
    RAWFRAME_EXPECT(cubes == 1);
    // A Radiance picture is only an environment, and an environment only a
    // Radiance picture.
    kSidecar("");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "pattern.png", kSkies / "flat.png");
    writeText(kSkies / "flat.png.rfmeta",
              sidecar("000000000000000000000000000000c2", R"("environment": {})", "rawframe.texture"));
    kSidecar("\"environment\": {\n      \"side\": 8,\n      \"levels\": 3,\n      \"samples\": 16\n    }");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
    fs::remove(kSkies / "flat.png.rfmeta");
    fs::remove(kSkies / "flat.png");
    // An environment takes no color; its defaults are omitted; its side is
    // a power of two.
    kSidecar("\"color\": \"linear\",\n    \"environment\": {}");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
    kSidecar("\"environment\": {\n      \"side\": 128\n    }");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    kSidecar("\"environment\": {\n      \"side\": 12\n    }");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}

RAWFRAME_TEST(AGradingTableCooksIntoAVolume) {
    const Project kProject;
    const fs::path kGrades = kProject.sources / "grades";
    fs::create_directories(kGrades);
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "../seeds/grading/swap.cube", kGrades / "swap.cube");
    const auto kSidecar = [&kGrades](std::string_view settings) {
        writeText(kGrades / "swap.cube.rfmeta",
                  sidecar("000000000000000000000000000000d1", settings, "rawframe.texture"));
    };
    kSidecar("\"grading\": true");
    static const std::array<Importer, 2> kImporters = {audioImporter(), textureImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kReport = kCook();
    RAWFRAME_EXPECT(kReport.cooked == 3 && kReport.failures.empty());
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    std::size_t volumes = 0;
    for (const content::ManifestEntry& each : kManifest.value_or(std::vector<content::ManifestEntry>{})) {
        if (each.type.value == texture::kTextureType) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kTable = texture::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            volumes += kTable.has_value() && kTable->format == texture::Format::Rgba16Float && kTable->depth == 2 &&
                       kTable->levels.size() == 1;
        }
    }
    RAWFRAME_EXPECT(volumes == 1);
    // A grading table takes no other setting, and its default is omitted;
    // an image is not one.
    kSidecar("\"color\": \"linear\",\n    \"grading\": true");
    RAWFRAME_EXPECT(failedWith(kCook(), CookError::BadSidecar));
    kSidecar("\"grading\": false");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
    fs::remove(kGrades / "swap.cube");
    fs::copy_file(fs::path{RAWFRAME_TEXTURE_DATA} / "pattern.png", kGrades / "swap.cube");
    kSidecar("\"grading\": true");
    RAWFRAME_EXPECT(kCook().failures.size() == 1);
}
