#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/diagnostics/identity.h"

#include <concepts>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>

namespace rawframe::diagnostics {

/// The three signal kinds SPEC-0004 keeps apart.
enum class Kind : std::uint8_t {
    Log,
    Span,
    Metric
};

enum class Severity : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

/// Who may see a value, ordered from least to most restricted. A `Secret` field
/// is removed before any sink writes, so no written record is ever `Secret`.
enum class Sensitivity : std::uint8_t {
    Public,
    Internal,
    Personal,
    Secret
};

enum class SpanPhase : std::uint8_t {
    Begin,
    End
};

/// SPEC-0005's task outcome vocabulary: cancellation is not failure.
enum class SpanOutcome : std::uint8_t {
    Completed,
    Cancelled,
    Failed
};

enum class MetricKind : std::uint8_t {
    Counter,
    Gauge,
    Distribution
};

enum class Unit : std::uint8_t {
    Count,
    Bytes,
    Nanoseconds,
    Ratio
};

/// A field value: string, integer, real, boolean, or null. Strings are borrowed
/// for the duration of the call that carries them.
struct FieldValue {
    enum class Type : std::uint8_t {
        String,
        Integer,
        Real,
        Boolean,
        Null
    };
    Type type = Type::Null;
    std::string_view text;
    std::int64_t integer = 0;
    double real = 0.0;
    bool boolean = false;
};

struct Field {
    FieldKey key;
    FieldValue value;
    /// Internal unless the producer knows better: the conservative default.
    Sensitivity sensitivity = Sensitivity::Internal;
};

[[nodiscard]] constexpr Field
field(FieldKey key, std::string_view text, Sensitivity sensitivity = Sensitivity::Internal) noexcept {
    return Field{key, FieldValue{.type = FieldValue::Type::String, .text = text}, sensitivity};
}

/// A string literal would otherwise pick the bool overload through the pointer
/// conversion, which beats the conversion to string_view.
[[nodiscard]] constexpr Field
field(FieldKey key, const char* text, Sensitivity sensitivity = Sensitivity::Internal) noexcept {
    return field(key, std::string_view{text}, sensitivity);
}

template <std::integral T>
    requires(!std::same_as<T, bool>)
[[nodiscard]] constexpr Field field(FieldKey key, T number, Sensitivity sensitivity = Sensitivity::Internal) noexcept {
    return Field{
        key, FieldValue{.type = FieldValue::Type::Integer, .integer = static_cast<std::int64_t>(number)}, sensitivity};
}

template <std::floating_point T>
[[nodiscard]] constexpr Field field(FieldKey key, T number, Sensitivity sensitivity = Sensitivity::Internal) noexcept {
    return Field{key, FieldValue{.type = FieldValue::Type::Real, .real = static_cast<double>(number)}, sensitivity};
}

[[nodiscard]] constexpr Field field(FieldKey key, bool flag, Sensitivity sensitivity = Sensitivity::Internal) noexcept {
    return Field{key, FieldValue{.type = FieldValue::Type::Boolean, .boolean = flag}, sensitivity};
}

/// Monotonic time owns ordering and duration; wall time is an optional
/// projection that must never order or time anything (ADR-0010).
struct Timestamp {
    std::uint64_t monotonicNanoseconds = 0;
    std::optional<std::int64_t> wallUnixNanoseconds;
};

struct Correlation {
    base::Bits128 trace;
    std::uint64_t span = 0;
    std::optional<std::uint64_t> parent;
};

struct Statistics {
    std::uint64_t count = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

/// One observation on its way to the sinks. Everything it points at is
/// borrowed for the duration of Sink::accept, which must copy what it keeps.
struct Record {
    Kind kind;
    Timestamp time;
    EventIdentity identity;
    std::optional<Correlation> correlation;
    std::span<const Field> fields;

    // Log.
    Severity severity = Severity::Info;
    std::optional<std::source_location> source;
    std::string_view message;

    // Span.
    SpanPhase phase = SpanPhase::Begin;
    std::uint64_t durationNanoseconds = 0;
    SpanOutcome outcome = SpanOutcome::Completed;

    // Metric.
    MetricKind metricKind = MetricKind::Counter;
    Unit unit = Unit::Count;
    double value = 0.0;
    Statistics statistics;
};

} // namespace rawframe::diagnostics
