#include "rawframe/content/library.h"

#include "directory.h"
#include "rawframe/content/errors.h"
#include "source.h"

#include <algorithm>

namespace rawframe::content {

namespace {

/// SPEC-0023's ceiling on a key set's record.
constexpr std::uint64_t kMaximumKeySet = 16 * 1024;

std::unexpected<result::Error> unavailable(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kContentDomain, code(ContentError::SourceUnavailable), why)
            .error()};
}

/// A file of the library no longer than `ceiling`, whole; `absent` is the
/// refusal's words when there is none within it.
result::Result<std::vector<std::byte>> readWithin(const ContentSource::Implementation& files,
                                                  std::string_view path,
                                                  std::uint64_t ceiling,
                                                  std::string_view absent) {
    const auto kSize = files.size(path);
    if (!kSize.has_value() || *kSize > ceiling) {
        return unavailable(absent);
    }
    return files.read(path, *kSize);
}

} // namespace

std::string buildDirectoryOf(const base::Sha256Digest& root) {
    return "builds/" + ContentDigest{.bytes = root}.text().substr(7);
}

std::string blobPathOf(const ContentDigest& blob) {
    const std::string kHex = blob.text().substr(7);
    return "sha256/" + kHex.substr(0, 2) + "/" + kHex.substr(2);
}

std::string keysPathOf(std::string_view publisher) {
    return "keys/" + std::string{publisher} + ".keys";
}

std::string compositionPathOf(const base::Sha256Digest& composition) {
    return "compositions/" + ContentDigest{.bytes = composition}.text().substr(7);
}

std::string releasePathOf(const base::Sha256Digest& release) {
    return "releases/" + ContentDigest{.bytes = release}.text().substr(7);
}

std::string channelPathOf(std::string_view subject, std::string_view channel) {
    return "channels/" + std::string{subject} + "/" + std::string{channel};
}

#if RAWFRAME_FILE_SYSTEM
result::Result<Library> Library::directory(const std::filesystem::path& root) {
    RAWFRAME_TRY_ASSIGN(auto opened, DirectorySource::open(root, "the library is not a readable directory"));
    return Library{std::move(opened)};
}
#endif

result::Result<Library> Library::memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files) {
    RAWFRAME_TRY_ASSIGN(ContentSource held, ContentSource::memory(std::move(files)));
    return Library{std::move(held.implementation_)};
}

result::Result<signature::PublisherKeySet> Library::keys(std::string_view publisher) const {
    RAWFRAME_TRY_ASSIGN(
        const std::vector<std::byte> kBytes,
        readWithin(*files_, keysPathOf(publisher), kMaximumKeySet, "the publisher has no key set within its ceiling"));
    return signature::readPublisherKeySet(
        std::string_view{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()});
}

result::Result<BuildManifest> Library::manifest(const base::Sha256Digest& root,
                                                const signature::PublisherKeySet& publisher) const {
    const std::string kDirectory = buildDirectoryOf(root) + "/";
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kManifest,
                        readWithin(*files_,
                                   kDirectory + std::string{kBuildManifestName},
                                   kMaximumBuildManifest,
                                   "the Build has no manifest within its ceiling"));
    RAWFRAME_TRY_ASSIGN(
        const std::vector<std::byte> kSigned,
        readWithin(
            *files_, kDirectory + std::string{kBuildSignatureName}, kMaximumBuildSignature, "the Build is not signed"));
    return readBuildManifest(kManifest, kSigned, root, publisher);
}

result::Result<std::vector<ContentDigest>> Library::damaged(const BuildManifest& manifest) const {
    std::vector<ContentDigest> found;
    for (std::size_t at = 0; at < manifest.entries.size(); ++at) {
        base::Sha256 whole;
        bool intact = true;
        for (const BuildChunk& chunk : manifest.chunks[at]) {
            const auto kContent = readChunk(*files_, chunk);
            if (kContent.has_value()) {
                whole.update(*kContent);
                continue;
            }
            intact = false;
            if (std::ranges::find(found, chunk.blob) == found.end()) {
                found.push_back(chunk.blob);
            }
        }
        if (intact && !sameDigest(ContentDigest{.bytes = whole.finish()}, manifest.entries[at].digest)) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::DataLoss,
                                                               kContentDomain,
                                                               code(ContentError::DigestMismatch),
                                                               "the Build's chunks do not make its resource")
                                                      .error()
                                                      .withContext("resource", manifest.entries[at].locator)};
        }
    }
    return found;
}

result::Result<BuildContent> Library::build(const base::Sha256Digest& root,
                                            const signature::PublisherKeySet& publisher) const {
    RAWFRAME_TRY_ASSIGN(BuildManifest read, manifest(root, publisher));
    return ContentSource::openBuild(files_, std::move(read));
}

} // namespace rawframe::content
