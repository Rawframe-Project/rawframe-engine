#pragma once

// The content store (SPEC-0008): a Runtime's one catalog generation, its
// sources, and reads that return bytes only once they are exactly what the
// catalog says. Reads run on the blocking-I/O executor; nothing here reads
// synchronously, logs, or decodes.

#include "rawframe/content/catalog.h"
#include "rawframe/content/source.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/operation.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawframe::content {

/// Bytes that were verified: exactly the declared length, with the declared
/// digest, from the generation that resolved them. Immutable.
class VerifiedContent {
public:
    VerifiedContent(std::shared_ptr<const std::vector<std::byte>> bytes,
                    ContentDescriptor descriptor,
                    std::uint64_t generation,
                    const CatalogFingerprint& fingerprint) noexcept
        : bytes_(std::move(bytes)), descriptor_(std::move(descriptor)), generation_(generation),
          fingerprint_(fingerprint) {
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return *bytes_;
    }
    /// The bytes to keep past this object, still immutable.
    [[nodiscard]] const std::shared_ptr<const std::vector<std::byte>>& shared() const noexcept {
        return bytes_;
    }
    [[nodiscard]] const ContentDescriptor& descriptor() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] const CatalogFingerprint& fingerprint() const noexcept {
        return fingerprint_;
    }

private:
    std::shared_ptr<const std::vector<std::byte>> bytes_;
    ContentDescriptor descriptor_;
    std::uint64_t generation_;
    CatalogFingerprint fingerprint_;
};

struct ReadRequest {
    /// The most bytes this read will take (`ResourceTooLarge`, before any
    /// allocation or read).
    std::uint64_t maximumBytes = std::uint64_t{256} * 1024 * 1024;
    /// Past this instant the read delivers `DeadlineExceeded`, even with its
    /// bytes in hand.
    std::optional<execution::MonotonicInstant> deadline;
};

class ContentStore {
public:
    /// A store over `sources`, reading on `blockingIo` as `owner`, which must
    /// be admitted there, under `parent`'s cancellation. `clock` judges
    /// deadlines. It has no catalog until one is published.
    [[nodiscard]] static result::Result<std::unique_ptr<ContentStore>> create(execution::Executor& blockingIo,
                                                                              execution::OwnerId owner,
                                                                              execution::CancellationScope& parent,
                                                                              const execution::MonotonicSource& clock,
                                                                              std::vector<ContentSource> sources);

    ContentStore(const ContentStore&) = delete;
    ContentStore& operator=(const ContentStore&) = delete;
    /// Cancels and joins every read.
    ~ContentStore();

    /// The sources a catalog for this store binds manifests to.
    [[nodiscard]] std::size_t sourceCount() const noexcept;

    /// Makes `catalog` the one reads resolve in from now on; reads already
    /// begun finish in the generation they began in. Call at a safe point.
    void publish(std::shared_ptr<const ContentCatalog> catalog) noexcept;
    [[nodiscard]] std::shared_ptr<const ContentCatalog> catalog() const noexcept;

    /// Resolves in the current generation, checks the length against the
    /// request, and starts the read. Refuses what resolving refuses, with no
    /// catalog (`SourceUnavailable`), too large (`ResourceTooLarge`), and a
    /// full executor (`QuotaExhausted`). The operation delivers the verified
    /// bytes, a cancellation, or `ReadFailed`, `ShortRead`, `SourceChanged`,
    /// `PathEscape`, `DigestMismatch`, or `DeadlineExceeded`.
    [[nodiscard]] result::Result<execution::AsyncHandle<VerifiedContent>> read(const ResourceRef& reference,
                                                                               const ReadRequest& request = {});
    [[nodiscard]] result::Result<execution::AsyncHandle<VerifiedContent>> read(const PinnedResourceRef& reference,
                                                                               const ReadRequest& request = {});

    struct State;

private:
    explicit ContentStore(std::unique_ptr<State> state) noexcept;
    [[nodiscard]] result::Result<execution::AsyncHandle<VerifiedContent>> start(
        std::shared_ptr<const ContentCatalog> catalog, const ContentDescriptor& descriptor, const ReadRequest& request);

    std::unique_ptr<State> state_;
};

} // namespace rawframe::content
