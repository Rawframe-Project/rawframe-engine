#pragma once

#include "rawframe/diagnostics/record.h"
#include "rawframe/diagnostics/router.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>

namespace rawframe::diagnostics {

/// A span in progress. Returned by beginSpan and handed back to endSpan.
struct SpanToken {
    Correlation correlation;
    EventIdentity identity;
    std::uint64_t startNanoseconds = 0;
};

/// What an owner holds to report observations. Passed in explicitly at
/// construction, copyable, and cheap. A default-constructed Emitter is disabled:
/// every call is a no-op, which is what a test or an owner without diagnostics
/// uses. There is no global emitter to fall back to.
class Emitter {
public:
    Emitter() noexcept = default;
    explicit Emitter(Router& router, std::optional<Correlation> context = std::nullopt) noexcept
        : router_(&router), context_(context) {
    }

    /// Whether a record of this severity would be delivered. Check it before
    /// building expensive fields.
    [[nodiscard]] bool enabled(Severity severity) const noexcept {
        return router_ != nullptr && router_->enabled(severity);
    }

    /// An emitter whose records carry this span as their correlation.
    [[nodiscard]] Emitter within(const SpanToken& span) const noexcept {
        return Emitter{router_, span.correlation};
    }

    void log(Severity severity,
             EventIdentity identity,
             std::string_view message,
             std::span<const Field> fields = {},
             std::source_location source = std::source_location::current()) const noexcept;

    void log(Severity severity,
             EventIdentity identity,
             std::string_view message,
             std::initializer_list<Field> fields,
             std::source_location source = std::source_location::current()) const noexcept {
        log(severity, identity, message, std::span<const Field>{fields.begin(), fields.size()}, source);
    }

    [[nodiscard]] SpanToken beginSpan(EventIdentity identity, std::span<const Field> fields = {}) const noexcept;
    void endSpan(const SpanToken& span, SpanOutcome outcome, std::span<const Field> fields = {}) const noexcept;

    void counter(EventIdentity identity, Unit unit, double value, std::span<const Field> fields = {}) const noexcept;
    void gauge(EventIdentity identity, Unit unit, double value, std::span<const Field> fields = {}) const noexcept;
    void distribution(EventIdentity identity,
                      Unit unit,
                      const Statistics& statistics,
                      std::span<const Field> fields = {}) const noexcept;

private:
    Emitter(Router* router, std::optional<Correlation> context) noexcept : router_(router), context_(context) {
    }

    void metric(EventIdentity identity,
                MetricKind kind,
                Unit unit,
                double value,
                const Statistics& statistics,
                std::span<const Field> fields) const noexcept;

    Router* router_ = nullptr;
    std::optional<Correlation> context_;
};

} // namespace rawframe::diagnostics
