#include "rawframe/diagnostics/emitter.h"

namespace rawframe::diagnostics {

void Emitter::log(Severity severity,
                  EventIdentity identity,
                  std::string_view message,
                  std::span<const Field> fields,
                  std::source_location source) const noexcept {
    if (!enabled(severity)) {
        return;
    }
    Record record{.kind = Kind::Log,
                  .time = {},
                  .identity = identity,
                  .correlation = context_,
                  .fields = fields,
                  .severity = severity,
                  .source = source,
                  .message = message};
    router_->deliver(record);
}

SpanToken Emitter::beginSpan(EventIdentity identity, std::span<const Field> fields) const noexcept {
    if (router_ == nullptr) {
        return SpanToken{.correlation = {}, .identity = identity, .startNanoseconds = 0};
    }
    Correlation correlation{.trace = context_ ? context_->trace : router_->newTrace(),
                            .span = router_->newSpan(),
                            .parent = context_ ? std::optional<std::uint64_t>{context_->span} : std::nullopt};
    SpanToken token{.correlation = correlation, .identity = identity, .startNanoseconds = router_->now()};
    // Spans are delivered at Debug: a begin/end pair per operation is detail an
    // operator turns on, not something every run writes.
    if (enabled(Severity::Debug)) {
        Record record{.kind = Kind::Span,
                      .time = {},
                      .identity = identity,
                      .correlation = correlation,
                      .fields = fields,
                      .severity = Severity::Debug,
                      .phase = SpanPhase::Begin};
        router_->deliver(record);
    }
    return token;
}

void Emitter::endSpan(const SpanToken& span, SpanOutcome outcome, std::span<const Field> fields) const noexcept {
    if (!enabled(Severity::Debug)) {
        return;
    }
    const std::uint64_t kNow = router_->now();
    Record record{.kind = Kind::Span,
                  .time = {},
                  .identity = span.identity,
                  .correlation = span.correlation,
                  .fields = fields,
                  .severity = Severity::Debug,
                  .phase = SpanPhase::End,
                  .durationNanoseconds = kNow >= span.startNanoseconds ? kNow - span.startNanoseconds : 0,
                  .outcome = outcome};
    router_->deliver(record);
}

void Emitter::metric(EventIdentity identity,
                     MetricKind kind,
                     Unit unit,
                     double value,
                     const Statistics& statistics,
                     std::span<const Field> fields) const noexcept {
    if (!enabled(Severity::Info)) {
        return;
    }
    Record record{.kind = Kind::Metric,
                  .time = {},
                  .identity = identity,
                  .correlation = context_,
                  .fields = fields,
                  .metricKind = kind,
                  .unit = unit,
                  .value = value,
                  .statistics = statistics};
    router_->deliver(record);
}

void Emitter::counter(EventIdentity identity, Unit unit, double value, std::span<const Field> fields) const noexcept {
    metric(identity, MetricKind::Counter, unit, value, Statistics{}, fields);
}

void Emitter::gauge(EventIdentity identity, Unit unit, double value, std::span<const Field> fields) const noexcept {
    metric(identity, MetricKind::Gauge, unit, value, Statistics{}, fields);
}

void Emitter::distribution(EventIdentity identity,
                           Unit unit,
                           const Statistics& statistics,
                           std::span<const Field> fields) const noexcept {
    metric(identity, MetricKind::Distribution, unit, 0.0, statistics, fields);
}

} // namespace rawframe::diagnostics
