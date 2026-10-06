#pragma once

// A library (SPEC-0021 storage, SPEC-0038): the Builds one machine holds.
// Each Build's manifest and signature are under `builds/<root>/`, named by
// its root hash's 64 hexadecimal digits; the blobs of every Build are in one
// content-addressed store, `sha256/<2>/<62>`, so a blob two Builds share is
// held once and an update fetches only what the store lacks; and the key set
// pinned for each publisher is `keys/<publisher>.keys`. An installed library
// (rawframe.install) also keeps the Compositions it has installed under
// `compositions/`, names the active one and those retained for rollback in
// `installed`, and writes under `staging/` before it publishes. This module
// reads the layout and names its paths; nothing else spells them.

#include "rawframe/base/platform.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/build_manifest.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/source.h"
#include "rawframe/result/result.h"
#include "rawframe/signature/signature.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::content {

/// A Build's own files, in a library's `builds/<root>/` or a packed Build's
/// directory.
inline constexpr std::string_view kBuildManifestName = "build.manifest";
inline constexpr std::string_view kBuildSignatureName = "build.manifest.sig";

/// `builds/` and the root's 64 hexadecimal digits.
[[nodiscard]] std::string buildDirectoryOf(const base::Sha256Digest& root);
/// `sha256/`, the blob digest's first two hexadecimal digits, `/`, and the
/// other 62.
[[nodiscard]] std::string blobPathOf(const ContentDigest& blob);
/// `keys/<publisher>.keys`.
[[nodiscard]] std::string keysPathOf(std::string_view publisher);
/// `compositions/` and the CompositionId's 64 hexadecimal digits.
[[nodiscard]] std::string compositionPathOf(const base::Sha256Digest& composition);
/// SPEC-0020's records a mirror serves (D424): `releases/` and a Release's
/// 64 hexadecimal digits, and `channels/<publisher>/<name>/<channel>`, a
/// subject's channel pointer. Each signature is beside its record, under
/// the same name with `.sig`; a library keeps the sequence of the pointer
/// it last followed under the pointer's name with `.sequence`.
[[nodiscard]] std::string releasePathOf(const base::Sha256Digest& release);
[[nodiscard]] std::string channelPathOf(std::string_view subject, std::string_view channel);
inline constexpr std::string_view kSignatureSuffix = ".sig";
inline constexpr std::string_view kSequenceSuffix = ".sequence";
/// The installed pointer, and where an update stages what it writes.
inline constexpr std::string_view kInstalledName = "installed";
inline constexpr std::string_view kStagingName = "staging";

class Library {
public:
#if RAWFRAME_FILE_SYSTEM
    /// The library at `root`, which must be a directory
    /// (`SourceUnavailable`), read as a directory source reads: no link
    /// followed, no other filesystem entered.
    [[nodiscard]] static result::Result<Library> directory(const std::filesystem::path& root);
#endif

    /// A library's files held in memory by their paths within it, as a web
    /// client holds what it fetched. Refuses (`InvalidLocator`) a path
    /// `validLocator` does not accept.
    [[nodiscard]] static result::Result<Library>
    memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files);

    /// The key set pinned for `publisher`, at most 16 KiB; refused as its
    /// read or `readPublisherKeySet` refuses. Blocks.
    [[nodiscard]] result::Result<signature::PublisherKeySet> keys(std::string_view publisher) const;

    /// The manifest of the Build of root hash `root`, read as
    /// `readBuildManifest` reads one against `publisher`; refused
    /// (`SourceUnavailable`) when the library holds none. Blocks.
    [[nodiscard]] result::Result<BuildManifest> manifest(const base::Sha256Digest& root,
                                                         const signature::PublisherKeySet& publisher) const;

    /// The Build of root hash `root`, verified against `publisher` as
    /// `ContentSource::build` verifies one, its blobs read from the store.
    /// Blocks.
    [[nodiscard]] result::Result<BuildContent> build(const base::Sha256Digest& root,
                                                     const signature::PublisherKeySet& publisher) const;

    /// What heal finds (SPEC-0038): every blob of `manifest`'s Build that the
    /// store lacks or that does not verify as SPEC-0021's order verifies it,
    /// once each, in the manifest's order; none when every resource reads.
    /// A Build whose chunks all verify but do not make a resource is the
    /// publisher's fault, not the store's, and is refused (`DigestMismatch`).
    /// Blocks, and reads every blob.
    [[nodiscard]] result::Result<std::vector<ContentDigest>> damaged(const BuildManifest& manifest) const;

private:
    explicit Library(std::shared_ptr<const ContentSource::Implementation> files) noexcept : files_(std::move(files)) {
    }
    std::shared_ptr<const ContentSource::Implementation> files_;
};

} // namespace rawframe::content
