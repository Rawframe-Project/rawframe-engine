#pragma once

#include "rawframe/base/threads.h"
#include "rawframe/diagnostics/sink.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace rawframe::diagnostics {

/// Identities one limiter tracks. Past this, new identities pass unlimited:
/// a full table never hides a record it has not seen before.
inline constexpr std::size_t kMaximumRateLimitedIdentities = 256;

struct RateLimit {
    /// Log records of one identity passed per window. Spans and metrics are
    /// never limited: a span pair or a counter with gaps would mislead.
    std::uint32_t recordsPerWindow = 20;
    std::uint64_t windowNanoseconds = 1'000'000'000;
};

/// Passes log records to another sink at most `recordsPerWindow` times per
/// identity per window, on the record's monotonic time. What it holds back is
/// counted, and the first record passed in a later window carries the count as
/// a `suppressed` field (SPEC-0004: rate limiting preserves the count).
class RateLimitedSink final : public Sink {
public:
    RateLimitedSink(Sink& next, RateLimit limit) noexcept : next_(&next), limit_(limit) {
    }

    void accept(const Record& record) noexcept override;

    /// Records held back so far, over all identities.
    [[nodiscard]] std::uint64_t suppressedRecords() const noexcept;

private:
    struct Entry {
        std::string_view domain;
        std::string_view code;
        std::uint64_t windowStart = 0;
        std::uint32_t passed = 0;
        std::uint64_t suppressed = 0; // held back and not yet reported
    };

    /// The entry for this identity, created if there is room, or null.
    [[nodiscard]] Entry* find(const EventIdentity& identity) noexcept;

    Sink* next_;
    RateLimit limit_;
    mutable base::Mutex mutex_;
    std::array<Entry, kMaximumRateLimitedIdentities> entries_{};
    std::size_t entryCount_ = 0;
    std::uint64_t suppressedTotal_ = 0;
};

} // namespace rawframe::diagnostics
