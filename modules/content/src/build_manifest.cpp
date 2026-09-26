#include "rawframe/content/build_manifest.h"

#include "rawframe/content/errors.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"
#include "rawframe/signature/errors.h"
#include "source.h"

#include <set>
#include <string_view>

namespace rawframe::content {

namespace {

/// SPEC-0021's hard ceilings.
constexpr std::size_t kMaximumBuildResources = 262'144;
constexpr std::size_t kMaximumChunks = 65'536;
constexpr std::uint64_t kMaximumChunkSize = std::uint64_t{4} * 1024 * 1024;

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

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

result::Result<std::vector<BuildChunk>> chunksOf(const document::Value& list, std::uint64_t size) {
    if (list.kind() != document::Value::Kind::Array || list.items().empty() || list.items().size() > kMaximumChunks) {
        return invalidBuild("a resource's chunk list is empty, too long, or not a list");
    }
    std::vector<BuildChunk> chunks;
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
        chunks.push_back(BuildChunk{
            .content = *kContent, .size = *kSize, .blob = *kBlob, .blobSize = *kBlobSize, .compressed = kZstd});
    }
    if (covered != size) {
        return invalidBuild("a resource's chunks do not cover it exactly");
    }
    return chunks;
}

} // namespace

// The manifest's exact bytes signed by the publisher before a byte of them
// is parsed, then what the Build is, then its resources.
result::Result<BuildManifest> readBuildManifest(std::span<const std::byte> manifest,
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
    BuildManifest opened{.root = base::sha256(kIdentity),
                         .descriptor = ContentDigest::of(manifest),
                         .subject = {},
                         .version = {},
                         .entries = {},
                         .chunks = {}};
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
    std::set<std::string_view> seen;
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
        RAWFRAME_TRY_ASSIGN(std::vector<BuildChunk> made, chunksOf(*list, *kSize));
        if (!seen.insert(*resource).second) {
            return refuse(ContentError::DuplicateResource, result::ErrorClass::InvalidArgument, "a resource twice");
        }
        opened.entries.push_back(ManifestEntry{.id = ResourceId{kId.value},
                                               .type = ResourceTypeId{kType.value},
                                               .representation = *kRepresentation,
                                               .byteLength = *kSize,
                                               .digest = *kDigest,
                                               .locator = *resource});
        opened.chunks.push_back(std::move(made));
    }
    return opened;
}

} // namespace rawframe::content
