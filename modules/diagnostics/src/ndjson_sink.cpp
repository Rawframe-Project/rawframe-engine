#include "rawframe/diagnostics/ndjson_sink.h"

#include "rawframe/base/assert.h"
#include "rawframe/base/threads.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <utility>

namespace rawframe::diagnostics {

namespace {

constexpr int kSchema = 1;

constexpr EventIdentity kRecordsDropped{"diagnostics", "records_dropped"};
constexpr EventIdentity kRecordsRefused{"diagnostics", "records_refused"};

std::string_view name(Kind kind) noexcept {
    switch (kind) {
    case Kind::Log:
        return "log";
    case Kind::Span:
        return "span";
    case Kind::Metric:
        return "metric";
    }
    return "log";
}

std::string_view name(Severity severity) noexcept {
    switch (severity) {
    case Severity::Trace:
        return "trace";
    case Severity::Debug:
        return "debug";
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    case Severity::Critical:
        return "critical";
    }
    return "critical";
}

std::string_view name(Sensitivity sensitivity) noexcept {
    switch (sensitivity) {
    case Sensitivity::Public:
        return "public";
    case Sensitivity::Internal:
        return "internal";
    case Sensitivity::Personal:
        return "personal";
    case Sensitivity::Secret:
        return "secret";
    }
    return "secret";
}

std::string_view name(SpanOutcome outcome) noexcept {
    switch (outcome) {
    case SpanOutcome::Completed:
        return "completed";
    case SpanOutcome::Cancelled:
        return "cancelled";
    case SpanOutcome::Failed:
        return "failed";
    }
    return "failed";
}

std::string_view name(MetricKind kind) noexcept {
    switch (kind) {
    case MetricKind::Counter:
        return "counter";
    case MetricKind::Gauge:
        return "gauge";
    case MetricKind::Distribution:
        return "distribution";
    }
    return "counter";
}

std::string_view name(Unit unit) noexcept {
    switch (unit) {
    case Unit::Count:
        return "count";
    case Unit::Bytes:
        return "bytes";
    case Unit::Nanoseconds:
        return "nanoseconds";
    case Unit::Ratio:
        return "ratio";
    }
    return "count";
}

/// The length of the valid UTF-8 sequence starting at `text[index]`, or zero if
/// the byte there does not start one.
std::size_t validSequenceLength(std::string_view text, std::size_t index) noexcept {
    const auto kByte = [&](std::size_t offset) noexcept {
        return static_cast<unsigned char>(text[index + offset]);
    };
    const auto kContinuation = [&](std::size_t offset) noexcept {
        return index + offset < text.size() && (kByte(offset) & 0xC0U) == 0x80U;
    };
    const unsigned char kLead = kByte(0);
    if (kLead < 0x80U) {
        return 1;
    }
    if (kLead >= 0xC2U && kLead <= 0xDFU) {
        return kContinuation(1) ? 2 : 0;
    }
    if (kLead >= 0xE0U && kLead <= 0xEFU) {
        if (!kContinuation(1) || !kContinuation(2)) {
            return 0;
        }
        // Overlong forms below U+0800 and the UTF-16 surrogates are not characters.
        const unsigned char kSecond = kByte(1);
        if ((kLead == 0xE0U && kSecond < 0xA0U) || (kLead == 0xEDU && kSecond > 0x9FU)) {
            return 0;
        }
        return 3;
    }
    if (kLead >= 0xF0U && kLead <= 0xF4U) {
        if (!kContinuation(1) || !kContinuation(2) || !kContinuation(3)) {
            return 0;
        }
        const unsigned char kSecond = kByte(1);
        if ((kLead == 0xF0U && kSecond < 0x90U) || (kLead == 0xF4U && kSecond > 0x8FU)) {
            return 0;
        }
        return 4;
    }
    return 0;
}

/// Appends JSON to a fixed buffer. Running out of room sets a flag instead of
/// writing past the end, so a caller finishes the record and then checks once.
class Writer {
public:
    explicit Writer(std::span<char> destination) noexcept : destination_(destination) {
    }

    void raw(std::string_view text) noexcept {
        if (text.size() > destination_.size() - size_) {
            overflowed_ = true;
            return;
        }
        std::copy(text.begin(), text.end(), destination_.begin() + static_cast<std::ptrdiff_t>(size_));
        size_ += text.size();
    }

    void raw(char character) noexcept {
        raw(std::string_view{&character, 1});
    }

    /// Writes `"key":`, with a separating comma unless this opens an object.
    void key(std::string_view text) noexcept {
        if (needsComma_) {
            raw(',');
        }
        raw('"');
        raw(text);
        raw("\":");
        needsComma_ = true;
    }

    void open() noexcept {
        raw('{');
        needsComma_ = false;
    }

    void close() noexcept {
        raw('}');
        needsComma_ = true;
    }

    /// Writes `text` as a JSON string holding at most `limit` bytes of UTF-8,
    /// cut on a character boundary. Invalid bytes become U+FFFD and control
    /// characters their escapes. Returns true if the value was cut.
    bool string(std::string_view text, std::size_t limit) noexcept {
        raw('"');
        std::size_t kept = 0;
        bool cut = false;
        for (std::size_t index = 0; index < text.size();) {
            const std::size_t kLength = validSequenceLength(text, index);
            const std::size_t kOutput = kLength == 0 ? 3 : kLength;
            if (kept + kOutput > limit) {
                cut = true;
                break;
            }
            kept += kOutput;
            if (kLength == 0) {
                raw("\xEF\xBF\xBD");
                ++index;
                continue;
            }
            if (kLength == 1) {
                character(text[index]);
            } else {
                raw(text.substr(index, kLength));
            }
            index += kLength;
        }
        raw('"');
        return cut;
    }

    void integer(std::uint64_t value) noexcept {
        number(value);
    }
    void integer(std::int64_t value) noexcept {
        number(value);
    }

    /// JSON has no spelling for infinity or NaN, so a non-finite value is null.
    void real(double value) noexcept {
        if (!std::isfinite(value)) {
            raw("null");
            return;
        }
        number(value);
    }

    void hex(std::uint64_t value) noexcept {
        constexpr std::string_view kDigits = "0123456789abcdef";
        std::array<char, 18> text{};
        text[0] = '"';
        for (std::size_t index = 0; index < 16; ++index) {
            text[16 - index] = kDigits[(value >> (index * 4U)) & 0xFU];
        }
        text[17] = '"';
        raw(std::string_view{text.data(), text.size()});
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] bool overflowed() const noexcept {
        return overflowed_;
    }

private:
    void character(char value) noexcept {
        switch (value) {
        case '"':
            raw("\\\"");
            return;
        case '\\':
            raw("\\\\");
            return;
        case '\n':
            raw("\\n");
            return;
        case '\r':
            raw("\\r");
            return;
        case '\t':
            raw("\\t");
            return;
        case '\b':
            raw("\\b");
            return;
        case '\f':
            raw("\\f");
            return;
        default:
            break;
        }
        const auto kByte = static_cast<unsigned char>(value);
        if (kByte < 0x20U) {
            constexpr std::string_view kDigits = "0123456789abcdef";
            const std::array<char, 6> kEscape = {'\\', 'u', '0', '0', kDigits[kByte >> 4U], kDigits[kByte & 0xFU]};
            raw(std::string_view{kEscape.data(), kEscape.size()});
            return;
        }
        raw(value);
    }

    template <typename T> void number(T value) noexcept {
        std::array<char, 32> text{};
        const auto kResult = std::to_chars(text.data(), text.data() + text.size(), value);
        raw(std::string_view{text.data(), static_cast<std::size_t>(kResult.ptr - text.data())});
    }

    std::span<char> destination_;
    std::size_t size_ = 0;
    bool overflowed_ = false;
    bool needsComma_ = false;
};

/// The key paths the record lost, bounded by kMaximumTruncatedEntries. The
/// strings are either literals or field keys, both of which outlive the record.
struct TruncatedPaths {
    std::array<std::string_view, kMaximumTruncatedEntries> prefix{};
    std::array<std::string_view, kMaximumTruncatedEntries> path{};
    std::size_t count = 0;

    void add(std::string_view prefixText, std::string_view pathText) noexcept {
        if (count < kMaximumTruncatedEntries) {
            prefix[count] = prefixText;
            path[count] = pathText;
            ++count;
        }
    }
};

/// The fields that survive redaction and the count ceiling, in producer order.
struct SurvivingFields {
    std::array<const Field*, kMaximumFieldCount> fields{};
    std::size_t count = 0;
    bool dropped = false;
    Sensitivity highest = Sensitivity::Public;
};

SurvivingFields selectFields(std::span<const Field> fields) noexcept {
    SurvivingFields surviving;
    for (const Field& candidate : fields) {
        // Removed, not masked: the key does not appear either (SPEC-0047).
        if (candidate.sensitivity == Sensitivity::Secret) {
            continue;
        }
        if (surviving.count == kMaximumFieldCount) {
            surviving.dropped = true;
            continue;
        }
        surviving.fields[surviving.count++] = &candidate;
        surviving.highest = std::max(surviving.highest, candidate.sensitivity);
    }
    return surviving;
}

/// Writes the envelope up to and excluding `truncated`, leaving the object open.
void writeEnvelope(Writer& out, const Record& record, Sensitivity sensitivity) noexcept {
    out.open();
    out.key("schema");
    out.integer(static_cast<std::int64_t>(kSchema));
    out.key("kind");
    out.raw('"');
    out.raw(name(record.kind));
    out.raw('"');
    out.key("time");
    out.open();
    out.key("monotonicNanoseconds");
    out.integer(record.time.monotonicNanoseconds);
    if (record.time.wallUnixNanoseconds) {
        out.key("wallUnixNanoseconds");
        out.integer(*record.time.wallUnixNanoseconds);
    }
    out.close();
    // Identities are compile-time checked lower_snake_case, so they need no
    // escaping.
    out.key("domain");
    out.raw('"');
    out.raw(record.identity.domain);
    out.raw('"');
    out.key("code");
    out.raw('"');
    out.raw(record.identity.code);
    out.raw('"');
    out.key("sensitivity");
    out.raw('"');
    out.raw(name(sensitivity));
    out.raw('"');
    if (record.correlation) {
        out.key("correlation");
        out.open();
        out.key("trace");
        std::array<char, base::kBits128HexDigits + 2> trace{};
        trace.front() = '"';
        trace.back() = '"';
        base::formatBits128Hex(record.correlation->trace,
                               std::span<char, base::kBits128HexDigits>{trace.data() + 1, base::kBits128HexDigits});
        out.raw(std::string_view{trace.data(), trace.size()});
        out.key("span");
        out.hex(record.correlation->span);
        if (record.correlation->parent) {
            out.key("parent");
            out.hex(*record.correlation->parent);
        }
        out.close();
    }
}

void writeTruncated(Writer& out, const TruncatedPaths& truncated) noexcept {
    if (truncated.count == 0) {
        return;
    }
    out.key("truncated");
    out.raw('[');
    for (std::size_t index = 0; index < truncated.count; ++index) {
        if (index != 0) {
            out.raw(',');
        }
        out.raw('"');
        out.raw(truncated.prefix[index]);
        out.raw(truncated.path[index]);
        out.raw('"');
    }
    out.raw(']');
}

/// Writes the per-kind keys into `out` and collects every cut into `truncated`.
/// Runs twice: once into scratch to learn the truncated paths, which precede
/// the per-kind keys on the line, and once for real.
void writeBody(Writer& out,
               const Record& record,
               const SurvivingFields& surviving,
               TruncatedPaths& truncated) noexcept {
    switch (record.kind) {
    case Kind::Log:
        out.key("severity");
        out.raw('"');
        out.raw(name(record.severity));
        out.raw('"');
        if (record.source) {
            out.key("source");
            out.open();
            out.key("file");
            if (out.string(record.source->file_name(), kMaximumStringValueBytes)) {
                truncated.add("source.file", {});
            }
            out.key("line");
            out.integer(static_cast<std::uint64_t>(record.source->line()));
            out.close();
        }
        if (!record.message.empty()) {
            out.key("message");
            if (out.string(record.message, kMaximumMessageBytes)) {
                truncated.add("message", {});
            }
        }
        break;
    case Kind::Span:
        out.key("phase");
        out.raw(record.phase == SpanPhase::Begin ? "\"begin\"" : "\"end\"");
        if (record.phase == SpanPhase::End) {
            out.key("durationNanoseconds");
            out.integer(record.durationNanoseconds);
            out.key("outcome");
            out.raw('"');
            out.raw(name(record.outcome));
            out.raw('"');
        }
        break;
    case Kind::Metric:
        out.key("metricKind");
        out.raw('"');
        out.raw(name(record.metricKind));
        out.raw('"');
        out.key("unit");
        out.raw('"');
        out.raw(name(record.unit));
        out.raw('"');
        if (record.metricKind == MetricKind::Distribution) {
            out.key("statistics");
            out.open();
            out.key("count");
            out.integer(record.statistics.count);
            out.key("minimum");
            out.real(record.statistics.minimum);
            out.key("maximum");
            out.real(record.statistics.maximum);
            out.key("p50");
            out.real(record.statistics.p50);
            out.key("p95");
            out.real(record.statistics.p95);
            out.key("p99");
            out.real(record.statistics.p99);
            out.close();
        } else {
            out.key("value");
            out.real(record.value);
        }
        break;
    }

    if (surviving.count != 0) {
        out.key("fields");
        out.open();
        for (std::size_t index = 0; index < surviving.count; ++index) {
            const Field& entry = *surviving.fields[index];
            out.key(entry.key.text);
            switch (entry.value.type) {
            case FieldValue::Type::String:
                if (out.string(entry.value.text, kMaximumStringValueBytes)) {
                    truncated.add("fields.", entry.key.text);
                }
                break;
            case FieldValue::Type::Integer:
                out.integer(entry.value.integer);
                break;
            case FieldValue::Type::Real:
                out.real(entry.value.real);
                break;
            case FieldValue::Type::Boolean:
                out.raw(entry.value.boolean ? "true" : "false");
                break;
            case FieldValue::Type::Null:
                out.raw("null");
                break;
            }
        }
        out.close();
    }
    if (surviving.dropped) {
        truncated.add("fields", {});
    }
}

/// The record's class: the highest among its surviving parts. A log's message
/// and source location are developer text and count as internal parts.
Sensitivity recordSensitivity(const Record& record, const SurvivingFields& surviving) noexcept {
    Sensitivity sensitivity = surviving.highest;
    if (record.kind == Kind::Log && (record.source || !record.message.empty())) {
        sensitivity = std::max(sensitivity, Sensitivity::Internal);
    }
    return sensitivity;
}

} // namespace

std::size_t serializeRecord(const Record& record, Sensitivity clearance, std::span<char> destination) noexcept {
    RAWFRAME_ASSERT(destination.size() >= kMaximumRecordLineBytes, "a record line needs its full ceiling");
    const SurvivingFields kSurviving = selectFields(record.fields);
    const Sensitivity kSensitivity = recordSensitivity(record, kSurviving);
    if (kSensitivity > clearance) {
        return 0;
    }

    // The truncated paths come before the body on the line but are only known
    // after writing it, so the body is written once into scratch to find them.
    std::array<char, kMaximumRecordLineBytes> scratch;
    TruncatedPaths truncated;
    {
        Writer probe{scratch};
        writeBody(probe, record, kSurviving, truncated);
    }

    const std::span<char> kLine = destination.first(kMaximumRecordLineBytes);
    Writer out{kLine};
    writeEnvelope(out, record, kSensitivity);
    writeTruncated(out, truncated);
    TruncatedPaths ignored;
    writeBody(out, record, kSurviving, ignored);
    out.close();
    out.raw('\n');
    if (!out.overflowed()) {
        return out.size();
    }

    // Still over the line ceiling: the overflow record keeps the envelope and,
    // for a log, its severity, so a reader of the tail still learns how serious
    // the lost record was. The envelope is bounded, so this always fits.
    Writer overflow{kLine};
    writeEnvelope(overflow, record, kSensitivity);
    overflow.key("truncated");
    overflow.raw("[\"record\"]");
    if (record.kind == Kind::Log) {
        overflow.key("severity");
        overflow.raw('"');
        overflow.raw(name(record.severity));
        overflow.raw('"');
    }
    overflow.close();
    overflow.raw('\n');
    RAWFRAME_CHECK(!overflow.overflowed(), "an overflow record fits by construction");
    return overflow.size();
}

NdjsonSink::NdjsonSink(StreamInfo stream, Sensitivity clearance, std::size_t bufferBytes, MonotonicClock clock)
    : stream_(stream), clearance_(clearance), clock_(clock != nullptr ? clock : &steadyClockNanoseconds) {
    RAWFRAME_CHECK(stream.configuration == "debug" || stream.configuration == "development" ||
                       stream.configuration == "shipping",
                   "a stream names one of the three SPEC-0003 configurations");
    fileStartNanoseconds_ = clock_();
    pending_.reserve(bufferBytes);
    draining_.reserve(bufferBytes);
}

void NdjsonSink::accept(const Record& record) noexcept {
    // Serialization, truncation, and redaction happen here, on the emitting
    // thread and outside the lock, so the buffer only ever holds finished bytes.
    std::array<char, kMaximumRecordLineBytes> line;
    const std::size_t kSize = serializeRecord(record, clearance_, line);
    const std::lock_guard kLock{mutex_};
    if (kSize == 0) {
        ++refused_;
        ++refusedUnreported_;
        return;
    }
    // Drop the newest: the records that explain a burst are at its start.
    if (kSize > pending_.capacity() - pending_.size()) {
        ++dropped_;
        ++unreported_;
        return;
    }
    pending_.insert(pending_.end(), line.begin(), line.begin() + static_cast<std::ptrdiff_t>(kSize));
}

namespace {

std::size_t serializeStreamHeader(const StreamInfo& stream, std::uint64_t now, std::span<char> destination) noexcept {
    Writer out{destination};
    out.open();
    out.key("schema");
    out.integer(static_cast<std::int64_t>(kSchema));
    out.key("kind");
    out.raw("\"stream\"");
    out.key("time");
    out.open();
    out.key("monotonicNanoseconds");
    out.integer(now);
    out.close();
    out.key("process");
    out.open();
    out.key("identity");
    static_cast<void>(out.string(stream.processIdentity, kMaximumStringValueBytes));
    out.key("startWallUnixNanoseconds");
    out.integer(stream.processStartWallUnixNanoseconds);
    out.close();
    out.key("build");
    out.open();
    out.key("receipt");
    static_cast<void>(out.string(stream.buildReceipt, kMaximumStringValueBytes));
    out.key("profile");
    static_cast<void>(out.string(stream.profile, kMaximumStringValueBytes));
    out.key("configuration");
    out.raw("\"build.");
    out.raw(stream.configuration);
    out.raw('"');
    out.key("targetRole");
    static_cast<void>(out.string(stream.targetRole, kMaximumIdentifierBytes));
    out.close();
    out.key("session");
    static_cast<void>(out.string(stream.session, kMaximumStringValueBytes));
    out.close();
    out.raw('\n');
    RAWFRAME_CHECK(!out.overflowed(), "the stream header is bounded well below a line");
    return out.size();
}

/// A loss the sink itself observed, reported as a counter so it reads like any
/// other suppression count.
std::size_t
serializeLoss(EventIdentity identity, std::uint64_t count, std::uint64_t now, std::span<char> destination) noexcept {
    const Record kRecord{.kind = Kind::Metric,
                         .time = {.monotonicNanoseconds = now, .wallUnixNanoseconds = std::nullopt},
                         .identity = identity,
                         .correlation = std::nullopt,
                         .fields = {},
                         .metricKind = MetricKind::Counter,
                         .unit = Unit::Count,
                         .value = static_cast<double>(count)};
    return serializeRecord(kRecord, Sensitivity::Public, destination);
}

} // namespace

bool NdjsonSink::drain(WriteBytes write, void* context) noexcept {
    std::uint64_t unreported = 0;
    std::uint64_t refused = 0;
    bool writeHeader = false;
    std::uint64_t fileStart = 0;
    {
        const std::lock_guard kLock{mutex_};
        std::swap(pending_, draining_);
        unreported = std::exchange(unreported_, 0);
        refused = std::exchange(refusedUnreported_, 0);
        writeHeader = !std::exchange(headerWritten_, true);
        fileStart = fileStartNanoseconds_;
    }

    const std::uint64_t kNow = clock_();
    std::array<char, kMaximumRecordLineBytes> line;
    bool succeeded = true;
    const auto kEmit = [&](std::span<const char> bytes) noexcept {
        if (succeeded && !bytes.empty() && !write(context, bytes)) {
            succeeded = false;
        }
    };
    if (writeHeader) {
        kEmit(std::span<const char>{line.data(), serializeStreamHeader(stream_, fileStart, line)});
    }
    kEmit(draining_);
    if (unreported != 0) {
        kEmit(std::span<const char>{line.data(), serializeLoss(kRecordsDropped, unreported, kNow, line)});
    }
    if (refused != 0) {
        kEmit(std::span<const char>{line.data(), serializeLoss(kRecordsRefused, refused, kNow, line)});
    }
    draining_.clear();
    return succeeded;
}

void NdjsonSink::startNewFile() noexcept {
    const std::uint64_t kNow = clock_();
    const std::lock_guard kLock{mutex_};
    headerWritten_ = false;
    fileStartNanoseconds_ = kNow;
}

std::uint64_t NdjsonSink::droppedRecords() const noexcept {
    const std::lock_guard kLock{mutex_};
    return dropped_;
}

std::uint64_t NdjsonSink::refusedRecords() const noexcept {
    const std::lock_guard kLock{mutex_};
    return refused_;
}

} // namespace rawframe::diagnostics
