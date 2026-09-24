// Rate limiting: what passes per window, what is counted, and where the count
// is reported.

#include "rawframe/diagnostics/rate_limit.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace rawframe::diagnostics;

namespace {

struct Seen {
    std::string code;
    std::optional<std::int64_t> suppressed;
};

struct Collecting final : Sink {
    std::vector<Seen> seen;
    void accept(const Record& record) noexcept override {
        Seen entry{std::string{record.identity.code}, std::nullopt};
        for (const Field& item : record.fields) {
            if (item.key.text == "suppressed") {
                entry.suppressed = item.value.integer;
            }
        }
        seen.push_back(entry);
    }
};

constexpr EventIdentity kNoisy{"test_domain", "noisy"};
constexpr EventIdentity kQuiet{"test_domain", "quiet"};

Record logAt(EventIdentity identity, std::uint64_t nanoseconds) {
    return Record{.kind = Kind::Log,
                  .time = {.monotonicNanoseconds = nanoseconds, .wallUnixNanoseconds = std::nullopt},
                  .identity = identity,
                  .correlation = std::nullopt,
                  .fields = {}};
}

} // namespace

RAWFRAME_TEST(EachIdentityPassesItsQuotaPerWindow) {
    Collecting collecting;
    RateLimitedSink limited{collecting, RateLimit{.recordsPerWindow = 3, .windowNanoseconds = 1000}};
    for (std::uint64_t index = 0; index < 10; ++index) {
        limited.accept(logAt(kNoisy, index));
    }
    limited.accept(logAt(kQuiet, 11));
    RAWFRAME_EXPECT(collecting.seen.size() == 4);
    RAWFRAME_EXPECT(limited.suppressedRecords() == 7);

    // A new window passes again, and its first record carries the count.
    limited.accept(logAt(kNoisy, 1500));
    limited.accept(logAt(kNoisy, 1501));
    RAWFRAME_EXPECT(collecting.seen.size() == 6);
    RAWFRAME_EXPECT(collecting.seen[4].suppressed == 7);
    RAWFRAME_EXPECT(!collecting.seen[5].suppressed.has_value());
}

RAWFRAME_TEST(SpansAndMetricsAreNeverLimited) {
    Collecting collecting;
    RateLimitedSink limited{collecting, RateLimit{.recordsPerWindow = 1, .windowNanoseconds = 1000}};
    for (std::uint64_t index = 0; index < 5; ++index) {
        Record span = logAt(kNoisy, index);
        span.kind = Kind::Span;
        limited.accept(span);
        Record metric = logAt(kNoisy, index);
        metric.kind = Kind::Metric;
        limited.accept(metric);
    }
    RAWFRAME_EXPECT(collecting.seen.size() == 10);
    RAWFRAME_EXPECT(limited.suppressedRecords() == 0);
}

RAWFRAME_TEST(TheCountRidesAlongsideExistingFields) {
    Collecting collecting;
    RateLimitedSink limited{collecting, RateLimit{.recordsPerWindow = 1, .windowNanoseconds = 10}};
    const Field kFields[] = {field("attempt", 3)};
    Record record = logAt(kNoisy, 0);
    record.fields = kFields;
    limited.accept(record);
    limited.accept(record);
    record.time.monotonicNanoseconds = 20;
    limited.accept(record);
    RAWFRAME_EXPECT(collecting.seen.size() == 2);
    RAWFRAME_EXPECT(collecting.seen[1].suppressed == 1);
}
