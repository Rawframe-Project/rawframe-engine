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
#include <thread>
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

/// Every mesh of the cook into `cooked` drawn into its `thumbnails`
/// directory by one run of the tool, whose device and pipelines are made
/// once for them all, what it said kept there in `thumbnails.log`. Five minutes only bounds a tool that never ends:
/// lavapipe makes the scene's pipelines on the CPU, slowly on a loaded
/// machine (D476).
void drawMeshes(const std::filesystem::path& tool,
                const std::filesystem::path& cooked,
                const std::vector<content::ManifestEntry>& entries) {
    const std::filesystem::path kInto = cooked / "thumbnails";
    std::error_code error;
    std::filesystem::create_directories(kInto, error);
    std::vector<std::string> arguments{kInto.string()};
    for (const content::ManifestEntry& kEntry : entries) {
        if (kEntry.type == content::ResourceTypeId{mesh::kMeshType}) {
            arguments.push_back(objectOf(cooked, kEntry).string());
        }
    }
    auto drawing =
        process::Child::start({.program = tool, .arguments = std::move(arguments), .output = kInto / "thumbnails.log"});
    if (!drawing.has_value()) {
        return;
    }
    const auto kUntil = std::chrono::steady_clock::now() + std::chrono::minutes{5};
    while (!drawing->exited().has_value() && std::chrono::steady_clock::now() < kUntil) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    drawing->kill();
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
        if (meshes.drawn != *cooked && !std::filesystem::exists(object, error)) {
            drawMeshes(meshes.tool, *cooked, *kEntries);
            meshes.drawn = *cooked;
        }
    }
    const auto kBytes = readFile(object);
    if (!kBytes.has_value()) {
        return kMesh ? refused(authoring::AuthoringError::UnsupportedOperation,
                               result::ErrorClass::Unavailable,
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
