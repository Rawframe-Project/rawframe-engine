// Static switches (D719): declared in the interface, each a static_switch
// node's choice written alone; an instance overriding one draws a variant
// of its own; and what a switch and its node refuse.

#include "rawframe/material/errors.h"
#include "rawframe/material/instance.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"
#include "surface_documents.h"

#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::material;
using namespace rawframe::material::testing;

namespace {

constexpr graph::NodeId kTint = 0x2f000000000000c1ULL;
constexpr graph::NodeId kWet = 0x2f000000000000c2ULL;

Value threeOf(double x, double y, double z) {
    return Value::array({Value::real(x), Value::real(y), Value::real(z)});
}

/// Dry, a sandy literal; wet, the tint input; by the `wet` switch.
graph::Document soaked(Value switches = objectOf({{"wet", objectOf({{"default", Value::boolean(false)}})}}),
                       Value off = threeOf(0.8, 0.6, 0.4),
                       Value name = Value::string("wet")) {
    graph::Document made =
        surfaceOf(objectOf({{"base_color", from(kWet, "out")}}),
                  {nodeOf(kTint, kParameterType, objectOf({{"name", Value::string("tint")}}), Value::object()),
                   nodeOf(kWet,
                          kStaticSwitchType,
                          objectOf({{"name", std::move(name)}}),
                          objectOf({{"off", std::move(off)}, {"on", from(kTint, "out")}}))});
    made.interface = objectOf(
        {{"inputs",
          objectOf({{"tint", objectOf({{"default", threeOf(0.2, 0.2, 0.25)}, {"type", Value::string("color3")}})}})},
         {"switches", std::move(switches)}});
    return made;
}

} // namespace

RAWFRAME_TEST(AStaticSwitchWritesTheBranchItTakes) {
    const graph::Document kDry = soaked();
    RAWFRAME_EXPECT(validateSurface(kDry).has_value());
    RAWFRAME_EXPECT(refusedWith(compile(kDry), MaterialError::Unsupported));
    const auto kWritten = generateSlang(kDry);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source.contains("(float3(0.8, 0.6, 0.4))") &&
                    !kWritten->source.contains("materials[blobOf("));
    // An instance turning it on draws its own variant: the tint read.
    const auto kVariant = switchedVariant(kDry, objectOf({{"tint", threeOf(1, 0, 0)}, {"wet", Value::boolean(true)}}));
    RAWFRAME_EXPECT(kVariant.has_value() && kVariant->has_value());
    if (kVariant.has_value() && kVariant->has_value()) {
        const auto kSoaked = generateSlang(**kVariant);
        RAWFRAME_EXPECT(kSoaked.has_value() && kSoaked->source.contains("materials[blobOf(point.material) + 9u].xyz") &&
                        !kSoaked->source.contains("float3(0.8, 0.6, 0.4)"));
        const auto kVariantHash = semanticHash(**kVariant);
        const auto kBaseHash = semanticHash(kDry);
        RAWFRAME_EXPECT(kVariantHash.has_value() && kBaseHash.has_value() && *kVariantHash != *kBaseHash);
    }
    // Its values take the inputs alone, and share the base's program where
    // no switch is overridden.
    const auto kValues = instanceValues(kDry, objectOf({{"tint", threeOf(1, 0, 0)}, {"wet", Value::boolean(true)}}));
    RAWFRAME_EXPECT(kValues.has_value() && kValues->parameters[0] == 1 && kValues->parameters[1] == 0);
    const auto kShared = switchedVariant(kDry, objectOf({{"tint", threeOf(1, 0, 0)}}));
    RAWFRAME_EXPECT(kShared.has_value() && !kShared->has_value());
    // An override repeating the default, or not a bool.
    RAWFRAME_EXPECT(
        refusedWith(switchedVariant(kDry, objectOf({{"wet", Value::boolean(false)}})), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(switchedVariant(kDry, objectOf({{"wet", Value::integer(1)}})), MaterialError::Invalid));
}

RAWFRAME_TEST(AStaticSwitchIsDeclaredAndTyped) {
    const auto kInvalid = [](const graph::Document& surface) {
        return refusedWith(validateSurface(surface), MaterialError::Invalid);
    };
    // A default not a bool, or more than one member; a name not in lower
    // case; a node naming no declared switch; branches of two types.
    RAWFRAME_EXPECT(kInvalid(soaked(objectOf({{"wet", objectOf({{"default", Value::real(1)}})}}))));
    RAWFRAME_EXPECT(kInvalid(
        soaked(objectOf({{"wet", objectOf({{"default", Value::boolean(true)}, {"label", Value::string("Wet")}})}}))));
    RAWFRAME_EXPECT(kInvalid(soaked(objectOf({{"Wet", objectOf({{"default", Value::boolean(true)}})}}))));
    RAWFRAME_EXPECT(kInvalid(soaked(objectOf({{"wet", objectOf({{"default", Value::boolean(false)}})}}),
                                    threeOf(0.8, 0.6, 0.4),
                                    Value::string("dry"))));
    RAWFRAME_EXPECT(
        kInvalid(soaked(objectOf({{"wet", objectOf({{"default", Value::boolean(false)}})}}), Value::real(1))));
    // An interface's members other than its inputs and its switches.
    graph::Document stray = soaked();
    stray.interface.add("variants", Value::object());
    RAWFRAME_EXPECT(kInvalid(stray));
}
