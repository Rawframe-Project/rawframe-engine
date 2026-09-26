#include "rawframe/content/library.h"

#include "directory.h"
#include "rawframe/content/errors.h"
#include "source.h"

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

result::Result<BuildContent> Library::build(const base::Sha256Digest& root,
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
    return ContentSource::openBuild(files_, kManifest, kSigned, root, publisher);
}

} // namespace rawframe::content
