// The int type and bool inputs: an int constant and input computed
// with ints alone by the nodes that take them, compared and selected into
// floats; a bool input choosing; both held in the material's vectors as
// floats exactly, and overridden by an instance; and what each refuses.

#include "rawframe/material/errors.h"
#include "rawframe/material/instance.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"
#include "surface_documents.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::material;
using namespace rawframe::material::testing;

namespace {

constexpr graph::NodeId kCount = 0x2f00000000000081ULL;
constexpr graph::NodeId kThree = 0x2f00000000000082ULL;
constexpr graph::NodeId kSum = 0x2f00000000000083ULL;
constexpr graph::NodeId kLeft = 0x2f00000000000084ULL;
constexpr graph::NodeId kMore = 0x2f00000000000085ULL;
constexpr graph::NodeId kWet = 0x2f00000000000086ULL;
constexpr graph::NodeId kRough = 0x2f00000000000087ULL;
constexpr graph::NodeId kShine = 0x2f00000000000088ULL;

Value inputOf(std::string_view type, Value initial) {
    return objectOf({{"default", std::move(initial)}, {"type", Value::string(std::string{type})}});
}

/// The roughness by an int: (count + three) modulo `by`, compared with one,
/// selects 0.25 or 0.75; then a bool input chooses that or nought.
graph::Document counted(Value count = inputOf("int", Value::integer(4)), Value by = Value::integer(3)) {
    graph::Document made = surfaceOf(
        objectOf({{"specular_roughness", from(kShine, "out")}}),
        {nodeOf(kCount, kParameterType, objectOf({{"name", Value::string("count")}}), Value::object()),
         nodeOf(kThree,
                kConstantType,
                objectOf({{"type", Value::string("int")}, {"value", Value::integer(3)}}),
                Value::object()),
         nodeOf(kSum, kAddType, Value::object(), objectOf({{"a", from(kCount, "out")}, {"b", from(kThree, "out")}})),
         nodeOf(
             kLeft, "rawframe/modulo@1", Value::object(), objectOf({{"a", from(kSum, "out")}, {"b", std::move(by)}})),
         nodeOf(kMore,
                kCompareType,
                objectOf({{"operator", Value::string("greater")}}),
                objectOf({{"a", from(kLeft, "out")}, {"b", Value::integer(1)}})),
         nodeOf(kWet, kParameterType, objectOf({{"name", Value::string("wet")}}), Value::object()),
         nodeOf(kRough,
                "rawframe/select@1",
                Value::object(),
                objectOf({{"a", Value::real(0.25)}, {"b", Value::real(0.75)}, {"condition", from(kMore, "out")}})),
         nodeOf(kShine,
                "rawframe/select@1",
                Value::object(),
                objectOf({{"a", from(kRough, "out")}, {"b", Value::real(0)}, {"condition", from(kWet, "out")}}))});
    made.interface =
        objectOf({{"inputs", objectOf({{"count", std::move(count)}, {"wet", inputOf("bool", Value::boolean(true))}})}});
    return made;
}

} // namespace

RAWFRAME_TEST(AnIntIsComputedWithIntsAlone) {
    const graph::Document kMade = counted();
    RAWFRAME_EXPECT(validateSurface(kMade).has_value());
    const auto kWritten = generateSlang(kMade);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    const std::string& kSource = kWritten->source;
    // The input read as an int and the bool as one, from their floats; the
    // sum and the floored remainder ints; the comparison an int's.
    RAWFRAME_EXPECT(kSource.contains("const int v0 = int(materials[blobOf(point.material) + 9u].x) + 3;"));
    RAWFRAME_EXPECT(kSource.contains("const int v1 = (((v0) % (3)) + (3)) % (3);"));
    RAWFRAME_EXPECT(kSource.contains("const bool v2 = v1 > 1;"));
    RAWFRAME_EXPECT(kSource.contains("(materials[blobOf(point.material) + 10u].x != 0.0)"));
    RAWFRAME_EXPECT(kWritten->parameters[0] == 4 && kWritten->parameters[4] == 1);
    // A whole literal beside an int is one; any other float is not.
    RAWFRAME_EXPECT(refusedWith(validateSurface(counted(inputOf("int", Value::integer(4)), Value::real(2.5))),
                                MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(counted(inputOf("int", Value::integer(4)), from(kRough, "out"))),
                                MaterialError::Invalid));
    // A default not whole, or past 2^24, which the blob's float would not
    // hold.
    RAWFRAME_EXPECT(refusedWith(validateSurface(counted(inputOf("int", Value::real(4.5)))), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(counted(inputOf("int", Value::integer(16777217)))), MaterialError::Invalid));
    // A node that takes no int; an int feeding a float's input; a constant
    // not whole.
    graph::Document floored = counted();
    floored.nodes[3] = nodeOf(kLeft, "rawframe/floor@1", Value::object(), objectOf({{"in", from(kSum, "out")}}));
    RAWFRAME_EXPECT(refusedWith(validateSurface(floored), MaterialError::Invalid));
    graph::Document mixed = counted();
    mixed.nodes[6] =
        nodeOf(kRough,
               "rawframe/select@1",
               Value::object(),
               objectOf({{"a", from(kLeft, "out")}, {"b", Value::real(0.75)}, {"condition", from(kMore, "out")}}));
    RAWFRAME_EXPECT(refusedWith(validateSurface(mixed), MaterialError::Invalid));
    graph::Document fractional = counted();
    fractional.nodes[1] = nodeOf(kThree,
                                 kConstantType,
                                 objectOf({{"type", Value::string("int")}, {"value", Value::real(3.5)}}),
                                 Value::object());
    RAWFRAME_EXPECT(refusedWith(validateSurface(fractional), MaterialError::Invalid));
}

RAWFRAME_TEST(AnInstanceOverridesIntAndBoolInputs) {
    const graph::Document kBase = counted();
    const auto kMade = instanceValues(kBase, objectOf({{"count", Value::integer(7)}, {"wet", Value::boolean(false)}}));
    RAWFRAME_EXPECT(kMade.has_value() && kMade->parameters[0] == 7 && kMade->parameters[4] == 0);
    RAWFRAME_EXPECT(
        refusedWith(instanceValues(kBase, objectOf({{"count", Value::real(7.5)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(instanceValues(kBase, objectOf({{"wet", Value::integer(0)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(instanceValues(kBase, objectOf({{"wet", Value::boolean(true)}})), MaterialError::Invalid));
}
