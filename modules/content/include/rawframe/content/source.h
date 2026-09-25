#pragma once

// Where content bytes come from (SPEC-0008): a source reads a validated
// locator's exact bytes and nothing else. Generation 1 has three, all built
// here and nowhere else: a memory source for tests and fixtures, a
// directory source confined to its root, and a Build (SPEC-0021) read
// chunk by chunk in its verification order. There is no public way to add
// another kind.

#include "rawframe/base/sha256.h"
#include "rawframe/content/manifest.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::content {

class ContentStore;
struct BuildContent;

class ContentSource {
public:
    /// Bytes held in memory, by locator. Refuses (`InvalidLocator`) a
    /// locator `validLocator` does not accept.
    [[nodiscard]] static result::Result<ContentSource>
    memory(std::vector<std::pair<std::string, std::vector<std::byte>>> files);

    /// Files under `root`, which must be a directory (`SourceUnavailable`).
    /// A read walks each segment of its locator from the root without
    /// following a link and without crossing onto another filesystem
    /// (`PathEscape`), and lists nothing: only what a catalog names is read.
    [[nodiscard]] static result::Result<ContentSource> directory(const std::filesystem::path& root);

    /// The Build at `root` (its `build.manifest` and blobs under `sha256/`),
    /// which must be the one whose identity section hashes to `expectedRoot`
    /// (`DigestMismatch`); a manifest that is not a canonical SPEC-0021
    /// record, lists a resource twice, or whose chunk lists do not cover
    /// their resources exactly is refused (`ManifestInvalid`,
    /// `DuplicateResource`). Its entries are located by their resource's
    /// 32-hex identity. A read verifies each blob before using it, each
    /// chunk's content, and the whole; the store verifies the whole again.
    [[nodiscard]] static result::Result<BuildContent> build(const std::filesystem::path& root,
                                                            const base::Sha256Digest& expectedRoot);

    struct Implementation;

private:
    friend class ContentStore;
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
