#include "thumbnails.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rawframe::authoring_session {

namespace {

using document::Value;

std::unexpected<result::Error> refused(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return std::unexpected{result::fail(kind, authoring::kAuthoringDomain, authoring::code(error), why).error()};
}

} // namespace

result::Result<Value> thumbnailOf(const std::filesystem::path& game,
                                  std::string_view asset,
                                  const std::optional<std::filesystem::path>& cooked,
                                  std::uint32_t size) {
    const std::vector<SourceListed> kSources = sourcesBeside(game);
    const auto kSource = std::ranges::find(kSources, asset, &SourceListed::path);
    if (kSource == kSources.end()) {
        return refused(authoring::AuthoringError::TargetNotFound,
                       result::ErrorClass::NotFound,
                       "no source with a sidecar is at that path under the game's directory");
    }
    if (kSource->importer != "rawframe.texture") {
        return refused(authoring::AuthoringError::UnsupportedOperation,
                       result::ErrorClass::Unsupported,
                       "only a texture has a thumbnail yet");
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
                       "the last cook holds no such texture; cook again");
    }
    const std::string kDigest = kEntry->digest.text();
    const std::filesystem::path kObject = *cooked / "objects" / kDigest.substr(kDigest.find(':') + 1);
    const auto kBytes = readFile(kObject);
    if (!kBytes.has_value()) {
        return refused(authoring::AuthoringError::TargetNotFound,
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
    made.add("object", Value::string(kObject.generic_string()));
    made.add("level", Value::integer(static_cast<std::int64_t>(level)));
    made.add("width",
             Value::integer(kTexture.levels.empty() ? 0 : static_cast<std::int64_t>(kTexture.levels[level].width)));
    made.add("height",
             Value::integer(kTexture.levels.empty() ? 0 : static_cast<std::int64_t>(kTexture.levels[level].height)));
    return made;
}

} // namespace rawframe::authoring_session
