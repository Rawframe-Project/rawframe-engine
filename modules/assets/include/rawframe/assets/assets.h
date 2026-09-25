#pragma once

// Decoded assets (ADR-0025, SPEC-0027): requests for a resource that
// coalesce into one load, readiness answered per requester, decoded forms
// held while anyone is interested and evicted in a deterministic order when
// a budget needs the room, and generation-checked handles that fail typed
// once what they named is gone. One asset family per set; the family
// decodes, this module owns the lifecycle.

#include "rawframe/content/store.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::assets {

enum class Readiness : std::uint8_t {
    NotRequested,
    Pending,
    Ready,
    Failed
};

enum class Residency : std::uint8_t {
    Absent,
    Loading,
    Resident,
    Evictable,
    Retiring
};

/// SPEC-0027's closed priority classes.
enum class Priority : std::uint8_t {
    Immediate,
    High,
    Normal,
    Background
};

/// One consumer's interest in one asset.
struct RequesterId {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(const RequesterId&, const RequesterId&) noexcept = default;
};

/// A decoded form, by slot and that slot's use; never durable.
struct AssetHandle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    friend constexpr bool operator==(const AssetHandle&, const AssetHandle&) noexcept = default;
};

/// What a family's decoder made: the form, and the bytes it holds.
struct DecodedForm {
    std::shared_ptr<const void> value;
    std::uint64_t bytes = 0;
};

/// A family's decoder: verified bytes to a decoded form, on a CPU worker,
/// with no input or output of its own.
using DecodeFunction = result::Result<DecodedForm> (*)(const content::VerifiedContent& content);

struct RequestOptions {
    Priority priority = Priority::Normal;
    /// Past this, the request fails `DeadlineExceeded` if not yet ready.
    std::optional<execution::MonotonicInstant> deadline;
};

struct AssetSettings {
    /// The family's resource type: requests of any other are refused.
    content::ResourceTypeId type;
    DecodeFunction decode = nullptr;
    /// The budget domain's hard ceiling for decoded forms, in bytes.
    std::uint64_t budgetBytes = std::uint64_t{64} * 1024 * 1024;
    std::size_t maximumAssets = 4'096;
    std::size_t maximumRequesters = 16'384;
};

struct AssetStatistics {
    std::uint64_t loads = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t evictions = 0;
    std::uint64_t residentBytes = 0;
    std::size_t loading = 0;
    std::size_t resident = 0;
    std::size_t evictable = 0;
};

/// A resource still wanted when its set closed (SPEC-0027 survivor record).
struct Survivor {
    content::ResourceId id;
    content::ContentDigest revision;
    std::uint32_t interest = 0;
};

class AssetSet {
public:
    /// A set reading through `store` and decoding on `cpu` as `owner`
    /// (admitted there) under `parent`'s cancellation.
    [[nodiscard]] static result::Result<std::unique_ptr<AssetSet>> create(content::ContentStore& store,
                                                                          execution::Executor& cpu,
                                                                          execution::OwnerId owner,
                                                                          execution::CancellationScope& parent,
                                                                          const execution::MonotonicSource& clock,
                                                                          const AssetSettings& settings);

    AssetSet(const AssetSet&) = delete;
    AssetSet& operator=(const AssetSet&) = delete;
    /// Closes if `close` was not called.
    ~AssetSet();

    /// Registers interest; a load of the same resource at the same revision
    /// already under way or done is shared. Refuses what the catalog refuses
    /// (not found, wrong type, wrong revision) and a full set.
    [[nodiscard]] result::Result<RequesterId> request(const content::ResourceRef& reference,
                                                      const RequestOptions& options = {});
    [[nodiscard]] result::Result<RequesterId> request(const content::PinnedResourceRef& reference,
                                                      const RequestOptions& options = {});

    [[nodiscard]] Readiness readiness(RequesterId requester) const noexcept;
    /// Why it failed, once `Failed`.
    [[nodiscard]] const result::Error* failure(RequesterId requester) const noexcept;
    /// The decoded form's handle, once `Ready`.
    [[nodiscard]] std::optional<AssetHandle> handle(RequesterId requester) const noexcept;

    /// No longer needed: the last interest going makes the form evictable.
    void release(RequesterId requester) noexcept;
    /// Cancelled now; a load no one else wants is cancelled with it.
    void cancel(RequesterId requester) noexcept;

    /// The decoded form, or why its handle is stale (`Evicted`,
    /// `RevisionRetired`, `ScopeClosed`).
    [[nodiscard]] result::Result<const void*> get(AssetHandle handle, std::uint64_t tick) const;

    /// At a schedule point on the owner's thread: takes finished reads to
    /// decoding and finished decodes to residency, fails requests past their
    /// deadlines, and evicts in order while over budget.
    void update(std::uint64_t tick);

    [[nodiscard]] Residency residency(AssetHandle handle) const noexcept;
    [[nodiscard]] AssetStatistics statistics() const noexcept;

    /// Cancels and joins every load and returns what was still wanted; every
    /// handle is then stale with `ScopeClosed`.
    std::vector<Survivor> close();

    struct State;

private:
    explicit AssetSet(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// A typed view of an AssetSet for one family's decoded type.
template <typename T> class Assets {
public:
    explicit Assets(AssetSet& set) noexcept : set_(&set) {
    }
    /// The decoded form, or why its handle is stale.
    [[nodiscard]] result::Result<const T*> get(AssetHandle handle, std::uint64_t tick) const {
        RAWFRAME_TRY_ASSIGN(const void* value, set_->get(handle, tick));
        return static_cast<const T*>(value);
    }
    [[nodiscard]] AssetSet& set() const noexcept {
        return *set_;
    }

private:
    AssetSet* set_;
};

} // namespace rawframe::assets
