#include "rawframe/cook/material.h"

#include "rawframe/cook/errors.h"
#include "rawframe/material/canvas.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/material/post_process.h"
#include "rawframe/process/child.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace rawframe::cook {

namespace {

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           kCookDomain,
                                                           code(CookError::BadSidecar),
                                                           "a material takes no settings")
                                                  .error()};
    }
    return std::string{};
}

/// SPEC-0026's `variants_per_material`: the quality axis's three, the one
/// axis generation 1 has (D319).
constexpr std::int64_t kVariantsPerMaterial = 3;

/// SPEC-0026's variant report (D319): the variants, the axes that make
/// them and the quality axis's cardinality, the variants the quality axis
/// adds, and the headroom under the ceiling. A variant is a distinct
/// material: generation 1 draws every one with the one program.
std::vector<std::pair<std::string, std::int64_t>> reportOf(const material::Qualities& made) {
    std::int64_t variants = 1;
    for (std::size_t at = 1; at < made.size(); ++at) {
        const bool kSeen =
            std::ranges::find(made.begin(), made.begin() + static_cast<std::ptrdiff_t>(at), made.at(at)) !=
            made.begin() + static_cast<std::ptrdiff_t>(at);
        variants += kSeen ? 0 : 1;
    }
    return {{"variants", variants},
            {"axes", variants > 1 ? 1 : 0},
            {"qualityCardinality", static_cast<std::int64_t>(made.size())},
            {"qualityVariants", variants - 1},
            {"variantsHeadroom", kVariantsPerMaterial - variants}};
}

std::unexpected<result::Error> toolFailed(std::string why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::FailedPrecondition, kCookDomain, code(CookError::ToolFailed), why).error()};
}

/// A directory of its own under the system's temporary one, made afresh
/// and open to its owner alone: a random name no one else can guess or
/// hold first, so nothing another user put there is read or written. What
/// the toolchain builds does not depend on where (the two cooks of one
/// source build in two places and are compared).
result::Result<std::filesystem::path> freshDirectory() {
    std::random_device random;
    std::error_code failed;
    const std::filesystem::path kTemporary = std::filesystem::temp_directory_path(failed);
    for (std::size_t each = 0; !failed && each < 100; ++each) {
        const std::uint64_t kName = (std::uint64_t{random()} << 32U) | std::uint64_t{random()};
        std::string named = "rawframe-material-";
        for (std::size_t at = 0; at < 16; ++at) {
            named += "0123456789abcdef"[(kName >> (60 - (4 * at))) & 0xFU];
        }
        const std::filesystem::path kMade = kTemporary / named;
        if (std::filesystem::create_directory(kMade, failed)) {
            std::filesystem::permissions(kMade, std::filesystem::perms::owner_all, failed);
            if (failed) {
                std::error_code ignored;
                std::filesystem::remove(kMade, ignored);
                break;
            }
            return kMade;
        }
    }
    return toolFailed("no directory of its own could be made to build a program material in");
}

std::vector<std::byte> bytesOf(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    const std::vector<char> kRead{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> made(kRead.size());
    std::ranges::transform(kRead, made.begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return made;
}

/// A graph the blob cannot fold, written as Slang and linked into the
/// scene's containers by the shader toolchain (D484): a program material,
/// one variant at the high quality.
result::Result<Artifact> cookProgram(const graph::Document& document, const ShaderTools& tools) {
    RAWFRAME_TRY_ASSIGN(const material::GeneratedSlang kGenerated, material::generateSlang(document));
    RAWFRAME_TRY_ASSIGN(const std::filesystem::path kWork, freshDirectory());
    const auto kRemove = [&kWork] {
        std::error_code ignored;
        std::filesystem::remove_all(kWork, ignored);
    };
    {
        std::ofstream written{kWork / "program.slang", std::ios::binary};
        written << kGenerated.source;
    }
    auto child = process::Child::start(process::ChildSettings{
        .program = tools.python,
        .arguments = {tools.generator.string(), "--material", (kWork / "program.slang").string(), kWork.string()},
        .output = kWork / "toolchain.log"});
    if (!child.has_value()) {
        kRemove();
        return toolFailed("the shader toolchain did not start: " + std::string{child.error().description()});
    }
    std::optional<int> exited;
    while (!(exited = child->exited()).has_value()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    if (*exited != 0) {
        const std::vector<std::byte> kLog = bytesOf(kWork / "toolchain.log");
        kRemove();
        return toolFailed(
            "the shader toolchain failed building a program material: " +
            std::string{reinterpret_cast<const char*>(kLog.data()), std::min<std::size_t>(kLog.size(), 2000)});
    }
    material::ProgramMaterial made{.shading = kGenerated.shading,
                                   .blend = kGenerated.blend,
                                   .alphaCutoff = kGenerated.alphaCutoff,
                                   .doubleSided = kGenerated.doubleSided,
                                   .textures = kGenerated.textures};
    constexpr std::array<std::string_view, material::kProgramContainers> kNames = {
        "scene.mrsc", "scene.metal.mrsc", "scene.d3d12.mrsc"};
    for (std::size_t at = 0; at < kNames.size(); ++at) {
        made.containers.at(at) = bytesOf(kWork / kNames.at(at));
    }
    kRemove();
    std::vector<std::byte> bytes = material::encodeProgram(made);
    // What the toolchain wrote, checked as a runtime reads it.
    RAWFRAME_TRY(material::decodeProgram(bytes));
    return Artifact{.type = content::ResourceTypeId{material::kMaterialType},
                    .representation = *content::RepresentationId::parse(material::kProgramRepresentation),
                    .bytes = std::move(bytes),
                    .subassets = {},
                    .report = {{"variants", 1}, {"axes", 0}, {"variantsHeadroom", kVariantsPerMaterial - 1}}};
}

result::Result<Artifact> cookMaterial(std::span<const std::byte> source, const std::optional<ShaderTools>& tools) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readMaterial(kText));
    auto compiled = material::compileQualities(kDocument);
    if (compiled.has_value()) {
        return Artifact{.type = content::ResourceTypeId{material::kMaterialType},
                        .representation = *content::RepresentationId::parse(material::kMaterialRepresentation),
                        .bytes = material::encode(*compiled),
                        .subassets = {},
                        .report = reportOf(*compiled)};
    }
    // What the blob cannot fold, a program says (D484).
    if (compiled.error().domain() != material::kMaterialDomain ||
        compiled.error().code() != material::code(material::MaterialError::Unsupported)) {
        return std::unexpected<result::Error>{std::move(compiled).error()};
    }
    if (!tools.has_value()) {
        return toolFailed("this material's graph needs its own program, which the shader toolchain builds, and the "
                          "cook was given none: " +
                          std::string{compiled.error().description()});
    }
    return cookProgram(kDocument, *tools);
}

/// A post process folds to one form (D348): one variant.
result::Result<Artifact> cookPostProcess(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readPostProcess(kText));
    RAWFRAME_TRY_ASSIGN(const material::PostProcess kCompiled, material::compilePostProcess(kDocument));
    return Artifact{.type = content::ResourceTypeId{material::kPostProcessType},
                    .representation = *content::RepresentationId::parse(material::kPostProcessRepresentation),
                    .bytes = material::encodePostProcess(kCompiled),
                    .subassets = {},
                    .report = {{"variants", 1}, {"axes", 0}, {"variantsHeadroom", kVariantsPerMaterial - 1}}};
}

/// A canvas material folds to one form (D355): one variant.
result::Result<Artifact> cookCanvas(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    RAWFRAME_TRY_ASSIGN(const graph::Document kDocument, material::readCanvas(kText));
    RAWFRAME_TRY_ASSIGN(const material::CanvasMaterial kCompiled, material::compileCanvas(kDocument));
    return Artifact{.type = content::ResourceTypeId{material::kCanvasMaterialType},
                    .representation = *content::RepresentationId::parse(material::kCanvasMaterialRepresentation),
                    .bytes = material::encodeCanvas(kCompiled),
                    .subassets = {},
                    .report = {{"variants", 1}, {"axes", 0}, {"variantsHeadroom", kVariantsPerMaterial - 1}}};
}

} // namespace

Importer materialImporter(std::optional<ShaderTools> tools) {
    const base::Sha256Digest kIdentity = tools.has_value() ? tools->identity : base::Sha256Digest{};
    return Importer{.identity = "rawframe.material",
                    .normalize = &normalize,
                    .cook =
                        [tools = std::move(tools)](std::span<const std::byte> source, std::string_view, Reads&) {
                            return cookMaterial(source, tools);
                        },
                    .tools = kIdentity};
}

Importer postProcessImporter() noexcept {
    return Importer{.identity = "rawframe.postprocess", .normalize = &normalize, .cook = &cookPostProcess};
}

Importer canvasImporter() noexcept {
    return Importer{.identity = "rawframe.canvasmaterial", .normalize = &normalize, .cook = &cookCanvas};
}

} // namespace rawframe::cook
