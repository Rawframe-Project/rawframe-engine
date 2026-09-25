#pragma once

// The content catalog (SPEC-0008): every selected manifest combined into one
// immutable lookup of resources, each bound to the source that holds its
// bytes, with a fingerprint of what it means and nothing of where it lives.

#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/manifest.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::content {

/// One resource as the catalog knows it.
struct ContentDescriptor {
    ResourceId id;
    ResourceTypeId type;
    RepresentationId representation;
    std::uint64_t byteLength = 0;
    ContentDigest digest;
    std::string locator;
    /// The source its bytes come from, by the index composition gave it.
    std::size_t source = 0;
};

/// A type of resource and a representation of it that this composition
/// admits.
struct AdmittedRepresentation {
    ResourceTypeId type;
    RepresentationId representation;
};

/// SHA-256 over the catalog's meaning: every entry's identity, type,
/// representation, length, and digest, in resource order; never its
/// locators, sources, or the order manifests came in.
using CatalogFingerprint = base::Sha256Digest;

/// One manifest's entries and the source they are read from.
struct BoundManifest {
    std::vector<ManifestEntry> entries;
    std::size_t source = 0;
};

class ContentCatalog {
public:
    /// Combines `manifests` into one catalog, or nothing: refuses a source
    /// index at or past `sources` (`SourceUnavailable`), any resource listed
    /// twice even identically (`DuplicateResource`), and a type and
    /// representation pair not in `admitted` (`UnsupportedRepresentation`).
    [[nodiscard]] static result::Result<std::shared_ptr<const ContentCatalog>>
    build(std::span<const BoundManifest> manifests,
          std::span<const AdmittedRepresentation> admitted,
          std::size_t sources,
          std::uint64_t generation);

    /// The resource, with no input or output (`ResourceNotFound`).
    [[nodiscard]] result::Result<const ContentDescriptor*> lookup(ResourceId id) const;
    /// The resource if it is of the expected type (`ResourceTypeMismatch`).
    [[nodiscard]] result::Result<const ContentDescriptor*> resolve(const ResourceRef& reference) const;
    /// The resource if it is also at exactly that revision
    /// (`RevisionMismatch`).
    [[nodiscard]] result::Result<const ContentDescriptor*> resolve(const PinnedResourceRef& reference) const;

    [[nodiscard]] const CatalogFingerprint& fingerprint() const noexcept {
        return fingerprint_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return descriptors_.size();
    }

private:
    ContentCatalog() = default;

    /// In ascending resource order.
    std::vector<ContentDescriptor> descriptors_;
    CatalogFingerprint fingerprint_{};
    std::uint64_t generation_ = 0;
};

} // namespace rawframe::content
