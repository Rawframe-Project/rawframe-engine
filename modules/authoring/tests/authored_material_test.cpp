// A material instance open for authoring (D721): an override set, changed,
// and unset, each change one entry that undo and redo walk exactly back to
// the bytes before, overrides kept in name order, a batch one entry or
// nothing, the last override not unset, and the instance read whole. A
// material graph open for authoring (D728): a node's input and param set
// and unset, a node made and removed, one another node reads refused, the
// graph in its canonical form after each, and each kind refusing the
// other's operations.

#include "rawframe/authoring/authored_material.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/test/test.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

using namespace rawframe;
using namespace rawframe::authoring;
using document::Value;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

const base::Bits128 kDocument{7, 7};

Value threeOf(double x, double y, double z) {
    return Value::array({Value::real(x), Value::real(y), Value::real(z)});
}

/// An instance of the tinted base, red, as its canonical text.
std::string redText() {
    Value overrides = Value::object();
    overrides.add("tint", threeOf(1, 0, 0));
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("surface"));
    made.add("base", Value::string("bases/tinted.rfmaterial"));
    made.add("overrides", std::move(overrides));
    return document::write(made);
}

result::Result<Committed>
set(AuthoredMaterial& material, std::uint64_t generation, std::string_view input, std::optional<Value> value) {
    const std::array<Operation, 1> kOne = {SetMaterialOverride{.input = std::string{input}, .value = std::move(value)}};
    return material.change(generation, kOne);
}

/// tinted.material (D708): its tint parameter, node 41, as its surface's
/// base color, node 42.
constexpr std::string_view kTinted = R"({
  "formatVersion": 1,
  "kind": "surface",
  "interface": {
    "inputs": {
      "tint": {
        "default": [
          0,
          1,
          0
        ],
        "type": "color3"
      }
    }
  },
  "graph": {
    "2f00000000000041": {
      "type": "rawframe/parameter@1",
      "params": {
        "name": "tint"
      },
      "inputs": {}
    },
    "2f00000000000042": {
      "type": "rawframe/surface@1",
      "params": {},
      "inputs": {
        "base_color": {
          "node": "2f00000000000041",
          "output": "out"
        }
      }
    }
  },
  "states": {
    "shading": "unlit"
  }
}
)";
constexpr std::uint64_t kParameter = 0x2f00000000000041;
constexpr std::uint64_t kSurface = 0x2f00000000000042;

} // namespace

RAWFRAME_TEST(AMaterialInstancesOverridesChangeAndUndoExactly) {
    const std::string kRed = redText();
    auto opened = AuthoredMaterial::open(kDocument, kRed);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredMaterial& material = **opened;
    RAWFRAME_EXPECT(material.text() == kRed && material.generation() == 0 &&
                    material.base() == "bases/tinted.rfmaterial");
    // An override changed, then two more made: they stand in name order,
    // which is the canonical form an instance reads in.
    RAWFRAME_EXPECT(set(material, 0, "tint", threeOf(0, 0, 1)).has_value());
    const std::string kBlue = material.text();
    RAWFRAME_EXPECT(set(material, 1, "wet", Value::boolean(true)).has_value());
    RAWFRAME_EXPECT(set(material, 2, "gloss", Value::real(0.25)).has_value());
    const std::string kThree = material.text();
    RAWFRAME_EXPECT(kThree.find("\"gloss\"") < kThree.find("\"tint\"") &&
                    kThree.find("\"tint\"") < kThree.find("\"wet\""));
    RAWFRAME_EXPECT(material.summaryAt(2) == "set gloss");
    // Unset, and the same value again is no entry.
    RAWFRAME_EXPECT(set(material, 3, "wet", std::nullopt).has_value() && material.summaryAt(3) == "unset wet");
    const auto kSame = set(material, 4, "gloss", Value::real(0.25));
    RAWFRAME_EXPECT(kSame.has_value() && kSame->deltas == 0 && material.undoable() == 4);
    // Back to the start exactly, then forward.
    for (std::uint64_t back = 4; back > 0; --back) {
        RAWFRAME_EXPECT(material.undo(material.generation()).has_value());
    }
    RAWFRAME_EXPECT(material.text() == kRed && material.undoable() == 0);
    RAWFRAME_EXPECT(material.redo(material.generation()).has_value() && material.text() == kBlue);
    // The instance read whole.
    const auto kRead = answer(material, ReadMaterialInstance{});
    RAWFRAME_EXPECT(kRead.has_value() && kRead->find("instance") != nullptr &&
                    document::write(*kRead->find("instance")) == kBlue);
    RAWFRAME_EXPECT(refusedWith(answer(material, ReadSheet{}), AuthoringError::ValidationFailed));
}

RAWFRAME_TEST(AMaterialInstancesBatchIsOneEntryOrNothing) {
    const std::string kRed = redText();
    auto opened = AuthoredMaterial::open(kDocument, kRed);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredMaterial& material = **opened;
    // The tint unset and another made: one entry, the instance read after.
    const std::array<Operation, 2> kSwap = {SetMaterialOverride{.input = "tint", .value = std::nullopt},
                                            SetMaterialOverride{.input = "gloss", .value = Value::real(1)}};
    RAWFRAME_EXPECT(material.change(0, kSwap).has_value() && material.undoable() == 1);
    RAWFRAME_EXPECT(material.summaryAt(0) == "unset tint and 1 more");
    RAWFRAME_EXPECT(material.undo(1).has_value() && material.text() == kRed);
    // Refused, nothing kept: the last override unset, an input not a Kest
    // name, a style sheet's operation, a stale generation.
    RAWFRAME_EXPECT(refusedWith(set(material, 2, "tint", std::nullopt), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(set(material, 2, "Tint", Value::real(1)), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(set(material, 2, "", Value::real(1)), AuthoringError::ValidationFailed));
    const std::array<Operation, 1> kStyles = {RemoveStyleClass{.style = 1}};
    RAWFRAME_EXPECT(refusedWith(material.change(2, kStyles), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(set(material, 0, "gloss", Value::real(1)), AuthoringError::TargetStale));
    RAWFRAME_EXPECT(material.text() == kRed);
    // A graph, or any text an instance does not read, does not open.
    RAWFRAME_EXPECT(refusedWith(AuthoredMaterial::open(kDocument, "{}"), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(AuthoredMaterial::open(kDocument, kRed + "\n"), AuthoringError::ValidationFailed));
}

RAWFRAME_TEST(AMaterialGraphsNodesChangeAndUndoExactly) {
    auto opened = AuthoredMaterial::open(kDocument, kTinted);
    RAWFRAME_EXPECT(opened.has_value());
    if (!opened.has_value()) {
        return;
    }
    AuthoredMaterial& material = **opened;
    RAWFRAME_EXPECT(material.graph() && material.text() == kTinted && material.base().empty());
    // The surface's roughness given, then its specular color: each in name
    // order among its inputs, the graph canonical after each.
    const std::array<Operation, 1> kRough = {
        SetMaterialNodeInput{.node = kSurface, .input = "specular_roughness", .value = Value::real(0.25)}};
    RAWFRAME_EXPECT(material.change(0, kRough).has_value() &&
                    material.summaryAt(0) == "set 2f000000.specular_roughness");
    const std::string kRoughText = material.text();
    RAWFRAME_EXPECT(kRoughText.find("\"base_color\"") < kRoughText.find("\"specular_roughness\""));
    // A node made, unread, and removed; one the surface reads is refused.
    constexpr std::uint64_t kUv = 0x2f00000000000043;
    const std::array<Operation, 1> kMade = {CreateMaterialNode{.node = kUv, .type = "rawframe/uv@1"}};
    const auto kCreated = material.change(1, kMade);
    RAWFRAME_EXPECT(kCreated.has_value() && material.summaryAt(1) == "create node rawframe/uv@1");
    const std::array<Operation, 1> kGone = {RemoveMaterialNode{.node = kParameter}};
    RAWFRAME_EXPECT(refusedWith(material.change(2, kGone), AuthoringError::ValidationFailed));
    const std::array<Operation, 1> kUvGone = {RemoveMaterialNode{.node = kUv}};
    RAWFRAME_EXPECT(material.change(2, kUvGone).has_value() && material.text() == kRoughText);
    RAWFRAME_EXPECT(material.summaryAt(2) == "remove node rawframe/uv@1");
    // The base color unset, then connected to the parameter again by a
    // connection's value: the bytes it had.
    const std::array<Operation, 1> kUnset = {
        SetMaterialNodeInput{.node = kSurface, .input = "base_color", .value = std::nullopt}};
    Value connection = Value::object();
    connection.add("node", Value::string("2f00000000000041"));
    connection.add("output", Value::string("out"));
    const std::array<Operation, 1> kConnected = {
        SetMaterialNodeInput{.node = kSurface, .input = "base_color", .value = connection}};
    RAWFRAME_EXPECT(material.change(3, kUnset).has_value() && material.summaryAt(3) == "unset 2f000000.base_color");
    RAWFRAME_EXPECT(material.change(4, kConnected).has_value() && material.text() == kRoughText);
    // A param unset that the parameter needs: refused.
    const std::array<Operation, 1> kUnnamed = {
        SetMaterialNodeParam{.node = kParameter, .param = "name", .value = std::nullopt}};
    RAWFRAME_EXPECT(refusedWith(material.change(5, kUnnamed), AuthoringError::ValidationFailed));
    // Back to the start exactly.
    for (int back = 0; back < 5; ++back) {
        RAWFRAME_EXPECT(material.undo(material.generation()).has_value());
    }
    RAWFRAME_EXPECT(material.text() == kTinted);
    // An unknown node, a taken or nought identity, the other kind's
    // operation, and the other kind's query: refused.
    const std::array<Operation, 1> kNowhere = {
        SetMaterialNodeInput{.node = 7, .input = "base_color", .value = std::nullopt}};
    const std::array<Operation, 1> kTaken = {CreateMaterialNode{.node = kSurface, .type = "rawframe/uv@1"}};
    const std::array<Operation, 1> kOverride = {SetMaterialOverride{.input = "tint", .value = Value::real(1)}};
    RAWFRAME_EXPECT(refusedWith(material.change(10, kNowhere), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(material.change(10, kTaken), AuthoringError::Conflict));
    RAWFRAME_EXPECT(refusedWith(material.change(10, kOverride), AuthoringError::ValidationFailed));
    const auto kRead = answer(material, ReadMaterialGraph{});
    RAWFRAME_EXPECT(kRead.has_value() && document::write(*kRead->find("graph")) == kTinted);
    RAWFRAME_EXPECT(refusedWith(answer(material, ReadMaterialInstance{}), AuthoringError::ValidationFailed));
    auto instance = AuthoredMaterial::open(kDocument, redText());
    RAWFRAME_EXPECT(instance.has_value());
    if (instance.has_value()) {
        const std::array<Operation, 1> kNode = {RemoveMaterialNode{.node = kSurface}};
        RAWFRAME_EXPECT(refusedWith((*instance)->change(0, kNode), AuthoringError::ValidationFailed));
        RAWFRAME_EXPECT(refusedWith(answer(**instance, ReadMaterialGraph{}), AuthoringError::ValidationFailed));
    }
}
