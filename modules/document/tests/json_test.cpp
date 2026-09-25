// Authored JSON documents: canonical text reads and writes back to the same
// bytes, anything else is refused where it is, and hostile bytes never get
// past the reader into a value that writes differently. Canonical records
// are JCS bytes, members sorted and integers only.

#include "rawframe/document/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::document;

namespace {

constexpr std::string_view kCanonical = R"({
  "kind": "input.actions",
  "formatVersion": 1,
  "actions": [
    {
      "actionId": "0f3a9c2e7b1d4e58",
      "name": "jump",
      "pressThreshold": 0.5,
      "scale": -0.25,
      "tiny": 1e-7,
      "huge": 1e+300,
      "zero": -0,
      "wide": 123456789012345678901234567890,
      "consume": true,
      "none": null
    }
  ],
  "empty": {},
  "nothing": [],
  "text": "tab\there \"quoted\" back\\slash \u0001 caf\u00e9 is café, \ud83d\ude00 is 😀"
}
)";

/// The line a refusal names, or nought.
std::size_t lineOf(const result::Error& error) {
    for (const auto& field : error.context()) {
        if (field.key == "line") {
            return std::stoul(std::string{field.value});
        }
    }
    return 0;
}

bool refused(std::string_view text, DocumentError expected) {
    const auto kRead = parse(text);
    return !kRead.has_value() && kRead.error().code() == code(expected);
}

} // namespace

RAWFRAME_TEST(CanonicalTextReadsAndWritesBackTheSame) {
    // The sample escapes what the writer escapes differently, so it is not
    // canonical as written; what the writer makes of it is, and stays so.
    const auto kRead = parse(kCanonical);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const std::string kWritten = write(*kRead);
    const auto kAgain = parseCanonical(kWritten);
    RAWFRAME_EXPECT(kAgain.has_value() && write(*kAgain) == kWritten);
    // Only what JSON requires is escaped: é and the emoji are bytes, \u0001
    // stays an escape, lowercase.
    RAWFRAME_EXPECT(kWritten.find("caf\xC3\xA9 is caf\xC3\xA9") != std::string::npos);
    RAWFRAME_EXPECT(kWritten.find("\\u0001") != std::string::npos);
    RAWFRAME_EXPECT(kWritten.find("\xF0\x9F\x98\x80 is \xF0\x9F\x98\x80") != std::string::npos);

    const Value& action = kRead->find("actions")->items()[0];
    RAWFRAME_EXPECT(*action.find("pressThreshold")->real() == 0.5);
    RAWFRAME_EXPECT(!action.find("pressThreshold")->integer().has_value());
    RAWFRAME_EXPECT(*kRead->find("formatVersion")->integer() == 1);
    // Too wide for an integer, but kept as written.
    RAWFRAME_EXPECT(!action.find("wide")->integer().has_value());
    RAWFRAME_EXPECT(kWritten.find("123456789012345678901234567890") != std::string::npos);
    RAWFRAME_EXPECT(*action.find("consume")->truth() && action.find("none")->isNull());
    RAWFRAME_EXPECT(kRead->find("empty")->items().empty() && kRead->find("missing") == nullptr);
    // Members keep their order.
    RAWFRAME_EXPECT(kRead->names()[0] == "kind" && kRead->names()[1] == "formatVersion");
}

RAWFRAME_TEST(WhatIsNotWrittenCanonicallyIsRefusedWhereItDiffers) {
    const std::string kGood = "{\n  \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n";
    RAWFRAME_EXPECT(parseCanonical(kGood).has_value());
    struct Case {
        std::string text;
        std::size_t line;
    };
    const std::vector<Case> kCases = {
        {"{\n  \"a\": 1,\n  \"b\": [\n    0.50,\n    true\n  ]\n}\n", 4},
        {"{\n  \"a\": 1,\n  \"b\": [\n    5e-1,\n    true\n  ]\n}\n", 4},
        {"{\n  \"a\":1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n", 2},
        {"{\n    \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n", 2},
        {"{\n  \"a\": 1,\n  \"b\": [0.5, true]\n}\n", 3},
        {"{\n  \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}", 7},
        {"{\n  \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n\n", 8},
        {"{\r\n  \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n", 1},
        {"{\n  \"\\u0061\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n", 2},
        {"{\n  \"a\": 1,\n  \"b\": [\n    0.5,\n    true\n  ]\n}\n", 0},
    };
    for (const Case& each : kCases) {
        const auto kRead = parseCanonical(each.text);
        if (each.line == 0) {
            RAWFRAME_EXPECT(kRead.has_value());
            continue;
        }
        RAWFRAME_EXPECT(!kRead.has_value() && kRead.error().code() == code(DocumentError::NotCanonical) &&
                        lineOf(kRead.error()) == each.line);
        // Every one of them is well formed, and means what the good one does.
        const auto kLoose = parse(each.text);
        RAWFRAME_EXPECT(kLoose.has_value() && write(*kLoose) == kGood);
    }
}

RAWFRAME_TEST(MalformedTextIsRefused) {
    for (const std::string_view kText : {
             std::string_view{"\xEF\xBB\xBF{}"},
             std::string_view{"{\"a\": 1, \"a\": 2}"},
             std::string_view{"[1, 2,]"},
             std::string_view{"{\"a\": 1,}"},
             std::string_view{"01"},
             std::string_view{"1."},
             std::string_view{".5"},
             std::string_view{"+1"},
             std::string_view{"-"},
             std::string_view{"1e"},
             std::string_view{"NaN"},
             std::string_view{"Infinity"},
             std::string_view{"1e400"},
             std::string_view{"-1e400"},
             std::string_view{"\"\\ud800\""},
             std::string_view{"\"\\udc00\""},
             std::string_view{"\"\\ud800\\u0041\""},
             std::string_view{"\"\\u12\""},
             std::string_view{"\"\\x41\""},
             std::string_view{"\"\xC0\x80\""},
             std::string_view{"\"\xED\xA0\x80\""},
             std::string_view{"\"\xF4\x90\x80\x80\""},
             std::string_view{"\"\xE2\x82\""},
             std::string_view{"\"\xFF\""},
             std::string_view{"\"a\nb\""},
             std::string_view{"'a'"},
             std::string_view{"// no\n{}"},
             std::string_view{"\"open"},
             std::string_view{"{} {}"},
             std::string_view{""},
             std::string_view{"   "},
             std::string_view{"tru"},
             std::string_view{"{\"a\" 1}"},
             std::string_view{"{1: 2}"},
         }) {
        RAWFRAME_EXPECT(refused(kText, DocumentError::Malformed));
    }
    // The place is told: the duplicate's object starts on line 2.
    const auto kDuplicate = parse("[\n  {\"a\": 1, \"a\": 2}\n]");
    RAWFRAME_EXPECT(!kDuplicate.has_value() && lineOf(kDuplicate.error()) == 2);
}

RAWFRAME_TEST(ReadersHaveBounds) {
    const auto kNested = [](int levels) {
        return std::string(static_cast<std::size_t>(levels), '[') + std::string(static_cast<std::size_t>(levels), ']');
    };
    // Sixty-four levels are allowed by default, sixty-five are not.
    RAWFRAME_EXPECT(parse(kNested(64)).has_value());
    RAWFRAME_EXPECT(refused(kNested(65), DocumentError::TooLarge));
    RAWFRAME_EXPECT(parse(kNested(65), {.maximumBytes = 1024, .maximumDepth = 65}).has_value());
    const auto kLong = parse("[1, 2, 3]", {.maximumBytes = 8, .maximumDepth = 4});
    RAWFRAME_EXPECT(!kLong.has_value() && kLong.error().code() == code(DocumentError::TooLarge));
}

RAWFRAME_TEST(MadeValuesWriteCanonically) {
    Value root = Value::object();
    root.add("integer", Value::integer(-42));
    root.add("real", Value::real(0.1));
    root.add("third", Value::real(1.0 / 3.0));
    root.add("whole", Value::real(2.0));
    root.add("nan", Value::real(std::numeric_limits<double>::quiet_NaN()));
    root.add("list", Value::array({Value::boolean(false), Value::string("x\ty")}));
    const std::string kWritten = write(root);
    RAWFRAME_EXPECT(kWritten ==
                    "{\n  \"integer\": -42,\n  \"real\": 0.1,\n  \"third\": 0.3333333333333333,\n"
                    "  \"whole\": 2,\n  \"nan\": null,\n  \"list\": [\n    false,\n    \"x\\ty\"\n  ]\n}\n");
    const auto kRead = parseCanonical(kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead->find("third")->real() == 1.0 / 3.0);
    // The hash-input form: the same members and numbers, no whitespace.
    root.add("empty", Value::object());
    RAWFRAME_EXPECT(writeCompact(root) == "{\"integer\":-42,\"real\":0.1,\"third\":0.3333333333333333,\"whole\":2,"
                                          "\"nan\":null,\"list\":[false,\"x\\ty\"],\"empty\":{}}");
}

RAWFRAME_TEST(HostileBytesNeverWriteDifferentlyThanTheyRead) {
    // Seeded mutations of a real document: whatever the reader accepts writes
    // canonical text that reads back to the same bytes.
    const std::string kSeed = write(*parse(kCanonical));
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto kNext = [&state] {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    constexpr std::string_view kInserted = "{}[]\",:\\0e-.\xC3\xF0";
    int accepted = 0;
    for (int round = 0; round < 20'000; ++round) {
        std::string text = kSeed;
        const int kEdits = 1 + static_cast<int>(kNext() % 4);
        for (int edit = 0; edit < kEdits && !text.empty(); ++edit) {
            const std::size_t kAt = kNext() % text.size();
            switch (kNext() % 3) {
            case 0:
                text[kAt] = static_cast<char>(kNext() & 0xFFU);
                break;
            case 1:
                text.erase(kAt, 1 + (kNext() % 8));
                break;
            default:
                text.insert(kAt, 1, kInserted[kNext() % kInserted.size()]);
                break;
            }
        }
        const auto kRead = parse(text);
        if (!kRead.has_value()) {
            continue;
        }
        ++accepted;
        const std::string kWritten = write(*kRead);
        const auto kAgain = parseCanonical(kWritten);
        RAWFRAME_EXPECT(kAgain.has_value() && write(*kAgain) == kWritten);
    }
    // Some mutations land inside strings and numbers and stay JSON.
    RAWFRAME_EXPECT(accepted > 100);
}

RAWFRAME_TEST(CanonicalRecordsAreJcsBytes) {
    // Members sorted at every depth, no whitespace, strings escaped as JCS
    // escapes them, whatever order the members were made in.
    const auto kRead = parse("{\"z\": [3, {\"b\": null, \"a\": true}], \"a\": \"\\u0001\\\"\u00e9\", \"m\": -12}");
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kBytes = writeCanonicalRecord(*kRead);
    RAWFRAME_EXPECT(kBytes.has_value() &&
                    *kBytes == "{\"a\":\"\\u0001\\\"\u00e9\",\"m\":-12,\"z\":[3,{\"a\":true,\"b\":null}]}");
    if (kBytes.has_value()) {
        RAWFRAME_EXPECT(parseCanonicalRecord(*kBytes).has_value());
    }

    // Refused to write: not an object, a fraction, an integer past 2^53 - 1,
    // a name that is not printable ASCII, and a name twice.
    const auto kInvalid = [](const Value& value) {
        const auto kWritten = writeCanonicalRecord(value);
        return !kWritten.has_value() && kWritten.error().code() == code(DocumentError::Invalid);
    };
    RAWFRAME_EXPECT(kInvalid(Value::array()));
    Value fraction = Value::object();
    fraction.add("x", Value::real(0.5));
    RAWFRAME_EXPECT(kInvalid(fraction));
    Value wide = Value::object();
    wide.add("x", Value::integer(9'007'199'254'740'992));
    RAWFRAME_EXPECT(kInvalid(wide));
    Value safe = Value::object();
    safe.add("x", Value::integer(-9'007'199'254'740'991));
    RAWFRAME_EXPECT(writeCanonicalRecord(safe).has_value());
    Value named = Value::object();
    named.add("\u00e9", Value::integer(1));
    RAWFRAME_EXPECT(kInvalid(named));
    Value twice = Value::object();
    twice.add("x", Value::integer(1));
    twice.add("x", Value::integer(2));
    RAWFRAME_EXPECT(kInvalid(twice));

    // Refused to read: whitespace, a final line feed, unsorted members, a
    // number not in its one form, and a fraction.
    for (const std::string_view kText : {std::string_view{"{\"a\": 1}"},
                                         std::string_view{"{\"a\":1}\n"},
                                         std::string_view{"{\"b\":1,\"a\":2}"},
                                         std::string_view{"{\"a\":-0}"},
                                         std::string_view{"{\"a\":1.0}"}}) {
        RAWFRAME_EXPECT(!parseCanonicalRecord(kText).has_value());
    }
}
