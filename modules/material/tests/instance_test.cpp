// Material instances (D709): an instance document read in its canonical
// form, its overrides made into its base's parameters, typed against the
// base's interface and never repeating a default, and its cooked bytes read
// back as they were written.

#include "generated/tinted_material.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/instance.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"
#include "surface_documents.h"

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::material;
using namespace rawframe::material::testing;

namespace {

/// An instance of `base` overriding `overrides`, as its canonical text.
std::string instanceText(std::string base, Value overrides) {
    return document::write(objectOf({{"formatVersion", Value::integer(1)},
                                     {"kind", Value::string("surface")},
                                     {"base", Value::string(std::move(base))},
                                     {"overrides", std::move(overrides)}}));
}

Value threeOf(double x, double y, double z) {
    return Value::array({Value::real(x), Value::real(y), Value::real(z)});
}

/// tinted.material, whose one input is its tint, green by default.
graph::Document tinted() {
    return *readMaterial(
        std::string_view{reinterpret_cast<const char*>(kTintedMaterial.data()), kTintedMaterial.size()});
}

} // namespace

RAWFRAME_TEST(AnInstanceIsReadInItsCanonicalForm) {
    const std::string kText = instanceText("bases/tinted.rfmaterial", objectOf({{"tint", threeOf(1, 0, 0)}}));
    RAWFRAME_EXPECT(namesBase(kText));
    const auto kRead = readInstance(kText);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->base == "bases/tinted.rfmaterial" &&
                    kRead->overrides.names().size() == 1);
    // A graph is no instance.
    const std::string_view kGraph{reinterpret_cast<const char*>(kTintedMaterial.data()), kTintedMaterial.size()};
    RAWFRAME_EXPECT(!namesBase(kGraph) && !namesBase("not json"));
    // Not canonical; members out of order; another format or kind; a base
    // leaving the sources, absolute, or empty; no overrides, or nine.
    RAWFRAME_EXPECT(refusedWith(readInstance(kText + "\n"), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(readInstance(document::write(objectOf({{"formatVersion", Value::integer(1)},
                                                                       {"base", Value::string("a.rfmaterial")},
                                                                       {"kind", Value::string("surface")},
                                                                       {"overrides", Value::object()}}))),
                                MaterialError::Invalid));
    const auto kRefusedWith = [](Value version, Value kind, Value base, Value overrides) {
        return refusedWith(readInstance(document::write(objectOf({{"formatVersion", std::move(version)},
                                                                  {"kind", std::move(kind)},
                                                                  {"base", std::move(base)},
                                                                  {"overrides", std::move(overrides)}}))),
                           MaterialError::Invalid);
    };
    const Value kTint = objectOf({{"tint", threeOf(1, 0, 0)}});
    RAWFRAME_EXPECT(kRefusedWith(Value::integer(2), Value::string("surface"), Value::string("a.rfmaterial"), kTint));
    RAWFRAME_EXPECT(kRefusedWith(Value::integer(1), Value::string("canvas"), Value::string("a.rfmaterial"), kTint));
    for (const std::string_view kBase : {"../a.rfmaterial", "/a.rfmaterial", "", "a//b.rfmaterial", "a/./b"}) {
        RAWFRAME_EXPECT(
            kRefusedWith(Value::integer(1), Value::string("surface"), Value::string(std::string{kBase}), kTint));
    }
    RAWFRAME_EXPECT(
        kRefusedWith(Value::integer(1), Value::string("surface"), Value::string("a.rfmaterial"), Value::object()));
    std::vector<std::pair<std::string, Value>> nine;
    for (const char kLetter : std::string_view{"abcdefghi"}) {
        nine.emplace_back(std::string{kLetter}, Value::real(1));
    }
    RAWFRAME_EXPECT(kRefusedWith(
        Value::integer(1), Value::string("surface"), Value::string("a.rfmaterial"), objectOf(std::move(nine))));
}

RAWFRAME_TEST(AnInstancesOverridesBecomeItsBasesParameters) {
    const graph::Document kBase = tinted();
    const auto kRed = instanceParameters(kBase, objectOf({{"tint", threeOf(1, 0.25, 0)}}));
    RAWFRAME_EXPECT(kRed.has_value() && (*kRed)[0] == 1 && (*kRed)[1] == 0.25F && (*kRed)[2] == 0 && (*kRed)[3] == 0);
    // An input the base has not; one of another type; its default again; a
    // connection; and a base that is no surface material.
    RAWFRAME_EXPECT(
        refusedWith(instanceParameters(kBase, objectOf({{"shade", threeOf(1, 0, 0)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(instanceParameters(kBase, objectOf({{"tint", Value::real(1)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(instanceParameters(kBase, objectOf({{"tint", threeOf(0, 1, 0)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(instanceParameters(kBase, objectOf({{"tint", from(kNode, "out")}})), MaterialError::Invalid));
    graph::Document notSurface = kBase;
    notSurface.kind = "canvas";
    RAWFRAME_EXPECT(
        refusedWith(instanceParameters(notSurface, objectOf({{"tint", threeOf(1, 0, 0)}})), MaterialError::Invalid));
}

RAWFRAME_TEST(ACookedInstanceReadsBackAsItWasWritten) {
    MaterialInstance made{.base = {.high = 0x0a5cfeecaa5658b2ULL, .low = 0xff7dab1981ef0cefULL}};
    made.parameters[0] = 1;
    made.parameters[31] = -0.5F;
    const std::vector<std::byte> kBytes = encodeInstance(made);
    const auto kRead = decodeInstance(kBytes);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == made);
    // Every prefix, a byte more, another format, no base, a number not
    // finite.
    for (std::size_t length = 0; length < kBytes.size(); ++length) {
        RAWFRAME_EXPECT(refusedWith(decodeInstance(std::span{kBytes}.first(length)), MaterialError::Invalid));
    }
    std::vector<std::byte> changed = kBytes;
    changed.push_back(std::byte{0});
    RAWFRAME_EXPECT(refusedWith(decodeInstance(changed), MaterialError::Invalid));
    changed = kBytes;
    changed[4] = std::byte{2};
    RAWFRAME_EXPECT(refusedWith(decodeInstance(changed), MaterialError::Invalid));
    MaterialInstance bad = made;
    bad.base = {};
    RAWFRAME_EXPECT(refusedWith(decodeInstance(encodeInstance(bad)), MaterialError::Invalid));
    bad = made;
    bad.parameters[7] = std::numeric_limits<float>::quiet_NaN();
    RAWFRAME_EXPECT(refusedWith(decodeInstance(encodeInstance(bad)), MaterialError::Invalid));
}
