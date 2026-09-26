#include "source.h"

#include "frame.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"
#include "rawframe/signature/errors.h"

#include <map>

#if RAWFRAME_FILE_SYSTEM
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

private:
    std::map<std::string, std::vector<std::byte>, std::less<>> files_;
};

#if RAWFRAME_FILE_SYSTEM
/// When a file was last modified, by the name each platform gives it.
const timespec& modifiedAt(const struct stat& status) noexcept {
#if defined(__APPLE__)
    return status.st_mtimespec;
#else
    return status.st_mtim;
#endif
}

/// A file descriptor closed when it goes.
class Descriptor {
public:
    explicit Descriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {
    }
    Descriptor(Descriptor&& other) noexcept : descriptor_(std::exchange(other.descriptor_, -1)) {
    }
    Descriptor& operator=(Descriptor&& other) noexcept {
        std::swap(descriptor_, other.descriptor_);
        return *this;
    }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }
    [[nodiscard]] int get() const noexcept {
        return descriptor_;
    }

private:
    int descriptor_;
};

class DirectorySource final : public ContentSource::Implementation {
public:
    DirectorySource(Descriptor root, dev_t device) noexcept : root_(std::move(root)), device_(device) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        // Segment by segment from the root: no link is followed and no other
        // filesystem entered, so nothing outside the root is reachable.
        Descriptor directory;
        int at = root_.get();
        std::size_t start = 0;
        for (std::size_t end = locator.find('/'); end != std::string_view::npos; end = locator.find('/', start)) {
            const std::string kSegment{locator.substr(start, end - start)};
            Descriptor next{::openat(at, kSegment.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
            RAWFRAME_TRY(check(next, "a directory on the way"));
            directory = std::move(next);
            at = directory.get();
            start = end + 1;
        }
        const std::string kName{locator.substr(start)};
        const Descriptor kFile{::openat(at, kName.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
        RAWFRAME_TRY(check(kFile, "the file"));
        struct stat before{};
        if (::fstat(kFile.get(), &before) != 0 || !S_ISREG(before.st_mode)) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::Unavailable, "not a regular file");
        }
        if (static_cast<std::uint64_t>(before.st_size) < length) {
            return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file is shorter than declared");
        }
        if (static_cast<std::uint64_t>(before.st_size) > length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file is longer than declared");
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        std::size_t done = 0;
        while (done < bytes.size()) {
            const ssize_t kRead = ::read(kFile.get(), bytes.data() + done, bytes.size() - done);
            if (kRead < 0 && errno == EINTR) {
                continue;
            }
            if (kRead < 0) {
                return refuse(ContentError::ReadFailed, result::ErrorClass::Unavailable, "the file cannot be read");
            }
            if (kRead == 0) {
                return refuse(ContentError::ShortRead, result::ErrorClass::DataLoss, "the file ended early");
            }
            done += static_cast<std::size_t>(kRead);
        }
        // Changed while it was read: its size or its modification.
        std::byte extra{};
        struct stat after{};
        if (::read(kFile.get(), &extra, 1) != 0 || ::fstat(kFile.get(), &after) != 0 ||
            after.st_size != before.st_size || modifiedAt(after).tv_sec != modifiedAt(before).tv_sec ||
            modifiedAt(after).tv_nsec != modifiedAt(before).tv_nsec) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the file changed while it was read");
        }
        return bytes;
    }

private:
    result::Status check(const Descriptor& opened, std::string_view what) const {
        if (opened.get() < 0) {
            if (errno == ELOOP || errno == ENOTDIR) {
                return refuse(ContentError::PathEscape, result::ErrorClass::PermissionDenied, "a link on the way");
            }
            return std::unexpected<result::Error>{
                refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "cannot be opened")
                    .error()
                    .withContext("what", what)};
        }
        struct stat status{};
        if (::fstat(opened.get(), &status) != 0 || status.st_dev != device_) {
            return refuse(
                ContentError::PathEscape, result::ErrorClass::PermissionDenied, "another filesystem on the way");
        }
        return {};
    }

    Descriptor root_;
    dev_t device_;
};
#endif

/// SPEC-0021's chunk, as the manifest lists it: a `raw` blob is its
/// content, a `zstd` blob one Zstandard frame of it.
struct Chunk {
    ContentDigest content;
    std::uint64_t size = 0;
    ContentDigest blob;
    std::uint64_t blobSize = 0;
    bool compressed = false;
};

/// SPEC-0021's hard ceilings.
constexpr std::size_t kMaximumBuildManifest = std::size_t{64} * 1024 * 1024;
constexpr std::size_t kMaximumBuildResources = 262'144;
constexpr std::size_t kMaximumChunks = 65'536;
constexpr std::uint64_t kMaximumChunkSize = std::uint64_t{4} * 1024 * 1024;

class BuildSource final : public ContentSource::Implementation {
public:
    BuildSource(std::shared_ptr<const ContentSource::Implementation> blobs,
                std::map<std::string, std::vector<Chunk>, std::less<>> chunks) noexcept
        : blobs_(std::move(blobs)), chunks_(std::move(chunks)) {
    }

    result::Result<std::vector<std::byte>> read(std::string_view locator, std::uint64_t length) const override {
        const auto kFound = chunks_.find(locator);
        if (kFound == chunks_.end()) {
            return refuse(ContentError::ReadFailed, result::ErrorClass::NotFound, "the Build has no such resource");
        }
        std::vector<std::byte> bytes;
        for (const Chunk& chunk : kFound->second) {
            // The stored blob verified before it is used, then the content
            // it holds (SPEC-0021's verification order, steps 2 and 3).
            const std::string kHex = chunk.blob.text().substr(7);
            RAWFRAME_TRY_ASSIGN(std::vector<std::byte> blob,
                                blobs_->read("sha256/" + kHex.substr(0, 2) + "/" + kHex.substr(2), chunk.blobSize));
            if (!sameDigest(ContentDigest::of(blob), chunk.blob)) {
                return refuse(
                    ContentError::DigestMismatch, result::ErrorClass::DataLoss, "a blob is not what the Build says");
            }
            if (chunk.compressed) {
                RAWFRAME_TRY_ASSIGN(blob, decompressFrame(blob, chunk.size));
            }
            if (!sameDigest(ContentDigest::of(blob), chunk.content)) {
                return refuse(ContentError::DigestMismatch,
                              result::ErrorClass::DataLoss,
                              "a chunk's content is not what the Build says");
            }
            bytes.insert(bytes.end(), blob.begin(), blob.end());
        }
        if (bytes.size() != length) {
            return refuse(
                ContentError::SourceChanged, result::ErrorClass::DataLoss, "the chunks do not make the resource");
        }
        return bytes;
    }

private:
    std::shared_ptr<const ContentSource::Implementation> blobs_;
    std::map<std::string, std::vector<Chunk>, std::less<>> chunks_;
};

std::unexpected<result::Error> invalidBuild(std::string_view why) {
    return refuse(ContentError::ManifestInvalid, result::ErrorClass::InvalidArgument, why);
}

/// A string member, or none.
const std::string* textOf(const document::Value& record, std::string_view name) {
    const document::Value* value = record.find(name);
    return value != nullptr && value->kind() == document::Value::Kind::String ? value->text() : nullptr;
}

std::optional<std::uint64_t> sizeOf(const document::Value& record, std::string_view name) {
    const document::Value* value = record.find(name);
    const std::optional<std::int64_t> kSize = value != nullptr ? value->integer() : std::nullopt;
    return kSize.has_value() && *kSize >= 0 ? std::optional{static_cast<std::uint64_t>(*kSize)} : std::nullopt;
}

result::Result<std::vector<Chunk>> chunksOf(const document::Value& list, std::uint64_t size) {
    if (list.kind() != document::Value::Kind::Array || list.items().empty() || list.items().size() > kMaximumChunks) {
        return invalidBuild("a resource's chunk list is empty, too long, or not a list");
    }
    std::vector<Chunk> chunks;
    std::uint64_t covered = 0;
    for (const document::Value& each : list.items()) {
        const std::string* content = textOf(each, "content");
        const std::string* blob = textOf(each, "blob");
        const std::string* codec = textOf(each, "codec");
        const auto kSize = sizeOf(each, "size");
        const auto kBlobSize = sizeOf(each, "blob_size");
        if (each.kind() != document::Value::Kind::Object || each.names().size() != 5 || content == nullptr ||
            blob == nullptr || codec == nullptr || !kSize.has_value() || !kBlobSize.has_value() ||
            *kSize > kMaximumChunkSize) {
            return invalidBuild("a chunk is not {content, size, blob, blob_size, codec} within its bounds");
        }
        const auto kContent = ContentDigest::parse(*content);
        const auto kBlob = ContentDigest::parse(*blob);
        if (!kContent.has_value() || !kBlob.has_value()) {
            return invalidBuild("a chunk's digest is not a digest");
        }
        // A raw blob is its content; a zstd one is smaller than its content,
        // or the packer would have stored it raw.
        const bool kRaw = *codec == "raw" && sameDigest(*kContent, *kBlob) && *kSize == *kBlobSize;
        const bool kZstd = *codec == "zstd" && *kBlobSize < *kSize;
        if (!kRaw && !kZstd) {
            return invalidBuild("a chunk is raw, its blob its content, or zstd, its blob smaller than its content");
        }
        covered += *kSize;
        chunks.push_back(
            Chunk{.content = *kContent, .size = *kSize, .blob = *kBlob, .blobSize = *kBlobSize, .compressed = kZstd});
    }
    if (covered != size) {
        return invalidBuild("a resource's chunks do not cover it exactly");
    }
    return chunks;
}

} // namespace

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
    Descriptor opened{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    struct stat status{};
    if (opened.get() < 0 || ::fstat(opened.get(), &status) != 0) {
        return refuse(
            ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the root is not a readable directory");
    }
    return ContentSource{std::make_shared<const DirectorySource>(std::move(opened), status.st_dev)};
}
#endif

// The manifest's exact bytes signed by the publisher before a byte of them
// is parsed, then what the Build is, then its resources.
result::Result<BuildContent> ContentSource::openBuild(std::shared_ptr<const Implementation> files,
                                                      std::span<const std::byte> manifest,
                                                      std::span<const std::byte> signedBytes,
                                                      const base::Sha256Digest& expectedRoot,
                                                      const signature::PublisherKeySet& publisher) {
    RAWFRAME_TRY_ASSIGN(const signature::Envelope kEnvelope,
                        signature::readEnvelope(
                            std::string_view{reinterpret_cast<const char*>(signedBytes.data()), signedBytes.size()}));
    RAWFRAME_TRY(signature::verifyPublished(publisher, manifest, kEnvelope));
    auto parsed = document::parseCanonicalRecord(
        std::string_view{reinterpret_cast<const char*>(manifest.data()), manifest.size()},
        document::ReadLimits{.maximumBytes = kMaximumBuildManifest});
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{
            std::move(parsed).error().mappedTo(result::ErrorClass::InvalidArgument,
                                               kContentDomain,
                                               code(ContentError::ManifestInvalid),
                                               "the Build manifest is not a canonical record")};
    }
    const document::Value& record = *parsed;
    const document::Value* schema = record.find("schema");
    const document::Value* identity = record.find("identity");
    const document::Value* chunks = record.find("chunks");
    if (record.names().size() != 3 || schema == nullptr || schema->integer() != 1 || identity == nullptr ||
        identity->kind() != document::Value::Kind::Object || chunks == nullptr ||
        chunks->kind() != document::Value::Kind::Object) {
        return invalidBuild("a Build manifest is {schema: 1, identity, chunks}");
    }
    // What the Build is, checked first: its identity section is the root.
    RAWFRAME_TRY_ASSIGN(const std::string kIdentity, document::writeCanonicalRecord(*identity));
    BuildContent opened{
        .root = base::sha256(kIdentity), .subject = {}, .version = {}, .entries = {}, .source = ContentSource{nullptr}};
    if (opened.root != expectedRoot) {
        return refuse(ContentError::DigestMismatch, result::ErrorClass::DataLoss, "not the Build that was named");
    }
    const std::string* subject = textOf(*identity, "subject");
    const std::string* version = textOf(*identity, "version");
    const document::Value* resources = identity->find("resources");
    if (subject == nullptr || version == nullptr || resources == nullptr ||
        resources->kind() != document::Value::Kind::Array || resources->items().size() > kMaximumBuildResources ||
        resources->items().size() != chunks->items().size()) {
        return invalidBuild("a Build identity names its subject, version, and resources, each with its chunks");
    }
    // Signed by this publisher's key, so a Build of this publisher's only.
    if (publisherOf(*subject) != publisher.publisher) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::PermissionDenied,
                                                           signature::kSignatureDomain,
                                                           code(signature::SignatureError::UnknownKey),
                                                           "the Build is another publisher's")
                                                  .error()};
    }
    opened.subject = *subject;
    opened.version = *version;
    std::map<std::string, std::vector<Chunk>, std::less<>> lists;
    for (const document::Value& each : resources->items()) {
        const std::string* resource = textOf(each, "resource");
        const std::string* type = textOf(each, "type");
        const std::string* representation = textOf(each, "representation");
        const std::string* digest = textOf(each, "digest");
        const auto kSize = sizeOf(each, "size");
        if (each.kind() != document::Value::Kind::Object || each.names().size() != 5 || resource == nullptr ||
            type == nullptr || representation == nullptr || digest == nullptr || !kSize.has_value()) {
            return invalidBuild("a Build resource is {resource, type, representation, digest, size}");
        }
        const base::Bits128Parse kId = base::parseBits128Hex(*resource);
        const base::Bits128Parse kType = base::parseBits128Hex(*type);
        const auto kRepresentation = RepresentationId::parse(*representation);
        const auto kDigest = ContentDigest::parse(*digest);
        if (!kId.parsed || !kType.parsed || !kRepresentation.has_value() || !kDigest.has_value() ||
            !ResourceId{kId.value}.valid() || !ResourceTypeId{kType.value}.valid()) {
            return invalidBuild("a Build resource's identity, type, representation, or digest is malformed");
        }
        const document::Value* list = chunks->find(*resource);
        if (list == nullptr) {
            return invalidBuild("a Build resource has no chunk list");
        }
        RAWFRAME_TRY_ASSIGN(std::vector<Chunk> made, chunksOf(*list, *kSize));
        if (!lists.emplace(*resource, std::move(made)).second) {
            return refuse(ContentError::DuplicateResource, result::ErrorClass::InvalidArgument, "a resource twice");
        }
        opened.entries.push_back(ManifestEntry{.id = ResourceId{kId.value},
                                               .type = ResourceTypeId{kType.value},
                                               .representation = *kRepresentation,
                                               .byteLength = *kSize,
                                               .digest = *kDigest,
                                               .locator = *resource});
    }
    opened.source = ContentSource{std::make_shared<const BuildSource>(std::move(files), std::move(lists))};
    return opened;
}

#if RAWFRAME_FILE_SYSTEM
result::Result<BuildContent> ContentSource::build(const std::filesystem::path& root,
                                                  const base::Sha256Digest& expectedRoot,
                                                  const signature::PublisherKeySet& publisher) {
    Descriptor directory{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    struct stat status{};
    if (directory.get() < 0 || ::fstat(directory.get(), &status) != 0) {
        return refuse(
            ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the Build is not a readable directory");
    }
    auto files = std::make_shared<const DirectorySource>(std::move(directory), status.st_dev);
    struct stat manifestStatus{};
    if (::stat((root / "build.manifest").c_str(), &manifestStatus) != 0 || !S_ISREG(manifestStatus.st_mode) ||
        static_cast<std::uint64_t>(manifestStatus.st_size) > kMaximumBuildManifest) {
        return refuse(ContentError::SourceUnavailable,
                      result::ErrorClass::Unavailable,
                      "the Build has no manifest within its ceiling");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes,
                        files->read("build.manifest", static_cast<std::uint64_t>(manifestStatus.st_size)));
    struct stat signatureStatus{};
    if (::stat((root / "build.manifest.sig").c_str(), &signatureStatus) != 0 || !S_ISREG(signatureStatus.st_mode) ||
        signatureStatus.st_size > 1024) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the Build is not signed");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kSigned,
                        files->read("build.manifest.sig", static_cast<std::uint64_t>(signatureStatus.st_size)));
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
    const auto kManifest = held.find("build.manifest");
    if (kManifest == held.end() || kManifest->second.size() > kMaximumBuildManifest) {
        return refuse(ContentError::SourceUnavailable,
                      result::ErrorClass::Unavailable,
                      "the Build has no manifest within its ceiling");
    }
    const auto kSignature = held.find("build.manifest.sig");
    if (kSignature == held.end() || kSignature->second.size() > 1024) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::Unavailable, "the Build is not signed");
    }
    const std::vector<std::byte> kBytes = kManifest->second;
    const std::vector<std::byte> kSigned = kSignature->second;
    return openBuild(std::make_shared<const MemorySource>(std::move(held)), kBytes, kSigned, expectedRoot, publisher);
}

} // namespace rawframe::content
