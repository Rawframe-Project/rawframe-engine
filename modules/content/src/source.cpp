#include "source.h"

#include "directory.h"
#include "frame.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/library.h"
#include "rawframe/content/manifest.h"

#include <map>

namespace rawframe::content {

namespace {

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

class MemorySource final : public ContentSource::Implementation {
public:
    explicit MemorySource(std::map<std::string, std::vector<std::byte>, std::less<>> files) noexcept
        : files_(std::move(files)) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        const auto kFound = files_.find(locator);
        if (kFound == files_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the source has no such file");
        }
        if (kFound->second.size() < length) {
            return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file is shorter than declared");
        }
        if (kFound->second.size() > length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file is longer than declared");
        }
        return kFound->second;
    }

    result::Result<std::uint64_t> size(std::string_view locator) const override {
        const auto kFound = files_.find(locator);
        if (kFound == files_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the source has no such file");
        }
        return kFound->second.size();
    }

private:
    std::map<std::string, std::vector<std::byte>, std::less<>> files_;
};

class BuildSource final : public ContentSource::Implementation {
public:
    BuildSource(std::shared_ptr<const ContentSource::Implementation> blobs,
                std::map<std::string, std::vector<BuildChunk>, std::less<>> chunks) noexcept
        : blobs_(std::move(blobs)), chunks_(std::move(chunks)) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        const auto kFound = chunks_.find(locator);
        if (kFound == chunks_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the Build has no such resource");
        }
        std::vector<std::byte> bytes;
        for (const BuildChunk& chunk : kFound->second) {
            RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kContent, readChunk(*blobs_, chunk));
            bytes.insert(bytes.end(), kContent.begin(), kContent.end());
        }
        if (bytes.size() != length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the chunks do not make the resource");
        }
        return bytes;
    }

    result::Result<std::uint64_t> size(std::string_view locator) const override {
        const auto kFound = chunks_.find(locator);
        if (kFound == chunks_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the Build has no such resource");
        }
        std::uint64_t total = 0;
        for (const BuildChunk& chunk : kFound->second) {
            total += chunk.size;
        }
        return total;
    }

private:
    std::shared_ptr<const ContentSource::Implementation> blobs_;
    std::map<std::string, std::vector<BuildChunk>, std::less<>> chunks_;
};

} // namespace

// The stored blob verified before it is used, then the content it holds
// (SPEC-0021's verification order, steps 2 and 3).
result::Result<std::vector<std::byte>> readChunk(const ContentSource::Implementation& blobs, const BuildChunk& chunk) {
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> blob, blobs.read(blobPathOf(chunk.blob), chunk.blobSize));
    if (!sameDigest(ContentDigest::of(blob), chunk.blob)) {
        return refuse(ContentError::DigestMismatch, result::ErrorClass::DataLoss, "a blob is not what the Build says");
    }
    if (chunk.compressed) {
        RAWFRAME_TRY_ASSIGN(blob, decompressFrame(blob, chunk.size));
    }
    if (!sameDigest(ContentDigest::of(blob), chunk.content)) {
        return refuse(
            ContentError::DigestMismatch, result::ErrorClass::DataLoss, "a chunk's content is not what the Build says");
    }
    return blob;
}

result::Result<ContentSource> ContentSource::memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files) {
    std::map<std::string, std::vector<std::byte>, std::less<>> held;
    for (auto& [locator, bytes] : files) {
        if (!validLocator(locator)) {
            return refuse(ContentError::InvalidLocator, result::ErrorClass::InvalidArgument, "not a valid locator");
        }
        held.insert_or_assign(std::move(locator), std::move(bytes));
    }
    return ContentSource{std::make_shared<const MemorySource>(std::move(held))};
}

#if RAWFRAME_FILE_SYSTEM
result::Result<ContentSource> ContentSource::directory(const std::filesystem::path& root) {
    RAWFRAME_TRY_ASSIGN(auto opened, DirectorySource::open(root, "the root is not a readable directory"));
    return ContentSource{std::move(opened)};
}
#endif

result::Result<BuildContent> ContentSource::openBuild(std::shared_ptr<const Implementation> files,
                                                      std::span<const std::byte> manifest,
                                                      std::span<const std::byte> signedBytes,
                                                      const base::Sha256Digest& expectedRoot,
                                                      const signature::PublisherKeySet& publisher) {
    RAWFRAME_TRY_ASSIGN(BuildManifest read, readBuildManifest(manifest, signedBytes, expectedRoot, publisher));
    return openBuild(std::move(files), std::move(read));
}

BuildContent ContentSource::openBuild(std::shared_ptr<const Implementation> files, BuildManifest manifest) {
    std::map<std::string, std::vector<BuildChunk>, std::less<>> lists;
    for (std::size_t at = 0; at < manifest.entries.size(); ++at) {
        lists.emplace(manifest.entries[at].locator, std::move(manifest.chunks[at]));
    }
    return BuildContent{.root = manifest.root,
                        .subject = std::move(manifest.subject),
                        .version = std::move(manifest.version),
                        .entries = std::move(manifest.entries),
                        .source =
                            ContentSource{std::make_shared<const BuildSource>(std::move(files), std::move(lists))}};
}

#if RAWFRAME_FILE_SYSTEM
result::Result<BuildContent> ContentSource::build(const std::filesystem::path& root,
                                                  const base::Sha256Digest& expectedRoot,
                                                  const signature::PublisherKeySet& publisher) {
    RAWFRAME_TRY_ASSIGN(auto files, DirectorySource::open(root, "the Build is not a readable directory"));
    const auto kManifestSize = files->size(kBuildManifestName);
    if (!kManifestSize || *kManifestSize > kMaximumBuildManifest) {
        return refuse(ContentError::SourceUnavailable,
                      result::ErrorClass::Unavailable,
                      "the Build has no manifest within its ceiling");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes, files->read(kBuildManifestName, *kManifestSize));
    const auto kSignatureSize = files->size(kBuildSignatureName);
    if (!kSignatureSize || *kSignatureSize > kMaximumBuildSignature) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the Build is not signed");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kSigned, files->read(kBuildSignatureName, *kSignatureSize));
    return openBuild(std::move(files), kBytes, kSigned, expectedRoot, publisher);
}
#endif

result::Result<BuildContent> ContentSource::build(std::vector<std::pair<std::string, std::vector<std::byte>>> files,
                                                  const base::Sha256Digest& expectedRoot,
                                                  const signature::PublisherKeySet& publisher) {
    std::map<std::string, std::vector<std::byte>, std::less<>> held;
    for (auto& [locator, bytes] : files) {
        if (!validLocator(locator)) {
            return refuse(ContentError::InvalidLocator, result::ErrorClass::InvalidArgument, "not a valid locator");
        }
        held.insert_or_assign(std::move(locator), std::move(bytes));
    }
    const auto kManifest = held.find(kBuildManifestName);
    if (kManifest == held.end() || kManifest->second.size() > kMaximumBuildManifest) {
        return refuse(ContentError::SourceUnavailable,
                      result::ErrorClass::Unavailable,
                      "the Build has no manifest within its ceiling");
    }
    const auto kSignature = held.find(kBuildSignatureName);
    if (kSignature == held.end() || kSignature->second.size() > kMaximumBuildSignature) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the Build is not signed");
    }
    const std::vector<std::byte> kBytes = kManifest->second;
    const std::vector<std::byte> kSigned = kSignature->second;
    return openBuild(std::make_shared<const MemorySource>(std::move(held)), kBytes, kSigned, expectedRoot, publisher);
}

} // namespace rawframe::content
