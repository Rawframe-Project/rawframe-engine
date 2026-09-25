#include "rawframe/diagnostics/rate_limit.h"

#include "rawframe/base/threads.h"
#include "rawframe/diagnostics/ndjson_sink.h"

#include <algorithm>
#include <utility>

namespace rawframe::diagnostics {

namespace {

constexpr Field kSuppressedPlaceholder = field("suppressed", 0, Sensitivity::Public);

/// An array of fields, every one a copy of `value`: Field has no default, since
/// its key exists only as a compile-time constant.
template <std::size_t... Index>
std::array<Field, sizeof...(Index)> filledWith(const Field& value, std::index_sequence<Index...>) noexcept {
    return {((void)Index, value)...};
}

} // namespace

RateLimitedSink::Entry* RateLimitedSink::find(const EventIdentity& identity) noexcept {
    for (std::size_t index = 0; index < entryCount_; ++index) {
        Entry& entry = entries_[index];
        if (entry.domain == identity.domain && entry.code == identity.code) {
            return &entry;
        }
    }
    if (entryCount_ == entries_.size()) {
        return nullptr;
    }
    Entry& entry = entries_[entryCount_++];
    entry = Entry{.domain = identity.domain, .code = identity.code};
    return &entry;
}

void RateLimitedSink::accept(const Record& record) noexcept {
    if (record.kind != Kind::Log) {
        next_->accept(record);
        return;
    }
    std::uint64_t report = 0;
    {
        const std::lock_guard kLock{mutex_};
        Entry* entry = find(record.identity);
        if (entry != nullptr) {
            const std::uint64_t kNow = record.time.monotonicNanoseconds;
            if (entry->passed == 0 || kNow - entry->windowStart >= limit_.windowNanoseconds) {
                entry->windowStart = kNow;
                entry->passed = 0;
            }
            if (entry->passed == limit_.recordsPerWindow) {
                ++entry->suppressed;
                ++suppressedTotal_;
                return;
            }
            ++entry->passed;
            report = entry->suppressed;
            entry->suppressed = 0;
        }
    }
    if (report == 0) {
        next_->accept(record);
        return;
    }
    // The same record with one more field, the count it stands in for. Built on
    // the stack; the sink below copies what it keeps.
    std::array<Field, kMaximumFieldCount> fields =
        filledWith(kSuppressedPlaceholder, std::make_index_sequence<kMaximumFieldCount>{});
    const std::size_t kKept = std::min(record.fields.size(), fields.size() - 1);
    std::copy_n(record.fields.begin(), kKept, fields.begin());
    fields[kKept] = field("suppressed", report, Sensitivity::Public);
    Record annotated = record;
    annotated.fields = std::span<const Field>{fields.data(), kKept + 1};
    next_->accept(annotated);
}

std::uint64_t RateLimitedSink::suppressedRecords() const noexcept {
    const std::lock_guard kLock{mutex_};
    return suppressedTotal_;
}

} // namespace rawframe::diagnostics
