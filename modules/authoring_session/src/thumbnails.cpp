#include "thumbnails.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/bits128.h"
#include "rawframe/base/files.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/material/material.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/process/child.h"
#include "rawframe/texture/texture.h"
#include "rawframe/world_kest/game_files.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <string>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

std::unexpected<result::Error> refused(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return std::unexpected{result::fail(kind, authoring::kAuthoringDomain, authoring::code(error), why).error()};
}

/// The cooked object an entry's bytes are in.
std::filesystem::path objectOf(const std::filesystem::path& cooked, const content::ManifestEntry& entry) {
    const std::string kDigest = entry.digest.text();
    return cooked / "objects" / kDigest.substr(kDigest.find(':') + 1);
}

/// The most meshes and materials one run of the tool draws: a cook of more
/// has the rest go without, so one asking cannot hold the machine for a run
/// without end.
constexpr std::size_t kMostDrawn = 1024;
/// How long a run may draw: lavapipe makes the scene's pipelines on the
/// CPU, slowly on a loaded machine (D476); past it, the run is stopped.
constexpr auto kDrawing = std::chrono::minutes{5};

std::string hexOf(const base::Bits128& id) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(id, digits);
    return {digits.begin(), digits.end()};
}

/// The tool's plan for the cook into `cooked` (D900): every mesh and every
/// surface material, up to `kMostDrawn`, then every material again as a
/// base an instance may read, and every texture the game's description
/// names by the identity its materials sample it by.
std::string planOf(const std::filesystem::path& game,
                   const std::filesystem::path& cooked,
                   const std::vector<content::ManifestEntry>& entries) {
    std::string plan;
    std::size_t asked = 0;
    for (const content::ManifestEntry& kEntry : entries) {
        const std::string kObject = objectOf(cooked, kEntry).string();
        if (kEntry.type == content::ResourceTypeId{mesh::kMeshType} && asked++ < kMostDrawn) {
            plan += "mesh\t" + kObject + "\n";
        } else if (kEntry.type == content::ResourceTypeId{material::kMaterialType} && asked++ < kMostDrawn) {
            plan += "material\t" + kObject + "\t" + hexOf(kEntry.id.value) + "\t" +
                    std::string{kEntry.representation.text()} + "\n";
        }
    }
    for (const content::ManifestEntry& kEntry : entries) {
        if (kEntry.type == content::ResourceTypeId{material::kMaterialType}) {
            plan += "base\t" + hexOf(kEntry.id.value) + "\t" + objectOf(cooked, kEntry).string() + "\t" +
                    std::string{kEntry.representation.text()} + "\n";
        }
    }
    const auto kFiles = world_kest::GameFiles::fromDirectory(game, nullptr, world_kest::MeshReading::Named);
    if (kFiles.has_value()) {
        for (const world_kest::GameTextureResource& kTexture : kFiles->textures()) {
            const auto kEntry =
                std::ranges::find(entries, content::ResourceId{kTexture.texture}, &content::ManifestEntry::id);
            if (kEntry != entries.end()) {
                plan += "texture\t" + std::to_string(kTexture.id) + "\t" + objectOf(cooked, *kEntry).string() + "\n";
            }
        }
    }
    return plan;
}

/// The cook into `cooked` set drawing into its `thumbnails` directory by one
/// run of the tool, whose device and pipelines are made once for every mesh
/// and material, its plan kept there as `plan` and what it says as
/// `thumbnails.log`; nothing when the tool does not start.
std::optional<process::Child> drawCook(const std::filesystem::path& tool,
                                       const std::filesystem::path& game,
                                       const std::filesystem::path& cooked,
                                       const std::vector<content::ManifestEntry>& entries) {
    const std::filesystem::path kInto = cooked / "thumbnails";
    std::error_code error;
    std::filesystem::create_directories(kInto, error);
    const std::string kPlan = planOf(game, cooked, entries);
    if (!base::replaceFile(kInto / "plan", std::as_bytes(std::span{kPlan.data(), kPlan.size()}))) {
        return std::nullopt;
    }
    auto drawing = process::Child::start({.program = tool,
                                          .arguments = {kInto.string(), (kInto / "plan").string()},
                                          .output = kInto / "thumbnails.log"});
    if (!drawing.has_value()) {
        return std::nullopt;
    }
    return std::move(*drawing);
}

} // namespace

result::Result<Value> thumbnailOf(const std::filesystem::path& game,
                                  std::string_view asset,
                                  const std::optional<std::filesystem::path>& cooked,
                                  std::uint32_t size,
                                  MeshPictures& meshes) {
    const std::vector<SourceListed> kSources = sourcesBeside(game);
    const auto kSource = std::ranges::find(kSources, asset, &SourceListed::path);
    if (kSource == kSources.end()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "no source with a sidecar is at that path under the game's directory");
    }
    // A mesh's and a material's are drawn (D899, D900).
    const bool kDrawn = kSource->importer == "rawframe.mesh" || kSource->importer == "rawframe.material";
    if (kSource->importer != "rawframe.texture" && !kDrawn) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "only a texture, a mesh, and a material have thumbnails");
    }
    if (kDrawn && meshes.tool.empty()) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "no thumbnail tool draws this session's meshes and materials");
    }
    if (!cooked.has_value()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "this session has not cooked the game");
    }
    // The cook's manifest names the object the texture became.
    const auto kText = readFile(*cooked / "content.manifest");
    const auto kSidecarText =
        readFile(game.parent_path() / (std::string{asset} + std::string{content::kSidecarSuffix}));
    if (!kText.has_value() || !kSidecarText.has_value()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "the last cook's manifest or the source's sidecar does not read");
    }
    const auto kSidecar = content::readSidecar(*kSidecarText);
    const auto kEntries = content::readManifest(*kText);
    if (!kSidecar.has_value() || !kEntries.has_value()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "the last cook's manifest or the source's sidecar does not read");
    }
    const auto kEntry = std::ranges::find(*kEntries, kSidecar->id, &content::ManifestEntry::id);
    if (kEntry == kEntries->end()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "the last cook holds no such source; cook again");
    }
    if (kDrawn && kEntry->type != content::ResourceTypeId{mesh::kMeshType} &&
        kEntry->type != content::ResourceTypeId{material::kMaterialType}) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "only a surface material has a thumbnail: a post process or a canvas material has none");
    }
    std::filesystem::path object = objectOf(*cooked, *kEntry);
    if (kDrawn) {
        object = *cooked / "thumbnails" / (object.filename().string() + ".texture");
        std::error_code error;
        // A run that ended, or ran past its time, is let go; one still
        // drawing is waited for by asking again, never on this thread,
        // which answers every other record and every other author.
        if (meshes.drawing.has_value() &&
            (meshes.drawing->exited().has_value() || std::chrono::steady_clock::now() - meshes.began > kDrawing)) {
            meshes.drawing.reset();
        }
        if (!meshes.drawing.has_value() && meshes.drawn != *kText && !std::filesystem::exists(object, error)) {
            meshes.drawing = drawCook(meshes.tool, game, *cooked, *kEntries);
            meshes.began = std::chrono::steady_clock::now();
            meshes.drawn = *kText;
        }
        if (meshes.drawing.has_value() && !std::filesystem::exists(object, error)) {
            return refused(authoring::AuthoringError::UnsupportedOperation,
                           result::ErrorClass::Unavailable,
                           "the cook's meshes and materials are being drawn; ask again");
        }
    }
    const auto kBytes = readFile(object);
    if (!kBytes.has_value()) {
        return kDrawn ? refused(authoring::AuthoringError::UnsupportedOperation,
                                result::ErrorClass::Unsupported,
                                "the thumbnail tool could not draw it here")
                      : refused(authoring::AuthoringError::TargetNotFound,
                                result::ErrorClass::NotFound,
                                "the cooked texture does not read");
    }
    RAWFRAME_TRY_ASSIGN(const texture::Texture kTexture,
                        texture::decode(std::as_bytes(std::span{kBytes->data(), kBytes->size()})));
    // A picture only: an environment's cube of light, a volume, or a glyph
    // atlas's coverage is no thumbnail a browser can show.
    const bool kPicture = kTexture.faces == 1 && kTexture.depth == 1 &&
                          (kTexture.format == texture::Format::Rgba8 || kTexture.format == texture::Format::Rgba8Srgb ||
                           kTexture.format == texture::Format::Bc7 || kTexture.format == texture::Format::Bc7Srgb);
    if (!kPicture) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "this texture is no picture: an environment, a volume, or a glyph atlas");
    }
    // The first level no larger than asked, else the smallest there is.
    std::size_t level = kTexture.levels.empty() ? 0 : kTexture.levels.size() - 1;
    for (std::size_t at = 0; at < kTexture.levels.size(); ++at) {
        if (std::max(kTexture.levels[at].width, kTexture.levels[at].height) <= size) {
            level = at;
            break;
        }
    }
    Value made = Value::object();
    made.add("kind", Value::string("authoring.thumbnail"));
    made.add("asset", Value::string(std::string{asset}));
    made.add("object", Value::string(object.generic_string()));
    made.add("level", Value::integer(static_cast<std::int64_t>(level)));
    made.add("width",
             Value::integer(kTexture.levels.empty() ? 0 : static_cast<std::int64_t>(kTexture.levels[level].width)));
    made.add("height",
             Value::integer(kTexture.levels.empty() ? 0 : static_cast<std::int64_t>(kTexture.levels[level].height)));
    return made;
}

} // namespace rawframe::authoring_session
