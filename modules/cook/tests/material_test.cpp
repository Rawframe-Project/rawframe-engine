// Materials cooked: a surface material into its
// compiled form, its qualities its variants, and a post process and a
// canvas material into their folded forms.

#include "fixture.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/manifest.h"
#include "rawframe/cook/cook.h"
#include "rawframe/cook/errors.h"
#include "rawframe/cook/material.h"
#include "rawframe/material/canvas.h"
#include "rawframe/material/instance.h"
#include "rawframe/material/material.h"
#include "rawframe/material/post_process.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::cook;

using namespace rawframe::cook_fixture;

RAWFRAME_TEST(AMaterialsQualitiesAreItsVariants) {
    // Plaza's paving, a plain grey at the low quality: two variants,
    // the second the quality axis's.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_SAMPLE_GAMES} / "plaza" / "paving.rfmaterial",
                  kProject.sources / "paving.rfmaterial");
    writeText(kProject.sources / "paving.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c2", "", "rawframe.material"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), materialImporter()};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kImporters});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty() && kReport->variants == 2);
    RAWFRAME_EXPECT(readText(kProject.output / "cook.receipt")
                        .find("\"variants\": 2,\n        \"axes\": 1,\n        \"qualityCardinality\": 3,\n        "
                              "\"qualityVariants\": 1,\n        \"variantsHeadroom\": 1") != std::string::npos);
}

RAWFRAME_TEST(APostProcessCooksIntoItsFoldedForm) {
    // Plaza's warmth: the picture tinted before the tonemapper.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_SAMPLE_GAMES} / "plaza" / "warmth.rfmaterial",
                  kProject.sources / "warmth.rfmaterial");
    writeText(kProject.sources / "warmth.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c3", "", "rawframe.postprocess"));
    static const std::array<Importer, 3> kImporters = {audioImporter(), materialImporter(), postProcessImporter()};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kImporters});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty() && kReport->variants == 1);
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    bool found = false;
    for (const content::ManifestEntry& each :
         kManifest.has_value() ? *kManifest : std::vector<content::ManifestEntry>{}) {
        if (each.type.value == material::kPostProcessType &&
            each.representation.text() == material::kPostProcessRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kRead = material::decodePostProcess(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            found = kRead.has_value() && kRead->insertion == material::Insertion::BeforeTonemap &&
                    (kRead->scene == std::array<float, 3>{1, 0.97F, 0.92F});
        }
    }
    RAWFRAME_EXPECT(found);
    // A surface material's sidecar cannot cook it.
    writeText(kProject.sources / "warmth.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c3", "", "rawframe.material"));
    const auto kWrong =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kImporters});
    RAWFRAME_EXPECT(kWrong.has_value() && kWrong->failures.size() == 1);
}

RAWFRAME_TEST(ACanvasMaterialCooksIntoItsFoldedForm) {
    // A sheet's texture tinted red and added, glowing faintly.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_MATERIAL_SEEDS} / "glowing.canvas", kProject.sources / "glow.rfmaterial");
    writeText(kProject.sources / "glow.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c4", "", "rawframe.canvasmaterial"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), canvasImporter()};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kImporters});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty() && kReport->variants == 1);
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    bool found = false;
    for (const content::ManifestEntry& each :
         kManifest.has_value() ? *kManifest : std::vector<content::ManifestEntry>{}) {
        if (each.type.value == material::kCanvasMaterialType &&
            each.representation.text() == material::kCanvasMaterialRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kRead = material::decodeCanvas(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            found = kRead.has_value() && kRead->blend == material::CanvasBlend::Additive &&
                    (kRead->colorTexture == std::array<float, 4>{1, 0.5F, 0.5F, 1});
        }
    }
    RAWFRAME_EXPECT(found);
}

RAWFRAME_TEST(ASurfaceMaterialCooksIntoItsCompiledForm) {
    const Project kProject;
    const fs::path kLooks = kProject.sources / "looks";
    material::Material brass{.doubleSided = true};
    brass.surface.baseColor = {0.9F, 0.7F, 0.3F};
    brass.surface.baseMetalness = 1;
    brass.surface.specularRoughness = 0.4F;
    const auto kText = material::writeMaterial(material::documentOf(brass, 0x2f00000000000002ULL));
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    writeText(kLooks / "brass.rfmaterial", *kText);
    writeText(kLooks / "brass.rfmaterial.rfmeta", sidecar("000000000000000000000000000000c1", "", "rawframe.material"));
    static const std::array<Importer, 2> kImporters = {audioImporter(), materialImporter()};
    const auto kCook = [&kProject] {
        auto report = cookSources(CookRequest{
            .sources = kProject.sources, .output = kProject.output, .cache = kProject.cache, .importers = kImporters});
        RAWFRAME_EXPECT(report.has_value());
        return report.has_value() ? std::move(*report) : CookReport{};
    };
    const CookReport kFirst = kCook();
    RAWFRAME_EXPECT(kFirst.failures.empty());
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    RAWFRAME_EXPECT(kManifest.has_value());
    bool found = false;
    for (const content::ManifestEntry& each :
         kManifest.has_value() ? *kManifest : std::vector<content::ManifestEntry>{}) {
        if (each.type.value == material::kMaterialType &&
            each.representation.text() == material::kMaterialRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kRead = material::decode(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            found = kRead.has_value() && *kRead == (material::Qualities{brass, brass, brass});
        }
    }
    RAWFRAME_EXPECT(found);
    // The variant report: one variant, from no axis, in the
    // receipt beside its source and for the package; from the cache too.
    const auto kReported = [&kProject] {
        const std::string kReceipt = readText(kProject.output / "cook.receipt");
        return kReceipt.find("\"report\": {\n        \"variants\": 1,\n        \"axes\": 0,\n        "
                             "\"qualityCardinality\": 3,\n        \"qualityVariants\": 0,\n        "
                             "\"variantsHeadroom\": 2\n      }") != std::string::npos &&
               kReceipt.find("\"variants\": 1,\n  \"variantsHeadroom\": 4095,") != std::string::npos;
    };
    RAWFRAME_EXPECT(kFirst.variants == 1 && kReported());
    const CookReport kAgain = kCook();
    RAWFRAME_EXPECT(kAgain.reused == 3 && kAgain.variants == 1 && kReported());
    // A package past its ceiling fails visibly.
    const auto kCapped = cookSources(CookRequest{.sources = kProject.sources,
                                                 .output = kProject.output,
                                                 .cache = kProject.cache,
                                                 .importers = kImporters,
                                                 .maximumVariants = 0});
    RAWFRAME_EXPECT(kCapped.has_value() && failedWith(*kCapped, CookError::OverLimit));
    // A material that does not compile fails, visibly: here, an input
    // connected round to its own node.
    std::string connected = *kText;
    connected.replace(
        connected.find("\"base_metalness\": 1"),
        19,
        "\"base_metalness\": {\n          \"node\": \"2f00000000000002\",\n          \"output\": \"out\"\n        }");
    writeText(kLooks / "brass.rfmaterial", connected);
    const CookReport kRefused = kCook();
    RAWFRAME_EXPECT(kRefused.failures.size() == 1);
}

namespace {

/// The shader toolchain as the check gives it: a Python on the path
/// and the pinned Slang compiler named; none where either is missing.
std::optional<ShaderTools> shaderTools() {
    const char* kSlangc = std::getenv("RAWFRAME_SLANGC");
    const char* kPath = std::getenv("PATH");
    if (kSlangc == nullptr || kPath == nullptr || !fs::exists(kSlangc)) {
        return std::nullopt;
    }
    std::string_view path{kPath};
    while (!path.empty()) {
        const std::size_t kEnd = std::min(path.find(':'), path.size());
        const fs::path kPython = fs::path{std::string{path.substr(0, kEnd)}} / "python3";
        if (fs::exists(kPython)) {
            return ShaderTools{.python = kPython,
                               .generator = RAWFRAME_SHADER_GENERATOR,
                               .identity = base::sha256(readText(RAWFRAME_SHADER_GENERATOR))};
        }
        path.remove_prefix(std::min(kEnd + 1, path.size()));
    }
    return std::nullopt;
}

} // namespace

RAWFRAME_TEST(AGraphTheBlobCannotFoldCooksWithItsOwnProgram) {
    // A texture's color faded by its own alpha, its roughness that alpha:
    // no blob says it, so it is a program material.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "faded.material", kProject.sources / "faded.rfmaterial");
    writeText(kProject.sources / "faded.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c3", "", "rawframe.material"));
    // And one lifting its vertices with time, which says it moves.
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "rising.material", kProject.sources / "rising.rfmaterial");
    writeText(kProject.sources / "rising.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c4", "", "rawframe.material"));
    // And one using every math node of the core, which the
    // toolchain builds for every backend.
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "arithmetic.material", kProject.sources / "arithmetic.rfmaterial");
    writeText(kProject.sources / "arithmetic.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c5", "", "rawframe.material"));
    // And one whose color is its tint parameter's, its default
    // carried by its program.
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "tinted.material", kProject.sources / "tinted.rfmaterial");
    writeText(kProject.sources / "tinted.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c6", "", "rawframe.material"));
    // And an instance of it, red: no program of its own, so it cooks
    // with or without the toolchain, to its base's identity and its tint.
    writeText(kProject.sources / "reddened.rfmaterial",
              "{\n  \"formatVersion\": 1,\n  \"kind\": \"surface\",\n  \"base\": \"tinted.rfmaterial\",\n"
              "  \"overrides\": {\n    \"tint\": [\n      1,\n      0,\n      0\n    ]\n  }\n}\n");
    writeText(kProject.sources / "reddened.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c7", "", "rawframe.material"));
    // And a clear glass, which transmits.
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "glass.material", kProject.sources / "glass.rfmaterial");
    writeText(kProject.sources / "glass.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000c8", "", "rawframe.material"));
    // Without the shader toolchain they are refused, saying it is needed.
    const std::array<Importer, 2> kBare = {audioImporter(), materialImporter()};
    const auto kRefused =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kBare});
    RAWFRAME_EXPECT(kRefused.has_value() && kRefused->failures.size() == 5 &&
                    kRefused->failures[0].code() == code(CookError::ToolFailed));
    const std::optional<ShaderTools> kTools = shaderTools();
    if (!kTools.has_value()) {
        // The check requires it where it is installed.
        RAWFRAME_EXPECT(std::getenv("RAWFRAME_REQUIRE_SHADER_TOOLCHAIN") == nullptr);
        std::puts("skip: no shader toolchain");
        return;
    }
    const std::array<Importer, 2> kTooled = {audioImporter(), materialImporter(kTools)};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kTooled});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty() && kReport->variants == 5);
    if (kReport.has_value() && !kReport->failures.empty()) {
        std::printf("%s\n", std::string{kReport->failures[0].description()}.c_str());
    }
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    bool found = false;
    bool rising = false;
    bool tinted = false;
    bool reddened = false;
    bool transmitting = false;
    for (const content::ManifestEntry& each :
         kManifest.has_value() ? *kManifest : std::vector<content::ManifestEntry>{}) {
        if (each.type.value == material::kMaterialType &&
            each.representation.text() == material::kInstanceRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kRead = material::decodeInstance(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            reddened = kRead.has_value() && kRead->base == base::Bits128{.high = 0, .low = 0xc6} &&
                       kRead->parameters[0] == 1 && kRead->parameters[1] == 0 && kRead->parameters[2] == 0;
        }
        if (each.type.value == material::kMaterialType &&
            each.representation.text() == material::kProgramRepresentation) {
            const std::string kBytes = readText(kProject.output / each.locator);
            const auto kRead = material::decodeProgram(std::as_bytes(std::span{kBytes.data(), kBytes.size()}));
            found = found || (kRead.has_value() && kRead->textures.size() == 1 &&
                              kRead->textures[0].id == 0xa44ecb4a39ac5cc8ULL &&
                              kRead->shading == material::Shading::Lit && !kRead->moves);
            rising = rising || (kRead.has_value() && kRead->textures.empty() && kRead->moves);
            transmitting = transmitting || (kRead.has_value() && kRead->transmits && !kRead->moves);
            tinted = tinted || (kRead.has_value() && kRead->parameters[0] == 0 && kRead->parameters[1] == 1 &&
                                kRead->parameters[2] == 0 && kRead->shading == material::Shading::Unlit);
            std::printf("program material: %zu bytes, containers %zu %zu %zu, shadows %zu %zu %zu\n",
                        kBytes.size(),
                        kRead.has_value() ? kRead->containers[0].size() : 0,
                        kRead.has_value() ? kRead->containers[1].size() : 0,
                        kRead.has_value() ? kRead->containers[2].size() : 0,
                        kRead.has_value() ? kRead->shadows[0].size() : 0,
                        kRead.has_value() ? kRead->shadows[1].size() : 0,
                        kRead.has_value() ? kRead->shadows[2].size() : 0);
        }
    }
    RAWFRAME_EXPECT(found && rising && tinted && reddened && transmitting);
}

RAWFRAME_TEST(AnInstanceCooksOnlyAgainstABaseItFits) {
    // An instance's base is read where it names it, so a base that is
    // not there, an override the base has no input for, and an instance of
    // an instance are each refused, and the rest cook.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "tinted.material", kProject.sources / "tinted.rfmaterial");
    writeText(kProject.sources / "tinted.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000d1", "", "rawframe.material"));
    const auto kInstance =
        [&kProject](std::string_view name, std::string_view base, std::string_view input, std::string_view id) {
            writeText(kProject.sources / name,
                      "{\n  \"formatVersion\": 1,\n  \"kind\": \"surface\",\n  \"base\": \"" + std::string{base} +
                          "\",\n  \"overrides\": {\n    \"" + std::string{input} +
                          "\": [\n      1,\n      0,\n      0\n    ]\n  }\n}\n");
            writeText(kProject.sources / (std::string{name} + ".rfmeta"), sidecar(id, "", "rawframe.material"));
        };
    kInstance("red.rfmaterial", "tinted.rfmaterial", "tint", "000000000000000000000000000000d2");
    kInstance("lost.rfmaterial", "gone.rfmaterial", "tint", "000000000000000000000000000000d3");
    kInstance("shaded.rfmaterial", "tinted.rfmaterial", "shade", "000000000000000000000000000000d4");
    kInstance("again.rfmaterial", "red.rfmaterial", "tint", "000000000000000000000000000000d5");
    // The base itself needs the toolchain; the instances never do.
    const std::array<Importer, 2> kBare = {audioImporter(), materialImporter()};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kBare});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.size() == 4);
    std::vector<std::string> failed;
    if (kReport.has_value()) {
        for (const result::Error& each : kReport->failures) {
            failed.emplace_back(each.description());
        }
    }
    const auto kSays = [&failed](std::string_view words) {
        return std::ranges::any_of(failed, [words](const std::string& each) {
            return each.contains(words);
        });
    };
    RAWFRAME_EXPECT(kSays("shader toolchain") && kSays("never another instance") &&
                    kSays("interface inputs and switches alone") && failed.size() == 4);
    for (const std::string& each : failed) {
        std::printf("refused: %s\n", each.c_str());
    }
}

RAWFRAME_TEST(AnInstanceSwitchingItsBaseCooksItsOwnProgram) {
    // soaked.material is sandy unless its wet switch is on, when its
    // tint shows. An instance turning it on draws a variant of its own, a
    // program with the instance's tint; one changing the tint alone shares
    // its base's program.
    const Project kProject;
    fs::copy_file(fs::path{RAWFRAME_SURFACE_SEEDS} / "soaked.material", kProject.sources / "soaked.rfmaterial");
    writeText(kProject.sources / "soaked.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000e1", "", "rawframe.material"));
    writeText(kProject.sources / "drenched.rfmaterial",
              "{\n  \"formatVersion\": 1,\n  \"kind\": \"surface\",\n  \"base\": \"soaked.rfmaterial\",\n"
              "  \"overrides\": {\n    \"tint\": [\n      1,\n      0,\n      0\n    ],\n    \"wet\": true\n  }\n}\n");
    writeText(kProject.sources / "drenched.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000e2", "", "rawframe.material"));
    writeText(kProject.sources / "reddish.rfmaterial",
              "{\n  \"formatVersion\": 1,\n  \"kind\": \"surface\",\n  \"base\": \"soaked.rfmaterial\",\n"
              "  \"overrides\": {\n    \"tint\": [\n      1,\n      0,\n      0\n    ]\n  }\n}\n");
    writeText(kProject.sources / "reddish.rfmaterial.rfmeta",
              sidecar("000000000000000000000000000000e3", "", "rawframe.material"));
    // Without the toolchain, the base and the switched instance wait for it.
    const std::array<Importer, 2> kBare = {audioImporter(), materialImporter()};
    const auto kRefused =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kBare});
    RAWFRAME_EXPECT(kRefused.has_value() && kRefused->failures.size() == 2);
    const std::optional<ShaderTools> kTools = shaderTools();
    if (!kTools.has_value()) {
        RAWFRAME_EXPECT(std::getenv("RAWFRAME_REQUIRE_SHADER_TOOLCHAIN") == nullptr);
        return;
    }
    const std::array<Importer, 2> kTooled = {audioImporter(), materialImporter(*kTools)};
    const auto kReport =
        cookSources(CookRequest{.sources = kProject.sources, .output = kProject.output, .importers = kTooled});
    RAWFRAME_EXPECT(kReport.has_value() && kReport->failures.empty());
    // Two programs, the base's sandy default and the drenched variant with
    // its red tint, and one instance, sharing the base's.
    const auto kManifest = content::readManifest(readText(kProject.output / "content.manifest"));
    std::size_t programs = 0;
    std::size_t instances = 0;
    bool drenched = false;
    for (const content::ManifestEntry& each :
         kManifest.has_value() ? *kManifest : std::vector<content::ManifestEntry>{}) {
        const std::string kBytes = readText(kProject.output / each.locator);
        const auto kAsBytes = std::as_bytes(std::span{kBytes.data(), kBytes.size()});
        if (each.representation.text() == material::kInstanceRepresentation) {
            instances += material::decodeInstance(kAsBytes).has_value() ? 1 : 0;
        }
        if (each.representation.text() == material::kProgramRepresentation) {
            const auto kRead = material::decodeProgram(kAsBytes);
            programs += kRead.has_value() ? 1 : 0;
            drenched = drenched || (kRead.has_value() && kRead->parameters[0] == 1 && kRead->parameters[1] == 0);
        }
    }
    RAWFRAME_EXPECT(programs == 2 && instances == 1 && drenched);
}
