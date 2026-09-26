#pragma once

// A Build's manifest (SPEC-0021) as the first step of its verification order
// reads it: its exact bytes signed by the publisher before a byte of them is
// parsed, then the Build it names, then its resources, each with the chunks
// that make it. Opening a Build and updating a library (SPEC-0038) both read
// a manifest through this one reader.

#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/manifest.h"
#include "rawframe/result/result.h"
#include "rawframe/signature/signature.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rawframe::content {

/// SPEC-0021's hard ceiling on a Build manifest, and a signature envelope's
/// own.
inline constexpr std::size_t kMaximumBuildManifest = std::size_t{64} * 1024 * 1024;
inline constexpr std::size_t kMaximumBuildSignature = 1024;

/// SPEC-0021's chunk, as the manifest lists it: a `raw` blob is its
/// content, a `zstd` blob one Zstandard frame of it.
struct BuildChunk {
    ContentDigest content;
    std::uint64_t size = 0;
    ContentDigest blob;
    std::uint64_t blobSize = 0;
    bool compressed = false;
};

struct BuildManifest {
    base::Sha256Digest root{};
    /// The digest of the manifest's exact bytes: the Build's physical
    /// identity, which changes when it is repacked though its root does not.
    ContentDigest descriptor;
    std::string subject;
    std::string version;
    /// Its resources in the manifest's order, each located by its 32-hex
    /// identity, and each one's chunks at the same index.
    std::vector<ManifestEntry> entries;
    std::vector<std::vector<BuildChunk>> chunks;
};

/// The manifest `manifest`, signed by `signature` with a key `publisher`
/// lists that is not revoked (the signature module's typed refusals), of a
/// Build of that publisher's (`UnknownKey`) whose identity section hashes to
/// `expectedRoot` (`DigestMismatch`). Refused (`ManifestInvalid`,
/// `DuplicateResource`) when it is not a canonical SPEC-0021 record within
/// its ceilings, lists a resource twice, or has chunk lists that do not
/// cover their resources exactly.
[[nodiscard]] result::Result<BuildManifest> readBuildManifest(std::span<const std::byte> manifest,
                                                              std::span<const std::byte> signature,
                                                              const base::Sha256Digest& expectedRoot,
                                                              const signature::PublisherKeySet& publisher);

} // namespace rawframe::content
