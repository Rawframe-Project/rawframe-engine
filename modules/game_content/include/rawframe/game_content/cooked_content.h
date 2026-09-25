#pragma once

// A game's cooked content as a process's content: a cook's output directory,
// its `content.manifest` read into catalogs that hold what the process
// admits, or a Composition (SPEC-0021) whose Builds are read in their
// verification order, from a library directory or from a library's files
// held in memory, as a web client holds what it fetched. Directories exist
// only where there are files (RAWFRAME_FILE_SYSTEM, D159).

#include "rawframe/base/platform.h"
#include "rawframe/content/source.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/game_content/game_content.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::game_content {

/// A library's files by their paths within it (`keys/<publisher>.keys`,
/// `builds/<root>/build.manifest` and the rest of each Build), held in
/// memory.
using HeldLibrary = std::vector<std::pair<std::string, std::vector<std::byte>>>;

class CookedContent final : public GameContent {
public:
    /// No content: a store with no catalog that refuses admission.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>> none(execution::Executor& blockingIo,
                                                                             execution::OwnerId owner,
                                                                             execution::CancellationScope& parent,
                                                                             const execution::MonotonicSource& clock);

#if RAWFRAME_FILE_SYSTEM
    /// The content at `root`, read on `blockingIo` as `owner`; with no root,
    /// a store with no catalog that refuses admission. Refuses a root that
    /// is not a directory and a manifest that does not read.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>> open(execution::Executor& blockingIo,
                                                                             execution::OwnerId owner,
                                                                             execution::CancellationScope& parent,
                                                                             const execution::MonotonicSource& clock,
                                                                             std::optional<std::filesystem::path> root);

    /// The Composition whose canonical record is `record` (SPEC-0021): its
    /// Game Build and Packages, each read from `library/builds/<root>/` (the
    /// root's 64 hexadecimal digits) and verified against its publisher's
    /// key set `library/keys/<publisher>.keys`, pinned there, and each the
    /// subject and version the record names (`ManifestInvalid`). Its Mods
    /// are read the same way after its Packages; whether the game takes them
    /// is the game's to decide (D179). A resource two of them hold is refused
    /// as a catalog refuses it. Builds never change, so `refresh` finds
    /// nothing to publish.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>>
    openComposition(execution::Executor& blockingIo,
                    execution::OwnerId owner,
                    execution::CancellationScope& parent,
                    const execution::MonotonicSource& clock,
                    std::string_view record,
                    const std::filesystem::path& library);
#endif

    /// The same Composition from a library's files held in memory, read and
    /// refused exactly as from a directory.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>>
    openComposition(execution::Executor& blockingIo,
                    execution::OwnerId owner,
                    execution::CancellationScope& parent,
                    const execution::MonotonicSource& clock,
                    std::string_view record,
                    HeldLibrary library);

    [[nodiscard]] content::ContentStore& store() noexcept override;
    [[nodiscard]] bool held() const noexcept override {
        return held_;
    }
    [[nodiscard]] result::Status admit(std::span<const content::AdmittedRepresentation> representations) override;

    /// Reads a cook's output's manifest again: true when it changed and was
    /// published as the next catalog, false when it did not change or there
    /// is no cook's output; a changed one that does not read is refused and
    /// the running catalog stays.
    [[nodiscard]] result::Result<bool> refresh();

    /// The generation of the catalog last published; 0 before any.
    [[nodiscard]] std::uint64_t generation() const noexcept;

    [[nodiscard]] const std::optional<base::Sha256Digest>& compositionId() const noexcept override;
    [[nodiscard]] const ComposedBuild* composedGame() const noexcept override;
    [[nodiscard]] std::span<const ComposedBuild> composedMods() const noexcept override;

private:
    /// A key set's text by its publisher, and a Build by its root.
    using KeysOf = std::function<result::Result<std::string>(std::string_view publisher)>;
    using BuildOf = std::function<result::Result<content::BuildContent>(
        std::string_view root, const base::Sha256Digest& digest, const signature::PublisherKeySet& keys)>;

    CookedContent() = default;
    /// A Composition's Builds, however the library holds them.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>> compose(execution::Executor& blockingIo,
                                                                                execution::OwnerId owner,
                                                                                execution::CancellationScope& parent,
                                                                                const execution::MonotonicSource& clock,
                                                                                std::string_view record,
                                                                                const KeysOf& keysOf,
                                                                                const BuildOf& buildOf);
    /// Each source's entries of admitted representations, published as the
    /// next catalog.
    result::Status publish(const std::vector<std::vector<content::ManifestEntry>>& manifests);

    std::unique_ptr<content::ContentStore> store_;
#if RAWFRAME_FILE_SYSTEM
    /// The cook's output to watch; none for a Build.
    std::optional<std::filesystem::path> root_;
#endif
    /// Whether there is content at all to admit into catalogs.
    bool held_ = false;
    std::string manifestText_;
    /// Each source's manifest entries, by source index.
    std::vector<std::vector<content::ManifestEntry>> manifests_;
    std::optional<base::Sha256Digest> compositionId_;
    std::optional<ComposedBuild> composedGame_;
    std::vector<ComposedBuild> composedMods_;
    std::vector<content::AdmittedRepresentation> admitted_;
    std::uint64_t generation_ = 0;
};

} // namespace rawframe::game_content
