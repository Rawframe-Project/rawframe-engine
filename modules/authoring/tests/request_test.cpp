// Authoring on the wire (SPEC-0040): requests read exactly as declared,
// discovery complete enough to build any call from, and one error record.

#include "rawframe/authoring/request.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

constexpr std::string_view kDoor = "0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1";
constexpr std::string_view kLink = "5f3a0c2d-9e81-4b74-8a1d-000000000002";

std::string request(std::string_view operations, std::string_view batch = "atomic") {
    return R"({"formatVersion": 1, "kind": "authoring.request", "batch": ")" + std::string{batch} +
           R"(", "operations": [)" + std::string{operations} + "]}";
}

std::string operation(std::string_view name, std::string_view inputs) {
    return R"({"operation": ")" + std::string{name} + R"(", "entity": ")" + std::string{kDoor} + "\"" +
           std::string{inputs} + "}";
}

std::string onLink(std::string_view name, std::string_view inputs) {
    return operation(name, R"(, "component": ")" + std::string{kLink} + "\"" + std::string{inputs});
}

} // namespace

RAWFRAME_TEST(ARequestReadsEveryOperation) {
    const std::vector<std::string> kAll = {
        operation("scene.create_entity", R"(, "name": "door", "place": 0)"),
        operation("scene.create_entity", R"(, "name": "door")"),
        operation("scene.destroy_entity", ""),
        operation("scene.rename_entity", R"(, "name": "gate")"),
        operation("scene.move_entity", R"(, "place": 3)"),
        onLink("scene.add_component", ""),
        onLink("scene.remove_component", ""),
        onLink("scene.set_field", R"(, "field": "x", "value": {"real": -1.5})"),
        onLink("scene.set_field", R"(, "field": "n", "value": {"signed": "-9223372036854775808"})"),
        onLink("scene.set_field", R"(, "field": "n", "value": {"unsigned": "18446744073709551615"})"),
        onLink("scene.set_field", R"(, "field": "on", "value": {"truth": true})"),
        onLink("scene.set_field", R"(, "field": "on", "value": null)"),
        onLink("scene.set_reference", R"(, "field": "target", "target": ")" + std::string{kDoor} + "\""),
        onLink("scene.set_reference", R"(, "field": "target", "target": null)"),
        onLink("scene.revert_field", R"(, "field": "target")"),
        onLink("scene.revert_component", ""),
        operation("scene.restore_entity", ""),
    };
    std::string joined;
    for (const std::string& each : kAll) {
        joined += (joined.empty() ? "" : ",") + each;
    }
    const auto kRead = readRequest(request(joined, "halt_remaining"));
    RAWFRAME_EXPECT(kRead.has_value() && kRead->operations.size() == kAll.size() &&
                    kRead->batch == Batch::HaltRemaining && !kRead->expects.has_value());
    RAWFRAME_EXPECT(std::get<CreateEntity>(kRead->operations[0]).place == 0 &&
                    !std::get<CreateEntity>(kRead->operations[1]).place.has_value());
    RAWFRAME_EXPECT(std::get<SetField>(kRead->operations[8]).value.integer == INT64_MIN &&
                    std::get<SetField>(kRead->operations[9]).value.whole == UINT64_MAX &&
                    std::get<SetField>(kRead->operations[11]).value.kind == FieldInput::Kind::Default);
    RAWFRAME_EXPECT(!std::get<SetReference>(kRead->operations[13]).target.has_value());
    RAWFRAME_EXPECT(std::get<RevertField>(kRead->operations[14]).field == "target" &&
                    std::holds_alternative<RevertComponent>(kRead->operations[15]) &&
                    std::holds_alternative<RestoreEntity>(kRead->operations[16]));
    RAWFRAME_EXPECT(
        refusedWith(readRequest(request(onLink("scene.revert_field", ""))), AuthoringError::ValidationFailed));
    const auto kRemark =
        readRequest(request(R"({"operation": "scene.remark_component", "component": ")" + std::string{kLink} + "\"}"));
    RAWFRAME_EXPECT(kRemark.has_value() && std::holds_alternative<RemarkComponent>(kRemark->operations[0]));
    RAWFRAME_EXPECT(
        refusedWith(readRequest(request(onLink("scene.remark_component", ""))), AuthoringError::ValidationFailed));
    const auto kExpecting = readRequest(R"({"formatVersion": 1, "kind": "authoring.request", "batch": "atomic",
        "expects": "sha256:00", "operations": []})");
    RAWFRAME_EXPECT(kExpecting.has_value() && kExpecting->expects == "sha256:00");
}

RAWFRAME_TEST(ARequestOutOfItsFormIsRefused) {
    RAWFRAME_EXPECT(
        refusedWith(readRequest(request(operation("scene.explode", ""))), AuthoringError::UnsupportedOperation));
    for (const std::string& bad : {
             request("", "sometimes"),
             std::string{"{"},
             request(operation("scene.destroy_entity", R"(, "name": "extra")")),
             request(operation("scene.rename_entity", "")),
             request(R"({"operation": "scene.destroy_entity", "entity": "not-a-uuid"})"),
             request(operation("scene.move_entity", R"(, "place": -1)")),
             request(operation("scene.create_entity", R"(, "name": 5)")),
             request(onLink("scene.set_field", R"(, "field": "n", "value": {"signed": 5})")),
             request(onLink("scene.set_field", R"(, "field": "n", "value": {"signed": "05"})")),
             request(onLink("scene.set_field", R"(, "field": "n", "value": {"unsigned": "-1"})")),
             request(onLink("scene.set_field", R"(, "field": "n", "value": {"real": "1"})")),
             request(onLink("scene.set_field", R"(, "field": "n", "value": {"real": 1, "truth": true})")),
             request(onLink("scene.set_reference", R"(, "field": "t", "target": 5)")),
             std::string{R"({"formatVersion": 2, "kind": "authoring.request", "batch": "atomic", "operations": []})"},
         }) {
        RAWFRAME_EXPECT(refusedWith(readRequest(bad), AuthoringError::ValidationFailed));
    }
    // The failing operation's place is said.
    const auto kSecond =
        readRequest(request(operation("scene.destroy_entity", "") + "," + operation("scene.rename_entity", "")));
    bool placed = false;
    for (const auto& field : kSecond.error().context()) {
        placed = placed || (field.key == "index" && field.value == "1");
    }
    RAWFRAME_EXPECT(placed);
}

RAWFRAME_TEST(DiscoveryIsCompleteAndErrorsHaveOneShape) {
    const auto kDiscovery = document::parse(writeDiscovery());
    RAWFRAME_EXPECT(kDiscovery.has_value() && kDiscovery->find("surfaceGeneration")->integer() == kSurfaceGeneration);
    const document::Value& operations = *kDiscovery->find("operations");
    RAWFRAME_EXPECT(operations.items().size() == declarations().size());
    for (const document::Value& each : operations.items()) {
        RAWFRAME_EXPECT(!each.find("inputs")->items().empty() && *each.find("history")->text() == "undoable");
    }
    RAWFRAME_EXPECT(kDiscovery->find("errors")->items().size() == 10);
    // An authoring error and another domain's, in the one record.
    const result::Error kConflict =
        result::fail(result::ErrorClass::Conflict, kAuthoringDomain, code(AuthoringError::Conflict), "taken")
            .error()
            .withContext("operation", "scene.create_entity");
    const document::Value kRecord = errorRecord(kConflict);
    RAWFRAME_EXPECT(*kRecord.find("code")->text() == "conflict" && *kRecord.find("class")->text() == "conflict" &&
                    *kRecord.find("message")->text() == "taken" &&
                    *kRecord.find("details")->find("operation")->text() == "scene.create_entity");
    const result::Error kOther =
        result::fail(result::ErrorClass::NotFound, result::ErrorDomain{base::Bits128{1, 1}}, result::ErrorCode{2}, "x")
            .error();
    RAWFRAME_EXPECT(codeName(kOther) == "internal");
}
