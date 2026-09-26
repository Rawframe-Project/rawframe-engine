#pragma once

// A library (SPEC-0021 storage, SPEC-0038): the Builds one machine holds.
// Each Build's manifest and signature are under `builds/<root>/`, named by
// its root hash's 64 hexadecimal digits; the blobs of every Build are in one
// content-addressed store, `sha256/<2>/<62>`, so a blob two Builds share is
// held once and an update fetches only what the store lacks; and the key set
// pinned for each publisher is `keys/<publisher>.keys`. This module reads
// the layout and names its paths; nothing else spells them.

#include "rawframe/base/platform.h"
#include "rawframe/base/sha256.h"
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

    /// The Build of root hash `root`, verified against `publisher` as
    /// `ContentSource::build` verifies one, its blobs read from the store.
    /// Blocks.
    [[nodiscard]] result::Result<BuildContent> build(const base::Sha256Digest& root,
                                                     const signature::PublisherKeySet& publisher) const;

private:
    explicit Library(std::shared_ptr<const ContentSource::Implementation> files) noexcept : files_(std::move(files)) {
    }
    std::shared_ptr<const ContentSource::Implementation> files_;
};

} // namespace rawframe::content
