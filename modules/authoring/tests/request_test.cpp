// Authoring on the wire: requests read exactly as declared, a
// style sheet's operation among them, discovery complete enough to
// build any call from, and one error record.

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
        R"({"operation": "scene.add_instance", "scene": "8d1a709be7c6f665a9d32c2c07e122c3", "instance": ")" +
            std::string{kDoor} + "\"}",
        operation("scene.remove_instance", ""),
        R"({"operation": "styles.set_part", "style": "85ecf7b643b63d65", "variant": "hovered", "part": "fill",
            "value": "#304860e0"})",
        R"({"operation": "styles.set_part", "style": "85ecf7b643b63d65", "variant": "focused",
            "part": "outerShadow", "value": null})",
        R"({"operation": "styles.create_class", "style": "00000000000000a1", "name": "tip", "place": 0})",
        R"({"operation": "styles.remove_class", "style": "85ecf7b643b63d65"})",
        R"({"operation": "styles.rename_class", "style": "85ecf7b643b63d65", "name": "knob"})",
        R"({"operation": "styles.set_transitions", "style": "85ecf7b643b63d65", "value": null})",
        R"({"operation": "ui.set_field", "node": "game.hud", "field": "fill", "value": "#101820c0"})",
        R"({"operation": "ui.create_node", "node": "game.coin", "parent": "game.hud", "place": 0})",
        R"({"operation": "ui.create_node", "node": "game.menu"})",
        R"({"operation": "ui.remove_node", "node": "game.hud"})",
        R"({"operation": "ui.move_node", "node": "game.coin", "parent": null, "place": 1})",
        R"({"operation": "materials.set_override", "input": "tint", "value": [1, 0, 0]})",
        R"({"operation": "materials.set_override", "input": "wet", "value": null})",
        R"({"operation": "materials.set_node_input", "node": "2f00000000000042", "input": "base_color",
            "value": {"node": "2f00000000000041", "output": "out"}})",
        R"({"operation": "materials.set_node_param", "node": "2f00000000000041", "param": "name", "value": null})",
        R"({"operation": "materials.create_node", "node": "2f00000000000043", "type": "rawframe/uv@1"})",
        R"({"operation": "materials.remove_node", "node": "2f00000000000043"})",
        R"({"operation": "materials.set_interface_input", "input": "gloss",
            "value": {"default": 0.5, "type": "float"}})",
        R"({"operation": "materials.set_interface_switch", "switch": "wet", "value": null})",
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
    RAWFRAME_EXPECT(std::get<AddInstance>(kRead->operations[17]).scene ==
                        base::parseBits128Hex("8d1a709be7c6f665a9d32c2c07e122c3").value &&
                    std::holds_alternative<RemoveInstance>(kRead->operations[18]));
    // A style sheet's part: its value as the document holds it, or
    // null to unset; the class by 16 hex digits.
    const auto& kHovered = std::get<SetStylePart>(kRead->operations[19]);
    RAWFRAME_EXPECT(kHovered.style == 0x85ecf7b643b63d65 && kHovered.variant == "hovered" && kHovered.part == "fill" &&
                    kHovered.value.has_value() && *kHovered.value->text() == "#304860e0");
    RAWFRAME_EXPECT(!std::get<SetStylePart>(kRead->operations[20]).value.has_value());
    const auto& kCreated = std::get<CreateStyleClass>(kRead->operations[21]);
    RAWFRAME_EXPECT(kCreated.style == 0xa1 && kCreated.name == "tip" && kCreated.place == 0);
    RAWFRAME_EXPECT(std::get<RemoveStyleClass>(kRead->operations[22]).style == 0x85ecf7b643b63d65 &&
                    std::get<RenameStyleClass>(kRead->operations[23]).name == "knob" &&
                    !std::get<SetStyleTransitions>(kRead->operations[24]).value.has_value());
    // A UI document's, its node by its component's name.
    RAWFRAME_EXPECT(std::get<SetUiField>(kRead->operations[25]).field == "fill" &&
                    std::get<CreateUiNode>(kRead->operations[26]).parent == "game.hud" &&
                    std::get<CreateUiNode>(kRead->operations[26]).place == 0 &&
                    !std::get<CreateUiNode>(kRead->operations[27]).parent.has_value() &&
                    std::get<RemoveUiNode>(kRead->operations[28]).node == "game.hud" &&
                    !std::get<MoveUiNode>(kRead->operations[29]).parent.has_value() &&
                    std::get<MoveUiNode>(kRead->operations[29]).place == 1);
    // A material instance's: its value as the instance holds it,
    // or null to unset.
    const auto& kTint = std::get<SetMaterialOverride>(kRead->operations[30]);
    RAWFRAME_EXPECT(kTint.input == "tint" && kTint.value.has_value() && kTint.value->items().size() == 3 &&
                    !std::get<SetMaterialOverride>(kRead->operations[31]).value.has_value());
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "materials.set_override", "input": "tint"})")),
                                AuthoringError::ValidationFailed));
    // A material graph's: its node by 16 hex digits.
    const auto& kInput = std::get<SetMaterialNodeInput>(kRead->operations[32]);
    RAWFRAME_EXPECT(kInput.node == 0x2f00000000000042 && kInput.input == "base_color" && kInput.value.has_value() &&
                    kInput.value->find("output") != nullptr);
    RAWFRAME_EXPECT(!std::get<SetMaterialNodeParam>(kRead->operations[33]).value.has_value() &&
                    std::get<CreateMaterialNode>(kRead->operations[34]).type == "rawframe/uv@1" &&
                    std::get<RemoveMaterialNode>(kRead->operations[35]).node == 0x2f00000000000043);
    // A material graph's interface.
    RAWFRAME_EXPECT(std::get<SetMaterialInterfaceInput>(kRead->operations[36]).input == "gloss" &&
                    std::get<SetMaterialInterfaceInput>(kRead->operations[36]).value.has_value() &&
                    std::get<SetMaterialSwitch>(kRead->operations[37]).name == "wet" &&
                    !std::get<SetMaterialSwitch>(kRead->operations[37]).value.has_value());
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "materials.remove_node", "node": "2f0000"})")),
                                AuthoringError::ValidationFailed));
    // A node's place on the canvas: two finite numbers.
    const auto kPlaced = readRequest(
        request(R"({"operation": "materials.place_node", "node": "2f00000000000043", "x": 12, "y": -3.5})"));
    RAWFRAME_EXPECT(kPlaced.has_value() &&
                    std::get<PlaceMaterialNode>(kPlaced->operations[0]).node == 0x2f00000000000043 &&
                    std::get<PlaceMaterialNode>(kPlaced->operations[0]).y == -3.5);
    RAWFRAME_EXPECT(refusedWith(
        readRequest(request(R"({"operation": "materials.place_node", "node": "2f00000000000043", "x": "12", "y": 0})")),
        AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "ui.create_node", "node": "game.coin",
        "parent": 3})")),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "styles.create_class",
        "style": "00000000000000a1"})")),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "styles.set_part", "style": "85ecf7b6",
        "variant": "base", "part": "fill", "value": null})")),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readRequest(request(R"({"operation": "styles.set_part", "style": "85ecf7b643b63d65",
        "variant": "base", "part": "fill"})")),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(readRequest(request(R"({"operation": "scene.add_instance", "scene": ")" + std::string{kDoor} +
                                        R"(", "instance": ")" + std::string{kDoor} + "\"}")),
                    AuthoringError::ValidationFailed));
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
        const bool kReads = *each.find("history")->text() == "read_only";
        RAWFRAME_EXPECT(kReads ||
                        (!each.find("inputs")->items().empty() && *each.find("history")->text() == "undoable"));
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
