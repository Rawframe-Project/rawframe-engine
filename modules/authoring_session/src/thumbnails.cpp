#include "thumbnails.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/process/child.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <chrono>
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

/// The most meshes one run of the tool draws: a cook of more has the rest
/// go without, so one asking cannot hold the machine for a run without end.
constexpr std::size_t kMostDrawn = 1024;
/// How long a run may draw: lavapipe makes the scene's pipelines on the
/// CPU, slowly on a loaded machine (D476); past it, the run is stopped.
constexpr auto kDrawing = std::chrono::minutes{5};

/// Every mesh of the cook into `cooked`, up to `kMostDrawn`, set drawing
/// into its `thumbnails` directory by one run of the tool, whose device and
/// pipelines are made once for them all, what it says kept there in
/// `thumbnails.log`; nothing when the tool does not start.
std::optional<process::Child> drawMeshes(const std::filesystem::path& tool,
                                         const std::filesystem::path& cooked,
                                         const std::vector<content::ManifestEntry>& entries) {
    const std::filesystem::path kInto = cooked / "thumbnails";
    std::error_code error;
    std::filesystem::create_directories(kInto, error);
    std::vector<std::string> arguments{kInto.string()};
    for (const content::ManifestEntry& kEntry : entries) {
        if (kEntry.type == content::ResourceTypeId{mesh::kMeshType} && arguments.size() <= kMostDrawn) {
            arguments.push_back(objectOf(cooked, kEntry).string());
        }
    }
    auto drawing =
        process::Child::start({.program = tool, .arguments = std::move(arguments), .output = kInto / "thumbnails.log"});
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
    const bool kMesh = kSource->importer == "rawframe.mesh";
    if (kSource->importer != "rawframe.texture" && !kMesh) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "only a texture and a mesh have thumbnails");
    }
    if (kMesh && meshes.tool.empty()) {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "no thumbnail tool draws this session's meshes");
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
    std::filesystem::path object = objectOf(*cooked, *kEntry);
    if (kMesh) {
        object = *cooked / "thumbnails" / (object.filename().string() + ".texture");
        std::error_code error;
        // A run that ended, or ran past its time, is let go; one still
        // drawing is waited for by asking again, never on this thread,
        // which answers every other record and every other author.
        if (meshes.drawing.has_value() &&
            (meshes.drawing->exited().has_value() || std::chrono::steady_clock::now() - meshes.began > kDrawing)) {
            meshes.drawing.reset();
        }
        if (!meshes.drawing.has_value() && meshes.drawn != *cooked && !std::filesystem::exists(object, error)) {
            meshes.drawing = drawMeshes(meshes.tool, *cooked, *kEntries);
            meshes.began = std::chrono::steady_clock::now();
            meshes.drawn = *cooked;
        }
        if (meshes.drawing.has_value() && !std::filesystem::exists(object, error)) {
            return refused(authoring::AuthoringError::UnsupportedOperation,
                           result::ErrorClass::Unavailable,
                           "the cook's meshes are being drawn; ask again");
        }
    }
    const auto kBytes = readFile(object);
    if (!kBytes.has_value()) {
        return kMesh ? refused(authoring::AuthoringError::UnsupportedOperation,
                               result::ErrorClass::Unsupported,
                               "the thumbnail tool could not draw this mesh here")
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
