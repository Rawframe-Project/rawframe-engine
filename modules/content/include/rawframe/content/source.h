#pragma once

// Where content bytes come from (SPEC-0008): a source reads a validated
// locator's exact bytes and nothing else. Generation 1 has three, all built
// here and nowhere else: a memory source, a directory source confined to
// its root, and a Build (SPEC-0021) read chunk by chunk in its verification
// order, whose files are in a directory or held in memory. There is no
// public way to add another kind.
//
// Directories exist only where there are files (RAWFRAME_FILE_SYSTEM): a
// web client holds in memory what it fetched, a Build's files among them
// (D162).

#include "rawframe/base/platform.h"
#include "rawframe/base/sha256.h"
#include "rawframe/content/manifest.h"
#include "rawframe/result/result.h"
#include "rawframe/signature/signature.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::content {

class ContentStore;
struct BuildContent;

class ContentSource {
public:
    /// Bytes held in memory, by locator. Refuses (`InvalidLocator`) a
    /// locator `validLocator` does not accept.
    [[nodiscard]] static result::Result<ContentSource>
    memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files);

#if RAWFRAME_FILE_SYSTEM
    /// Files under `root`, which must be a directory (`SourceUnavailable`).
    /// A read walks each segment of its locator from the root without
    /// following a link and without crossing onto another filesystem
    /// (`PathEscape`), and lists nothing: only what a catalog names is read.
    [[nodiscard]] static result::Result<ContentSource> directory(const std::filesystem::path& root);

    /// The Build at `root` (its `build.manifest`, `build.manifest.sig`, and
    /// blobs under `sha256/`). Its manifest's exact bytes must be signed by
    /// a key `publisher` lists that is not revoked, before anything else is
    /// read of them (the signature module's typed refusals), and its
    /// subject must be that publisher's (`UnknownKey`). It must be the Build
    /// whose identity section hashes to `expectedRoot` (`DigestMismatch`); a
    /// manifest that is not a canonical SPEC-0021
    /// record, lists a resource twice, or whose chunk lists do not cover
    /// their resources exactly is refused (`ManifestInvalid`,
    /// `DuplicateResource`). Its entries are located by their resource's
    /// 32-hex identity. A read verifies each blob before using it, each
    /// chunk's content, and the whole; the store verifies the whole again.
    [[nodiscard]] static result::Result<BuildContent> build(const std::filesystem::path& root,
                                                            const base::Sha256Digest& expectedRoot,
                                                            const signature::PublisherKeySet& publisher);
#endif

    /// A Build whose files are held in memory by their paths within it
    /// (`build.manifest`, `build.manifest.sig`, `sha256/<2>/<62>`), as a
    /// web client holds what it fetched, and verified exactly as a Build
    /// in a directory is. Refuses (`InvalidLocator`) a path `validLocator`
    /// does not accept.
    [[nodiscard]] static result::Result<BuildContent>
    build(std::vector<std::pair<std::string, std::vector<std::byte>>> files,
          const base::Sha256Digest& expectedRoot,
          const signature::PublisherKeySet& publisher);

    struct Implementation;

private:
    friend class ContentStore;
    /// SPEC-0021's verification order over a Build's files, wherever they
    /// are held.
    [[nodiscard]] static result::Result<BuildContent> openBuild(std::shared_ptr<const Implementation> files,
                                                                std::span<const std::byte> manifest,
                                                                std::span<const std::byte> signedBytes,
                                                                const base::Sha256Digest& expectedRoot,
                                                                const signature::PublisherKeySet& publisher);
    explicit ContentSource(std::shared_ptr<const Implementation> implementation) noexcept
        : implementation_(std::move(implementation)) {
    }
    std::shared_ptr<const Implementation> implementation_;
};

/// An opened Build: what it is, its resources as manifest entries, and the
/// source that reads them.
struct BuildContent {
    base::Sha256Digest root{};
    std::string subject;
    std::string version;
    std::vector<ManifestEntry> entries;
    ContentSource source;
};

} // namespace rawframe::content
