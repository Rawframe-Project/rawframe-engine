#pragma once

// A Build (ADR-0023, SPEC-0021) from a cook's output (ADR-0024): the
// receipt is the only proof accepted, every artifact is checked against the
// receipt and the manifest it names, each resource is chunked and stored
// by digest, and the BuildManifest names the Build by the hash of its
// identity section. Generation 2 of this packer compresses each chunk worth
// compressing (one Zstandard frame under pinned parameters) and signs the
// manifest's exact bytes with the publisher's key into
// `build.manifest.sig`.

#include "rawframe/base/sha256.h"
#include "rawframe/build/publisher_key.h"
#include "rawframe/content/identity.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace rawframe::build {

/// What a Build is of: ADR-0023's Build dimensions.
struct BuildIdentity {
    /// `publisher/name`, each segment `[a-z0-9]([a-z0-9-]*[a-z0-9])?`.
    std::string subject;
    /// Semantic Versioning 2.0.0, at most 64 bytes.
    std::string version;
    /// The engine's own version, likewise.
    std::string engine;
    /// Lowercase tokens: `linux`, `x86_64`.
    std::string platform;
    std::string architecture;
    /// `client` or `server`.
    std::string side;
    /// `build.debug`, `build.development`, or `build.shipping` (SPEC-0003).
    std::string configuration;
    /// A profile token.
    std::string profile;
};

struct BuildRequest {
    /// A cook's output: `cook.receipt`, `content.manifest`, and its objects.
    std::filesystem::path cooked;
    /// Where the Build goes: `build.manifest`, `packaging.receipt`, and
    /// blobs under `sha256/`.
    std::filesystem::path output;
    BuildIdentity identity;
    /// The publisher's key, to sign the manifest with; its publisher must
    /// be the subject's. Without one the Build is unsigned, and no reader
    /// takes it.
    const PublisherKey* signer = nullptr;
};

struct BuildReport {
    /// SHA-256 of the canonical identity section: the Build's identity.
    base::Sha256Digest root{};
    /// The BuildManifest's own digest: its physical identity.
    content::ContentDigest manifest;
    std::size_t resources = 0;
    std::size_t blobsWritten = 0;
    std::size_t blobsReused = 0;
};

/// Packs `request.cooked` into a Build at `request.output`. Nothing is
/// published unless every check passes: the manifest is written after
/// every blob, the packaging receipt last.
[[nodiscard]] result::Result<BuildReport> packBuild(const BuildRequest& request);

/// Whether `text` is a Semantic Versioning 2.0.0 version.
[[nodiscard]] bool validVersion(std::string_view text) noexcept;
/// Whether `text` is a `publisher/name` subject.
[[nodiscard]] bool validSubject(std::string_view text) noexcept;

} // namespace rawframe::build
