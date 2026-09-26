#pragma once

// An UpdatePlan (SPEC-0038): exactly the blobs an update needs that the
// library's store lacks, a pure function of the target Builds' manifests and
// the store's inventory. Identical inputs make byte-identical canonical
// records. A plan names digests only, never a path, and is never an
// authority: it is recomputed wherever it is carried out, and how its blobs
// are fetched (order, parallelism, origin) is no part of it.

#include "rawframe/base/sha256.h"
#include "rawframe/content/build_manifest.h"
#include "rawframe/content/identity.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace rawframe::install {

/// The blob digests a store holds.
using Inventory = std::set<base::Sha256Digest>;

struct PlanEntry {
    content::ContentDigest blob;
    std::uint64_t blobSize = 0;
    content::ContentDigest content;
    /// The digest of the manifest that first names the blob.
    content::ContentDigest manifest;
};

struct UpdatePlan {
    /// In the targets' order, each manifest's resources and chunks in its
    /// own order, each blob once.
    std::vector<PlanEntry> entries;

    /// What the plan fetches.
    [[nodiscard]] std::uint64_t bytes() const noexcept;
};

/// SPEC-0038's ceiling on a plan's canonical record.
inline constexpr std::size_t kMaximumPlanRecord = std::size_t{64} * 1024 * 1024;

/// Every blob `targets` name that `inventory` lacks.
[[nodiscard]] UpdatePlan planUpdate(std::span<const content::BuildManifest> targets, const Inventory& inventory);

/// The plan's canonical record, `{schema: 1, entries: [{blob, blob_size,
/// content, manifest}]}`; refused (`OverLimit`) past kMaximumPlanRecord.
[[nodiscard]] result::Result<std::string> writePlan(const UpdatePlan& plan);

} // namespace rawframe::install
