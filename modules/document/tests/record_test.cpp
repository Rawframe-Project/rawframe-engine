// Typed records: declared fields in declared order, defaults omitted, and
// refusals that name the field.

#include "rawframe/document/errors.h"
#include "rawframe/document/record.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>

using namespace rawframe;
using namespace rawframe::document;

namespace {

constexpr std::array<std::string_view, 4> kFields = {"name", "slot", "scale", "invert"};

/// The refusal's code and path, when `text` read as a binding is refused.
std::pair<DocumentError, std::string> refusalOf(std::string_view text) {
    const auto kValue = parse(text);
    const auto kRead = [&]() -> result::Status {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*kValue, kFields, "bindings[0]"));
        RAWFRAME_TRY(kRecord.text("name"));
        RAWFRAME_TRY(kRecord.integer("slot", 0));
        RAWFRAME_TRY(kRecord.real("scale", 1.0));
        RAWFRAME_TRY(kRecord.truth("invert", false));
        return {};
    }();
    if (kRead.has_value()) {
        return {DocumentError{}, ""};
    }
    std::string path;
    for (const auto& field : kRead.error().context()) {
        if (field.key == "path") {
            path = field.value;
        }
    }
    return {static_cast<DocumentError>(kRead.error().code().value), path};
}

} // namespace

RAWFRAME_TEST(ARecordReadsItsDeclaredFields) {
    const auto kValue = parse(R"({"name": "jump", "slot": 1, "scale": 0.5, "invert": true})");
    const auto kRecord = Record::of(*kValue, kFields, "bindings[0]");
    RAWFRAME_EXPECT(kRecord.has_value() && *kRecord->text("name") == "jump" && *kRecord->integer("slot", 0) == 1 &&
                    *kRecord->real("scale", 1.0) == 0.5 && *kRecord->truth("invert", false));
    // Absent is the default.
    const auto kShort = parse(R"({"name": "jump"})");
    const auto kDefaults = Record::of(*kShort, kFields, "bindings[0]");
    RAWFRAME_EXPECT(kDefaults.has_value() && *kDefaults->integer("slot", 0) == 0 &&
                    *kDefaults->real("scale", 1.0) == 1.0 && !*kDefaults->truth("invert", false) &&
                    kDefaults->optionalText("name").has_value());
}

RAWFRAME_TEST(ARecordRefusesWhatItDoesNotDeclareOrWritesTwoWays) {
    using Refusal = std::pair<DocumentError, std::string>;
    RAWFRAME_EXPECT(refusalOf(R"({"name": "jump", "extra": 1})") ==
                    Refusal(DocumentError::Invalid, "bindings[0].extra"));
    RAWFRAME_EXPECT(refusalOf(R"({"slot": 1, "name": "jump"})") ==
                    Refusal(DocumentError::NotCanonical, "bindings[0].name"));
    RAWFRAME_EXPECT(refusalOf(R"({"name": "jump", "scale": 1})") ==
                    Refusal(DocumentError::NotCanonical, "bindings[0].scale"));
    RAWFRAME_EXPECT(refusalOf(R"({"name": "jump", "invert": false})") ==
                    Refusal(DocumentError::NotCanonical, "bindings[0].invert"));
    RAWFRAME_EXPECT(refusalOf(R"({"name": "jump", "slot": 1.5})") ==
                    Refusal(DocumentError::Invalid, "bindings[0].slot"));
    RAWFRAME_EXPECT(refusalOf(R"({"name": 3})") == Refusal(DocumentError::Invalid, "bindings[0].name"));
    RAWFRAME_EXPECT(refusalOf(R"({"slot": 2})") == Refusal(DocumentError::Invalid, "bindings[0].name"));
    RAWFRAME_EXPECT(refusalOf(R"([1])") == Refusal(DocumentError::Invalid, "bindings[0]"));
    RAWFRAME_EXPECT(refusalOf(R"({"name": "jump"})") == Refusal(DocumentError{}, ""));
}
