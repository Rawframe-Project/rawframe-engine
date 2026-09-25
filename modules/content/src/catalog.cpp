#include "rawframe/content/catalog.h"

#include "rawframe/content/errors.h"

#include <algorithm>
#include <array>

namespace rawframe::content {

namespace {

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

void feedBig(base::Sha256& digest, std::uint64_t value, std::size_t bytes) {
    std::array<std::byte, 8> out{};
    for (std::size_t index = 0; index < bytes; ++index) {
        out[index] = static_cast<std::byte>((value >> (8 * (bytes - 1 - index))) & 0xFFU);
    }
    digest.update(std::span{out}.first(bytes));
}

void feedIdentity(base::Sha256& digest, base::Bits128 value) {
    feedBig(digest, value.high, 8);
    feedBig(digest, value.low, 8);
}

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

} // namespace

result::Result<std::shared_ptr<const ContentCatalog>>
ContentCatalog::build(std::span<const BoundManifest> manifests,
                      std::span<const AdmittedRepresentation> admitted,
                      std::size_t sources,
                      std::uint64_t generation) {
    std::shared_ptr<ContentCatalog> catalog{new ContentCatalog{}};
    catalog->generation_ = generation;
    for (const BoundManifest& manifest : manifests) {
        if (manifest.source >= sources) {
            return refuse(ContentError::SourceUnavailable,
                          result::ErrorClass::FailedPrecondition,
                          "a manifest is bound to no source");
        }
        for (const ManifestEntry& entry : manifest.entries) {
            const bool kAdmitted = std::ranges::any_of(admitted, [&entry](const AdmittedRepresentation& each) {
                return each.type == entry.type && each.representation == entry.representation;
            });
            if (!kAdmitted) {
                return std::unexpected<result::Error>{refuse(ContentError::UnsupportedRepresentation,
                                                             result::ErrorClass::Unsupported,
                                                             "this composition does not admit that type and "
                                                             "representation")
                                                          .error()
                                                          .withContext("resource", hexOf(entry.id.value))
                                                          .withContext("representation", entry.representation.text())};
            }
            catalog->descriptors_.push_back(ContentDescriptor{.id = entry.id,
                                                              .type = entry.type,
                                                              .representation = entry.representation,
                                                              .byteLength = entry.byteLength,
                                                              .digest = entry.digest,
                                                              .locator = entry.locator,
                                                              .source = manifest.source});
        }
    }
    // One order whatever order the manifests came in; no entry wins over
    // another.
    std::ranges::sort(catalog->descriptors_, {}, &ContentDescriptor::id);
    const auto kDuplicate = std::ranges::adjacent_find(catalog->descriptors_, {}, &ContentDescriptor::id);
    if (kDuplicate != catalog->descriptors_.end()) {
        return std::unexpected<result::Error>{
            refuse(ContentError::DuplicateResource, result::ErrorClass::Conflict, "a resource is listed twice")
                .error()
                .withContext("resource", hexOf(kDuplicate->id.value))};
    }
    base::Sha256 digest;
    digest.update("rawframe.content.catalog.v1");
    feedBig(digest, catalog->descriptors_.size(), 8);
    for (const ContentDescriptor& descriptor : catalog->descriptors_) {
        feedIdentity(digest, descriptor.id.value);
        feedIdentity(digest, descriptor.type.value);
        feedBig(digest, descriptor.representation.text().size(), 4);
        digest.update(descriptor.representation.text());
        feedBig(digest, descriptor.byteLength, 8);
        feedBig(digest, static_cast<std::uint64_t>(descriptor.digest.algorithm), 1);
        digest.update(descriptor.digest.bytes);
    }
    catalog->fingerprint_ = digest.finish();
    return std::shared_ptr<const ContentCatalog>{std::move(catalog)};
}

result::Result<const ContentDescriptor*> ContentCatalog::lookup(ResourceId id) const {
    const auto kFound = std::ranges::lower_bound(descriptors_, id, {}, &ContentDescriptor::id);
    if (kFound == descriptors_.end() || kFound->id != id) {
        return std::unexpected<result::Error>{
            refuse(ContentError::ResourceNotFound, result::ErrorClass::NotFound, "no such resource")
                .error()
                .withContext("resource", hexOf(id.value))};
    }
    return &*kFound;
}

result::Result<const ContentDescriptor*> ContentCatalog::resolve(const ResourceRef& reference) const {
    RAWFRAME_TRY_ASSIGN(const ContentDescriptor* descriptor, lookup(reference.id));
    if (descriptor->type != reference.type) {
        return std::unexpected<result::Error>{refuse(ContentError::ResourceTypeMismatch,
                                                     result::ErrorClass::InvalidArgument,
                                                     "the resource is of another type")
                                                  .error()
                                                  .withContext("resource", hexOf(reference.id.value))
                                                  .withContext("expected", hexOf(reference.type.value))};
    }
    return descriptor;
}

result::Result<const ContentDescriptor*> ContentCatalog::resolve(const PinnedResourceRef& reference) const {
    RAWFRAME_TRY_ASSIGN(const ContentDescriptor* descriptor, resolve(reference.resource));
    if (!sameDigest(descriptor->digest, reference.digest)) {
        return std::unexpected<result::Error>{refuse(ContentError::RevisionMismatch,
                                                     result::ErrorClass::FailedPrecondition,
                                                     "the resource is at another revision")
                                                  .error()
                                                  .withContext("resource", hexOf(reference.resource.id.value))};
    }
    return descriptor;
}

} // namespace rawframe::content
