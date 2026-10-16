// A data document authored (D811): a value set at a path through an item
// known by its key, a new item and a new member kept last in the order the
// document holds, a change its reader refuses kept nowhere, and the history
// stepped back.

#include "rawframe/authoring/authored_data.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/test/test.h"

#include <array>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;
using document::Value;

namespace {

constexpr std::string_view kText = R"({
  "kind": "test.data",
  "formatVersion": 1,
  "actions": [
    {
      "actionId": "a1",
      "name": "move",
      "bindings": []
    }
  ]
})";

/// Reads as its kind: every action named by text.
result::Status reads(std::string_view text) {
    const auto kParsed = document::parse(text);
    const Value* kActions = kParsed.has_value() ? kParsed->find("actions") : nullptr;
    bool sound = kActions != nullptr && kActions->kind() == Value::Kind::Array;
    for (const Value& kAction : sound ? kActions->items() : std::span<const Value>{}) {
        const Value* kName = kAction.find("name");
        sound = sound && kName != nullptr && kName->kind() == Value::Kind::String;
    }
    if (!sound) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAuthoringDomain,
                            code(AuthoringError::ValidationFailed),
                            "every action is named");
    }
    return {};
}

Operation set(std::string path, std::optional<Value> value) {
    return SetDataValue{.path = std::move(path), .value = std::move(value)};
}

} // namespace

RAWFRAME_TEST(ADataDocumentsValuesAreSetAtTheirPaths) {
    auto opened = AuthoredData::open({}, kText, &reads);
    RAWFRAME_EXPECT(opened.has_value());
    AuthoredData& data = **opened;
    const std::array<Operation, 1> kRename = {set("actions[actionId=a1].name", Value::string("walk"))};
    RAWFRAME_EXPECT(data.change(data.generation(), kRename).has_value());
    RAWFRAME_EXPECT(data.text().find("\"name\": \"walk\"") != std::string::npos);
    // A new item last among the array's, a new member last among the root's.
    Value jump = Value::object();
    jump.add("actionId", Value::string("b2"));
    jump.add("name", Value::string("jump"));
    const std::array<Operation, 2> kAdded = {set("actions[actionId=b2]", jump), set("notes", Value::string("two"))};
    RAWFRAME_EXPECT(data.change(data.generation(), kAdded).has_value());
    const std::string kNow = data.text();
    RAWFRAME_EXPECT(kNow.find("\"kind\"") < kNow.find("\"formatVersion\"") &&
                    kNow.find("\"walk\"") < kNow.find("\"jump\"") && kNow.find("\"actions\"") < kNow.find("\"notes\""));
    RAWFRAME_EXPECT(data.summaryAt(1) == "set actions[b2] and 1 more");
    // What the reader refuses is kept nowhere.
    const std::array<Operation, 1> kUnnamed = {set("actions[actionId=a1].name", Value::integer(5))};
    const auto kRefused = data.change(data.generation(), kUnnamed);
    RAWFRAME_EXPECT(!kRefused.has_value() && kRefused.error().code() == code(AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(data.text() == kNow);
    // Nor a path out of form, nor one through an item the document lacks.
    const std::array<Operation, 1> kBadPath = {set("actions[a1].name", Value::string("x"))};
    RAWFRAME_EXPECT(!data.change(data.generation(), kBadPath).has_value());
    const std::array<Operation, 1> kNoItem = {set("actions[actionId=c3].name", Value::string("x"))};
    RAWFRAME_EXPECT(!data.change(data.generation(), kNoItem).has_value());
    // Stepped back, the rename and the additions both.
    RAWFRAME_EXPECT(data.undo(data.generation()).has_value() && data.undo(data.generation()).has_value());
    RAWFRAME_EXPECT(data.text().find("\"move\"") != std::string::npos && data.text().find("jump") == std::string::npos);
}

RAWFRAME_TEST(ADataPathReadsItsStepsAndRefusesTheRest) {
    const auto kPath = dataPathOf("master.children[busId=ea6b].effects");
    RAWFRAME_EXPECT(kPath.has_value() && kPath->size() == 3 && (*kPath)[1].member == "children" &&
                    (*kPath)[1].key == "busId" && (*kPath)[1].id == "ea6b" && (*kPath)[2].key.empty());
    RAWFRAME_EXPECT(!dataPathOf("").has_value() && !dataPathOf("a..b").has_value() && !dataPathOf("a[b]").has_value() &&
                    !dataPathOf("a b").has_value());
}
