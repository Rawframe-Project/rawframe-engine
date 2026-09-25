#pragma once

// A game's cooked content as a process's content: a cook's output directory,
// its `content.manifest` read into catalogs that hold what the process
// admits, or a Composition (SPEC-0021) whose Builds are read in their
// verification order.

#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/game_content/game_content.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::game_content {

class CookedContent final : public GameContent {
public:
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
    /// subject and version the record names (`ManifestInvalid`). A resource
    /// two of them hold is refused as a catalog refuses it. Mods are refused
    /// until mod policy exists. Builds never change, so `refresh` finds
    /// nothing to publish.
    [[nodiscard]] static result::Result<std::unique_ptr<CookedContent>>
    openComposition(execution::Executor& blockingIo,
                    execution::OwnerId owner,
                    execution::CancellationScope& parent,
                    const execution::MonotonicSource& clock,
                    std::string_view record,
                    const std::filesystem::path& library);

    [[nodiscard]] content::ContentStore& store() noexcept override;
    [[nodiscard]] result::Status admit(std::span<const content::AdmittedRepresentation> representations) override;

    /// Reads the manifest again: true when it changed and was published as
    /// the next catalog, false when it did not change; a changed one that
    /// does not read is refused and the running catalog stays.
    [[nodiscard]] result::Result<bool> refresh();

    /// The generation of the catalog last published; 0 before any.
    [[nodiscard]] std::uint64_t generation() const noexcept;

    /// The CompositionId of the Composition this content is, if it is one.
    [[nodiscard]] const std::optional<base::Sha256Digest>& compositionId() const noexcept;

private:
    CookedContent() = default;
    /// Each source's entries of admitted representations, published as the
    /// next catalog.
    result::Status publish(const std::vector<std::vector<content::ManifestEntry>>& manifests);

    std::unique_ptr<content::ContentStore> store_;
    /// The cook's output to watch; none for a Build.
    std::optional<std::filesystem::path> root_;
    /// Whether there is content at all to admit into catalogs.
    bool held_ = false;
    std::string manifestText_;
    /// Each source's manifest entries, by source index.
    std::vector<std::vector<content::ManifestEntry>> manifests_;
    std::optional<base::Sha256Digest> compositionId_;
    std::vector<content::AdmittedRepresentation> admitted_;
    std::uint64_t generation_ = 0;
};

} // namespace rawframe::game_content
