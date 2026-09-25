#pragma once

// A content manifest (SPEC-0008): which resources a source holds, each by
// its identity, type, representation, exact length and digest, and where in
// the source its bytes are. Written as a `content.manifest` document in the
// SPEC-0028 profile.

#include "rawframe/content/identity.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::content {

struct ManifestEntry {
    ResourceId id;
    ResourceTypeId type;
    RepresentationId representation;
    std::uint64_t byteLength = 0;
    ContentDigest digest;
    /// Where in its source, never what it is.
    std::string locator;
};

/// SPEC-0008's named limits for manifests; the product's to set.
struct ManifestLimits {
    std::size_t maximumBytes = std::size_t{16} * 1024 * 1024;
    std::size_t maximumEntries = 65'536;
    std::size_t maximumLocatorBytes = 256;
    std::size_t maximumLocatorSegments = 16;
    std::uint64_t maximumResourceBytes = std::uint64_t{1} << 30U;
};

/// A locator of generation 1: relative, `/`-separated segments of lowercase
/// ASCII letters, digits, `_`, `-`, and `.`, none empty, `.`, or `..`, within
/// the limits. Narrower than SPEC-0008 allows, so no two locators differ
/// only in case or Unicode normalization, on any filesystem.
[[nodiscard]] bool validLocator(std::string_view locator, const ManifestLimits& limits = {}) noexcept;

/// Reads a manifest of format version 1, in any entry order, and returns
/// its entries in ascending resource order. Refuses as SPEC-0008 names:
/// `UnsupportedManifestVersion`, `InvalidResourceId`, `InvalidResourceType`,
/// `UnsupportedRepresentation` (a malformed one), `InvalidLocator`,
/// `DuplicateResource`, `ResourceTooLarge`, and `ManifestInvalid` for the
/// rest; the document profile's own refusals are its.
[[nodiscard]] result::Result<std::vector<ManifestEntry>> readManifest(std::string_view text,
                                                                      const ManifestLimits& limits = {});

/// The canonical manifest of `entries`, in ascending resource order.
[[nodiscard]] std::string writeManifest(std::span<const ManifestEntry> entries);

} // namespace rawframe::content
