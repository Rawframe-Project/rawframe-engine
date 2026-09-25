#pragma once

// The cook's output directory as a process's content: its `content.manifest`
// read into catalogs that hold what the process admits, over the directory
// as the one source.

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

    [[nodiscard]] content::ContentStore& store() noexcept override;
    [[nodiscard]] result::Status admit(std::span<const content::AdmittedRepresentation> representations) override;

    /// Reads the manifest again: true when it changed and was published as
    /// the next catalog, false when it did not change; a changed one that
    /// does not read is refused and the running catalog stays.
    [[nodiscard]] result::Result<bool> refresh();

    /// The generation of the catalog last published; 0 before any.
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    CookedContent() = default;
    /// The manifest's entries of admitted representations, published as
    /// the next catalog.
    result::Status publish(const std::vector<content::ManifestEntry>& entries);

    std::unique_ptr<content::ContentStore> store_;
    std::optional<std::filesystem::path> root_;
    std::string manifestText_;
    std::vector<content::ManifestEntry> entries_;
    std::vector<content::AdmittedRepresentation> admitted_;
    std::uint64_t generation_ = 0;
};

} // namespace rawframe::game_content
