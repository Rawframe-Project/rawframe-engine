// The router and emitter: what reaches a sink, when, and with which time and
// correlation.

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/diagnostics/router.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace rawframe::diagnostics;

namespace {

constexpr EventIdentity kIdentity{"test_domain", "event"};

/// What a sink kept of one record. Records are borrowed, so this copies.
struct Seen {
    Kind kind;
    Severity severity;
    std::string code;
    std::string message;
    Timestamp time;
    std::optional<Correlation> correlation;
    SpanPhase phase;
    std::uint64_t durationNanoseconds;
    MetricKind metricKind;
    std::size_t fieldCount;
};

class RecordingSink final : public Sink {
public:
    void accept(const Record& record) noexcept override {
        seen.push_back(Seen{record.kind,
                            record.severity,
                            std::string{record.identity.code},
                            std::string{record.message},
                            record.time,
                            record.correlation,
                            record.phase,
                            record.durationNanoseconds,
                            record.metricKind,
                            record.fields.size()});
    }

    std::vector<Seen> seen;
};

std::uint64_t clockNow = 0;

std::uint64_t testClock() noexcept {
    return clockNow;
}

std::int64_t testWall() noexcept {
    return 1785000000000000000;
}

} // namespace

RAWFRAME_TEST(ADefaultEmitterDoesNothing) {
    const Emitter kEmitter;
    RAWFRAME_EXPECT(!kEmitter.enabled(Severity::Critical));
    kEmitter.log(Severity::Critical, kIdentity, "nowhere");
    const SpanToken kSpan = kEmitter.beginSpan(kIdentity);
    kEmitter.endSpan(kSpan, SpanOutcome::Completed);
    kEmitter.counter(kIdentity, Unit::Count, 1);
}

RAWFRAME_TEST(RecordsBelowTheMinimumSeverityAreNotDelivered) {
    RecordingSink sink;
    const std::array<Sink*, 1> kSinks{&sink};
    Router router{RouterSettings{.minimumSeverity = Severity::Warning}, kSinks};
    const Emitter kEmitter = router.emitter();
    RAWFRAME_EXPECT(!kEmitter.enabled(Severity::Info));
    RAWFRAME_EXPECT(kEmitter.enabled(Severity::Warning));
    kEmitter.log(Severity::Info, kIdentity, "quiet");
    kEmitter.log(Severity::Error, kIdentity, "loud", {field("attempt", 2)});
    // Metrics are delivered at Info, so they are filtered here too.
    kEmitter.counter(kIdentity, Unit::Count, 1);
    RAWFRAME_EXPECT(sink.seen.size() == 1);
    RAWFRAME_EXPECT(sink.seen[0].message == "loud");
    RAWFRAME_EXPECT(sink.seen[0].severity == Severity::Error);
    RAWFRAME_EXPECT(sink.seen[0].fieldCount == 1);
}

RAWFRAME_TEST(TheRouterStampsTimeFromItsClocks) {
    RecordingSink sink;
    const std::array<Sink*, 1> kSinks{&sink};
    clockNow = 500;
    Router router{RouterSettings{.minimumSeverity = Severity::Info, .monotonic = &testClock, .wall = &testWall},
                  kSinks};
    router.emitter().log(Severity::Info, kIdentity, "stamped");
    RAWFRAME_EXPECT(sink.seen.size() == 1);
    RAWFRAME_EXPECT(sink.seen[0].time.monotonicNanoseconds == 500);
    RAWFRAME_EXPECT(sink.seen[0].time.wallUnixNanoseconds == 1785000000000000000);

    RecordingSink noWall;
    const std::array<Sink*, 1> kNoWallSinks{&noWall};
    Router plain{RouterSettings{.minimumSeverity = Severity::Info, .monotonic = &testClock}, kNoWallSinks};
    plain.emitter().log(Severity::Info, kIdentity, "no wall");
    RAWFRAME_EXPECT(!noWall.seen[0].time.wallUnixNanoseconds.has_value());
}

RAWFRAME_TEST(SpansNestThroughCorrelation) {
    RecordingSink sink;
    const std::array<Sink*, 1> kSinks{&sink};
    clockNow = 100;
    Router router{RouterSettings{.minimumSeverity = Severity::Debug, .monotonic = &testClock, .traceSeed = 9}, kSinks};
    const Emitter kEmitter = router.emitter();

    const SpanToken kOuter = kEmitter.beginSpan(kIdentity);
    RAWFRAME_EXPECT(!kOuter.correlation.parent.has_value());
    const Emitter kInside = kEmitter.within(kOuter);
    clockNow = 150;
    const SpanToken kInner = kInside.beginSpan(EventIdentity{"test_domain", "inner"});
    RAWFRAME_EXPECT(kInner.correlation.trace == kOuter.correlation.trace);
    RAWFRAME_EXPECT(kInner.correlation.parent == kOuter.correlation.span);
    RAWFRAME_EXPECT(kInner.correlation.span != kOuter.correlation.span);
    kInside.log(Severity::Info, kIdentity, "inside");
    clockNow = 400;
    kInside.endSpan(kInner, SpanOutcome::Failed);
    kEmitter.endSpan(kOuter, SpanOutcome::Completed);

    RAWFRAME_EXPECT(sink.seen.size() == 5);
    if (sink.seen.size() != 5) {
        return;
    }
    RAWFRAME_EXPECT(sink.seen[0].kind == Kind::Span && sink.seen[0].phase == SpanPhase::Begin);
    RAWFRAME_EXPECT(sink.seen[2].correlation->span == kOuter.correlation.span);
    RAWFRAME_EXPECT(sink.seen[3].phase == SpanPhase::End && sink.seen[3].durationNanoseconds == 250);
    RAWFRAME_EXPECT(sink.seen[4].code == "event" && sink.seen[4].durationNanoseconds == 300);
}

RAWFRAME_TEST(SpansAreDeliveredAtDebug) {
    RecordingSink sink;
    const std::array<Sink*, 1> kSinks{&sink};
    Router router{RouterSettings{.minimumSeverity = Severity::Info}, kSinks};
    const Emitter kEmitter = router.emitter();
    const SpanToken kSpan = kEmitter.beginSpan(kIdentity);
    // The span still has identities, so records inside it correlate even when
    // the span records themselves are filtered.
    RAWFRAME_EXPECT(kSpan.correlation.span != 0);
    kEmitter.endSpan(kSpan, SpanOutcome::Completed);
    kEmitter.gauge(kIdentity, Unit::Bytes, 12);
    RAWFRAME_EXPECT(sink.seen.size() == 1);
    RAWFRAME_EXPECT(sink.seen[0].kind == Kind::Metric && sink.seen[0].metricKind == MetricKind::Gauge);
}

RAWFRAME_TEST(TwoRoutersMintDifferentTraces) {
    Router first{RouterSettings{.traceSeed = 1}, {}};
    Router second{RouterSettings{.traceSeed = 2}, {}};
    RAWFRAME_EXPECT(first.newTrace() != second.newTrace());
    RAWFRAME_EXPECT(first.newTrace() != first.newTrace());
    Router unseeded{RouterSettings{}, {}};
    const rawframe::base::Bits128 kTrace = unseeded.newTrace();
    RAWFRAME_EXPECT(kTrace.high != 0 && kTrace.low != 0);
}

RAWFRAME_TEST(AfterStopRecordsAreCountedAndDropped) {
    RecordingSink sink;
    const std::array<Sink*, 1> kSinks{&sink};
    Router router{RouterSettings{}, kSinks};
    const Emitter kEmitter = router.emitter();
    kEmitter.log(Severity::Info, kIdentity, "before");
    router.stop();
    kEmitter.log(Severity::Critical, kIdentity, "after");
    Record late{.kind = Kind::Log, .time = {}, .identity = kIdentity, .correlation = std::nullopt, .fields = {}};
    router.deliver(late);
    RAWFRAME_EXPECT(sink.seen.size() == 1);
    RAWFRAME_EXPECT(router.droppedAfterStop() == 2);
}
