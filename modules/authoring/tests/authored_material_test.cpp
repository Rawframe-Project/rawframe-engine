// A material instance open for authoring (D721): an override set, changed,
// and unset, each change one entry that undo and redo walk exactly back to
// the bytes before, overrides kept in name order, a batch one entry or
// nothing, the last override not unset, and the instance read whole.

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
