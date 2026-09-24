// The SPEC-0047 conformance corpus for the NDJSON sink: every line is read back
// with a strict JSON reader, so a test proves what a consumer would see rather
// than what the writer meant.

#include "json.h"
#include "rawframe/diagnostics/ndjson_sink.h"
#include "rawframe/test/test.h"

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe::diagnostics;
using rawframe::diagnostics::testing::Json;
using rawframe::diagnostics::testing::JsonReader;
using rawframe::diagnostics::testing::splitLines;

namespace {

constexpr EventIdentity kIdentity{"test_domain", "something_happened"};

constexpr std::array<FieldKey, 40> kKeys = {"k0",  "k1",  "k2",  "k3",  "k4",  "k5",  "k6",  "k7",  "k8",  "k9",
                                            "k10", "k11", "k12", "k13", "k14", "k15", "k16", "k17", "k18", "k19",
                                            "k20", "k21", "k22", "k23", "k24", "k25", "k26", "k27", "k28", "k29",
                                            "k30", "k31", "k32", "k33", "k34", "k35", "k36", "k37", "k38", "k39"};

constexpr StreamInfo kStream{.processIdentity = "process-1",
                             .processStartWallUnixNanoseconds = 1785000000000000000,
                             .buildReceipt = "receipt-1",
                             .profile = "test",
                             .configuration = "development",
                             .targetRole = "tool",
                             .session = "session-1"};

Record logRecord(std::string_view message, std::span<const Field> fields = {}) {
    return Record{.kind = Kind::Log,
                  .time = {.monotonicNanoseconds = 42, .wallUnixNanoseconds = std::nullopt},
                  .identity = kIdentity,
                  .correlation = std::nullopt,
                  .fields = fields,
                  .severity = Severity::Warning,
                  .source = std::nullopt,
                  .message = message};
}

Record metricRecord(std::span<const Field> fields) {
    return Record{.kind = Kind::Metric,
                  .time = {.monotonicNanoseconds = 7, .wallUnixNanoseconds = std::nullopt},
                  .identity = kIdentity,
                  .correlation = std::nullopt,
                  .fields = fields,
                  .metricKind = MetricKind::Gauge,
                  .unit = Unit::Bytes,
                  .value = 3.5};
}

/// The record's line without its LF, or empty if it was refused. Checks that
/// the line ends in exactly one LF and fits the ceiling.
std::string serialize(const Record& record, Sensitivity clearance = Sensitivity::Personal) {
    std::array<char, kMaximumRecordLineBytes> line{};
    const std::size_t kSize = serializeRecord(record, clearance, line);
    if (kSize == 0) {
        return {};
    }
    RAWFRAME_EXPECT(kSize <= kMaximumRecordLineBytes);
    if (kSize > kMaximumRecordLineBytes) {
        return {};
    }
    RAWFRAME_EXPECT(line[kSize - 1] == '\n');
    const std::string_view kText{line.data(), kSize - 1};
    RAWFRAME_EXPECT(kText.find('\n') == std::string_view::npos);
    return std::string{kText};
}

Json parse(std::string_view line) {
    auto parsed = JsonReader{line}.read();
    RAWFRAME_EXPECT(parsed.has_value());
    return parsed ? *parsed : Json{};
}

std::vector<std::string> truncatedPaths(const Json& record) {
    std::vector<std::string> paths;
    if (const Json* truncated = record.find("truncated")) {
        for (const Json& item : truncated->items) {
            paths.push_back(item.text);
        }
    }
    return paths;
}

/// Collects everything a sink drains.
struct Collected {
    std::string bytes;
    bool fail = false;

    static bool write(void* context, std::span<const char> bytes) noexcept {
        auto& self = *static_cast<Collected*>(context);
        if (self.fail) {
            return false;
        }
        self.bytes.append(bytes.data(), bytes.size());
        return true;
    }
};

std::uint64_t fixedClock() noexcept {
    return 1000;
}

} // namespace

RAWFRAME_TEST(HostileTextStaysOneValidLine) {
    // LF, CR, tab, NUL, an encoded UTF-16 surrogate, a stray continuation byte,
    // and a truncated sequence at the end.
    const std::string kMessage = std::string{"line\none\rtwo\tthree"} + '\0' + "\xED\xA0\x80" + "\x80" + "\xE2\x82";
    const Field kFields[] = {field("detail", std::string_view{kMessage})};
    const Json kRecord = parse(serialize(logRecord(kMessage, kFields)));
    const Json* message = kRecord.find("message");
    RAWFRAME_EXPECT(message != nullptr);
    if (message != nullptr) {
        RAWFRAME_EXPECT(message->text.starts_with(std::string{"line\none\rtwo\tthree"} + '\0'));
        RAWFRAME_EXPECT(message->text.find("\xEF\xBF\xBD") != std::string::npos);
    }
    RAWFRAME_EXPECT(truncatedPaths(kRecord).empty());
}

RAWFRAME_TEST(EnvelopeKeysAppearInOrder) {
    const Field kFields[] = {field("count", 3)};
    Record record = logRecord("hello", kFields);
    record.time.wallUnixNanoseconds = 1785000000000000000;
    record.correlation = Correlation{.trace = {1, 2}, .span = 3, .parent = 4};
    record.source = std::source_location::current();
    const Json kRecord = parse(serialize(record));
    const std::vector<std::string> kExpected = {"schema",
                                                "kind",
                                                "time",
                                                "domain",
                                                "code",
                                                "sensitivity",
                                                "correlation",
                                                "severity",
                                                "source",
                                                "message",
                                                "fields"};
    RAWFRAME_EXPECT(kRecord.keys() == kExpected);
    RAWFRAME_EXPECT(kRecord.find("schema")->text == "1");
    RAWFRAME_EXPECT(kRecord.find("kind")->text == "log");
    RAWFRAME_EXPECT(kRecord.find("domain")->text == "test_domain");
    RAWFRAME_EXPECT(kRecord.find("code")->text == "something_happened");
    RAWFRAME_EXPECT(kRecord.find("severity")->text == "warning");
    RAWFRAME_EXPECT(
        (kRecord.find("time")->keys() == std::vector<std::string>{"monotonicNanoseconds", "wallUnixNanoseconds"}));
}

RAWFRAME_TEST(AbsentValuesAreMissingKeysNotNulls) {
    const Json kRecord = parse(serialize(logRecord("")));
    RAWFRAME_EXPECT((kRecord.keys() ==
                     std::vector<std::string>{"schema", "kind", "time", "domain", "code", "sensitivity", "severity"}));
    RAWFRAME_EXPECT((kRecord.find("time")->keys() == std::vector<std::string>{"monotonicNanoseconds"}));

    Record root = logRecord("root");
    root.correlation = Correlation{.trace = {1, 2}, .span = 3, .parent = std::nullopt};
    const Json kRoot = parse(serialize(root));
    RAWFRAME_EXPECT((kRoot.find("correlation")->keys() == std::vector<std::string>{"trace", "span"}));
}

RAWFRAME_TEST(CorrelationUsesCanonicalHex) {
    Record record = logRecord("x");
    record.correlation = Correlation{
        .trace = {0x0123456789abcdefULL, 0x0123456789abcdefULL}, .span = 0xfedcba9876543210ULL, .parent = 1};
    const Json kCorrelation = *parse(serialize(record)).find("correlation");
    RAWFRAME_EXPECT(kCorrelation.find("trace")->text == "0123456789abcdef0123456789abcdef");
    RAWFRAME_EXPECT(kCorrelation.find("span")->text == "fedcba9876543210");
    RAWFRAME_EXPECT(kCorrelation.find("parent")->text == "0000000000000001");
}

RAWFRAME_TEST(PerKindGrammarsCarryTheirKeys) {
    Record begin = logRecord("");
    begin.kind = Kind::Span;
    begin.phase = SpanPhase::Begin;
    RAWFRAME_EXPECT((parse(serialize(begin)).keys() ==
                     std::vector<std::string>{"schema", "kind", "time", "domain", "code", "sensitivity", "phase"}));

    Record end = begin;
    end.phase = SpanPhase::End;
    end.durationNanoseconds = 99;
    end.outcome = SpanOutcome::Cancelled;
    const Json kEnd = parse(serialize(end));
    RAWFRAME_EXPECT(kEnd.find("phase")->text == "end");
    RAWFRAME_EXPECT(kEnd.find("durationNanoseconds")->text == "99");
    RAWFRAME_EXPECT(kEnd.find("outcome")->text == "cancelled");

    const Json kGauge = parse(serialize(metricRecord({})));
    RAWFRAME_EXPECT(kGauge.find("metricKind")->text == "gauge");
    RAWFRAME_EXPECT(kGauge.find("unit")->text == "bytes");
    RAWFRAME_EXPECT(kGauge.find("value")->text == "3.5");
    RAWFRAME_EXPECT(kGauge.find("statistics") == nullptr);

    Record distribution = metricRecord({});
    distribution.metricKind = MetricKind::Distribution;
    distribution.statistics = Statistics{.count = 4, .minimum = 1, .maximum = 9, .p50 = 2, .p95 = 8, .p99 = 9};
    const Json kDistribution = parse(serialize(distribution));
    RAWFRAME_EXPECT(kDistribution.find("value") == nullptr);
    RAWFRAME_EXPECT((kDistribution.find("statistics")->keys() ==
                     std::vector<std::string>{"count", "minimum", "maximum", "p50", "p95", "p99"}));
}

RAWFRAME_TEST(FieldValuesKeepTheirTypes) {
    // A string literal must stay a string, not decay to a pointer and become a
    // boolean.
    const Field kFields[] = {field("literal", "text"), field("count", -3), field("ratio", 0.5), field("flag", false)};
    const Json kFieldsJson = *parse(serialize(metricRecord(kFields))).find("fields");
    RAWFRAME_EXPECT(kFieldsJson.find("literal")->type == Json::Type::String);
    RAWFRAME_EXPECT(kFieldsJson.find("literal")->text == "text");
    RAWFRAME_EXPECT(kFieldsJson.find("count")->text == "-3");
    RAWFRAME_EXPECT(kFieldsJson.find("ratio")->text == "0.5");
    RAWFRAME_EXPECT(kFieldsJson.find("flag")->type == Json::Type::Boolean && !kFieldsJson.find("flag")->boolean);
}

RAWFRAME_TEST(NonFiniteRealsAreNull) {
    const Field kFields[] = {field("ratio", std::numeric_limits<double>::infinity())};
    Record record = metricRecord(kFields);
    record.value = std::numeric_limits<double>::quiet_NaN();
    const Json kRecord = parse(serialize(record));
    RAWFRAME_EXPECT(kRecord.find("value")->type == Json::Type::Null);
    RAWFRAME_EXPECT(kRecord.find("fields")->find("ratio")->type == Json::Type::Null);
}

RAWFRAME_TEST(SensitivityIsTheHighestSurvivingClass) {
    const Field kPublic[] = {field("a", 1, Sensitivity::Public)};
    const Field kInternal[] = {field("a", 1, Sensitivity::Public), field("b", 2, Sensitivity::Internal)};
    const Field kPersonal[] = {field("a", 1, Sensitivity::Personal), field("b", 2, Sensitivity::Public)};
    RAWFRAME_EXPECT(parse(serialize(metricRecord(kPublic))).find("sensitivity")->text == "public");
    RAWFRAME_EXPECT(parse(serialize(metricRecord(kInternal))).find("sensitivity")->text == "internal");
    RAWFRAME_EXPECT(parse(serialize(metricRecord(kPersonal))).find("sensitivity")->text == "personal");
    // A log's own text is an internal part of it.
    RAWFRAME_EXPECT(parse(serialize(logRecord("text", kPublic))).find("sensitivity")->text == "internal");
}

RAWFRAME_TEST(SecretFieldsAreRemovedNotMasked) {
    const Field kFields[] = {field("token", "hunter2-secret-value", Sensitivity::Secret),
                             field("visible", "shown", Sensitivity::Public)};
    const std::string kLine = serialize(metricRecord(kFields));
    RAWFRAME_EXPECT(kLine.find("token") == std::string::npos);
    RAWFRAME_EXPECT(kLine.find("hunter2") == std::string::npos);
    const Json kRecord = parse(kLine);
    RAWFRAME_EXPECT(kRecord.find("fields")->keys() == std::vector<std::string>{"visible"});
    RAWFRAME_EXPECT(kRecord.find("fields")->find("visible")->text == "shown");
    RAWFRAME_EXPECT(kRecord.find("sensitivity")->text == "public");
    RAWFRAME_EXPECT(truncatedPaths(kRecord).empty());
}

RAWFRAME_TEST(RecordsAboveClearanceAreRefusedAndReported) {
    const Field kFields[] = {field("player", "someone", Sensitivity::Personal)};
    RAWFRAME_EXPECT(serialize(metricRecord(kFields), Sensitivity::Internal).empty());

    NdjsonSink sink{kStream, Sensitivity::Internal, 4096, &fixedClock};
    sink.accept(metricRecord(kFields));
    sink.accept(metricRecord({}));
    RAWFRAME_EXPECT(sink.refusedRecords() == 1);
    Collected out;
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &out));
    const auto kLines = splitLines(out.bytes);
    RAWFRAME_EXPECT(kLines && kLines->size() == 3);
    if (kLines && kLines->size() == 3) {
        const Json kReport = parse((*kLines)[2]);
        RAWFRAME_EXPECT(kReport.find("code")->text == "records_refused");
        RAWFRAME_EXPECT(kReport.find("value")->text == "1");
    }
}

RAWFRAME_TEST(StringsAreCutAtTheirCeilingOnACharacterBoundary) {
    // Each value is one byte short of its ceiling, then a two-byte character
    // that would cross it.
    const std::string kLongField = std::string(kMaximumStringValueBytes - 1, 'a') + "\xC3\xA9";
    const std::string kLongMessage = std::string(kMaximumMessageBytes - 1, 'm') + "\xC3\xA9";
    const std::string kExactField = std::string(kMaximumStringValueBytes - 2, 'b') + "\xC3\xA9";
    const Field kFields[] = {field("long", std::string_view{kLongField}),
                             field("exact", std::string_view{kExactField})};
    const Json kRecord = parse(serialize(logRecord(kLongMessage, kFields)));
    RAWFRAME_EXPECT(kRecord.find("message")->text == std::string(kMaximumMessageBytes - 1, 'm'));
    RAWFRAME_EXPECT(kRecord.find("fields")->find("long")->text == std::string(kMaximumStringValueBytes - 1, 'a'));
    RAWFRAME_EXPECT(kRecord.find("fields")->find("exact")->text == kExactField);
    const auto kPaths = truncatedPaths(kRecord);
    RAWFRAME_EXPECT((kPaths == std::vector<std::string>{"message", "fields.long"}));
    // `truncated` sits in the envelope, before the per-kind keys.
    RAWFRAME_EXPECT(kRecord.keys()[6] == "truncated");
}

RAWFRAME_TEST(FieldsBeyondTheCountCeilingAreDropped) {
    std::vector<Field> fields;
    for (std::size_t index = 0; index < kKeys.size(); ++index) {
        fields.push_back(field(kKeys[index], static_cast<std::int64_t>(index)));
    }
    const Json kRecord = parse(serialize(metricRecord(fields)));
    RAWFRAME_EXPECT(kRecord.find("fields")->values.size() == kMaximumFieldCount);
    RAWFRAME_EXPECT(kRecord.find("fields")->find("k31") != nullptr);
    RAWFRAME_EXPECT(kRecord.find("fields")->find("k32") == nullptr);
    RAWFRAME_EXPECT(truncatedPaths(kRecord) == std::vector<std::string>{"fields"});
}

RAWFRAME_TEST(TheTruncatedListIsBounded) {
    const std::string kLong(kMaximumStringValueBytes + 1, 'x');
    std::vector<Field> fields;
    for (std::size_t index = 0; index < 12; ++index) {
        fields.push_back(field(kKeys[index], std::string_view{kLong}));
    }
    const Json kRecord = parse(serialize(metricRecord(fields)));
    const auto kPaths = truncatedPaths(kRecord);
    RAWFRAME_EXPECT(kPaths.size() == kMaximumTruncatedEntries);
    RAWFRAME_EXPECT(kPaths.front() == "fields.k0");
}

RAWFRAME_TEST(AnOversizeRecordBecomesAnOverflowRecord) {
    // Every ceiling at once, in characters that each escape to six bytes: the
    // largest record the producer side can hand over.
    const std::string kControl(kMaximumMessageBytes, '\x01');
    std::vector<Field> fields;
    for (std::size_t index = 0; index < kMaximumFieldCount; ++index) {
        fields.push_back(field(kKeys[index], std::string_view{kControl}.substr(0, kMaximumStringValueBytes)));
    }
    Record record = logRecord(kControl, fields);
    record.severity = Severity::Critical;
    record.source = std::source_location::current();
    record.correlation = Correlation{.trace = {5, 6}, .span = 7, .parent = 8};
    record.time.wallUnixNanoseconds = -1;
    const Json kRecord = parse(serialize(record));
    RAWFRAME_EXPECT(
        (kRecord.keys() ==
         std::vector<std::string>{
             "schema", "kind", "time", "domain", "code", "sensitivity", "correlation", "truncated", "severity"}));
    RAWFRAME_EXPECT(truncatedPaths(kRecord) == std::vector<std::string>{"record"});
    RAWFRAME_EXPECT(kRecord.find("kind")->text == "log");
    RAWFRAME_EXPECT(kRecord.find("severity")->text == "critical");
    RAWFRAME_EXPECT(kRecord.find("code")->text == "something_happened");
    RAWFRAME_EXPECT(kRecord.find("sensitivity")->text == "internal");
}

RAWFRAME_TEST(TheStreamHeaderOpensEveryFile) {
    NdjsonSink sink{kStream, Sensitivity::Personal, 4096, &fixedClock};
    sink.accept(logRecord("first"));
    Collected out;
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &out));
    sink.accept(logRecord("second"));
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &out));
    sink.startNewFile();
    sink.accept(logRecord("third"));
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &out));

    const auto kLines = splitLines(out.bytes);
    RAWFRAME_EXPECT(kLines && kLines->size() == 5);
    if (!kLines || kLines->size() != 5) {
        return;
    }
    const Json kHeader = parse((*kLines)[0]);
    RAWFRAME_EXPECT(
        (kHeader.keys() == std::vector<std::string>{"schema", "kind", "time", "process", "build", "session"}));
    RAWFRAME_EXPECT(kHeader.find("kind")->text == "stream");
    RAWFRAME_EXPECT(kHeader.find("time")->find("monotonicNanoseconds")->text == "1000");
    RAWFRAME_EXPECT(kHeader.find("build")->find("configuration")->text == "build.development");
    RAWFRAME_EXPECT(kHeader.find("process")->find("startWallUnixNanoseconds")->text == "1785000000000000000");
    RAWFRAME_EXPECT(parse((*kLines)[1]).find("message")->text == "first");
    RAWFRAME_EXPECT(parse((*kLines)[2]).find("message")->text == "second");
    RAWFRAME_EXPECT(parse((*kLines)[3]).find("kind")->text == "stream");
    RAWFRAME_EXPECT(parse((*kLines)[4]).find("message")->text == "third");
}

RAWFRAME_TEST(ABurstDropsTheNewestWithoutAllocating) {
    NdjsonSink sink{kStream, Sensitivity::Personal, 1024, &fixedClock};
    const std::size_t kBefore = rawframe::test::allocationCount();
    // Nothing drains during the burst, so no file is ever touched: accept does
    // no I/O.
    for (int index = 0; index < 1000; ++index) {
        const Field kFields[] = {field("index", index)};
        sink.accept(logRecord("burst", kFields));
    }
    RAWFRAME_EXPECT(rawframe::test::allocationCount() == kBefore);
    const std::uint64_t kDropped = sink.droppedRecords();
    RAWFRAME_EXPECT(kDropped > 900);

    Collected out;
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &out));
    const auto kLines = splitLines(out.bytes);
    RAWFRAME_EXPECT(kLines.has_value());
    if (!kLines) {
        return;
    }
    // Header, the survivors in emit order starting from the first, then the
    // drop count.
    const std::size_t kKept = kLines->size() - 2;
    RAWFRAME_EXPECT(kKept + kDropped == 1000);
    for (std::size_t index = 0; index < kKept; ++index) {
        const Json kRecord = parse((*kLines)[index + 1]);
        RAWFRAME_EXPECT(kRecord.find("fields")->find("index")->text == std::to_string(index));
    }
    const Json kReport = parse(kLines->back());
    RAWFRAME_EXPECT(kReport.find("code")->text == "records_dropped");
    RAWFRAME_EXPECT(kReport.find("value")->text == std::to_string(kDropped));

    // The report is made once; the next drain has nothing to say.
    Collected again;
    RAWFRAME_EXPECT(sink.drain(&Collected::write, &again));
    RAWFRAME_EXPECT(again.bytes.empty());
}

RAWFRAME_TEST(AFailedWriteIsReported) {
    NdjsonSink sink{kStream, Sensitivity::Personal, 1024, &fixedClock};
    sink.accept(logRecord("lost"));
    Collected out;
    out.fail = true;
    RAWFRAME_EXPECT(!sink.drain(&Collected::write, &out));
}

RAWFRAME_TEST(RandomHostileRecordsAlwaysReadBack) {
    // Seeded, so a failure reproduces. Bytes are drawn mostly from the ranges
    // that stress escaping and UTF-8 repair.
    std::uint64_t state = 0x5EEDULL;
    const auto kNext = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33U);
    };
    const auto kHostile = [&](std::size_t length) {
        std::string text;
        for (std::size_t index = 0; index < length; ++index) {
            const std::uint32_t kPick = kNext() % 4;
            const std::uint32_t kByte = kNext();
            text.push_back(static_cast<char>(kPick == 0 ? kByte % 0x20U : kPick == 1 ? 0x80U + kByte % 0x80U : kByte));
        }
        return text;
    };
    for (int round = 0; round < 300; ++round) {
        const std::string kMessage = kHostile(kNext() % 1600);
        std::vector<std::string> texts;
        const std::size_t kFieldCount = kNext() % 40;
        for (std::size_t index = 0; index < kFieldCount; ++index) {
            texts.push_back(kHostile(kNext() % 700));
        }
        std::vector<Field> fields;
        for (std::size_t index = 0; index < kFieldCount; ++index) {
            fields.push_back(
                field(kKeys[index], std::string_view{texts[index]}, static_cast<Sensitivity>(kNext() % 4)));
        }
        const Json kRecord = parse(serialize(logRecord(kMessage, fields)));
        RAWFRAME_EXPECT(kRecord.find("severity") != nullptr);
        RAWFRAME_EXPECT(kRecord.find("sensitivity")->text != "secret");
    }
}
