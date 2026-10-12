// The inline-Slang node (D718): a function body with typed inputs and
// outputs written once a material and called once a node, in the stages
// it declares; what its declarations and its body may not say; and an
// untrusted package's material refused for holding one.

#include "rawframe/material/errors.h"
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

constexpr graph::NodeId kPlace = 0x2f000000000000b1ULL;
constexpr graph::NodeId kBanded = 0x2f000000000000b2ULL;

Value namesOf(std::vector<std::string> names) {
    std::vector<Value> made;
    for (std::string& name : names) {
        made.push_back(Value::string(std::move(name)));
    }
    return Value::array(std::move(made));
}

/// Bands across the World's height: a color and a roughness from the
/// point's place and a band count, written by an inline node.
graph::Node bandsOf(std::string body,
                    Value stages = namesOf({"fragment"}),
                    Value domains = namesOf({"surface"}),
                    Value capabilities = Value::array({}),
                    Value count = Value::real(4)) {
    return nodeOf(
        kBanded,
        kInlineSlangType,
        objectOf({{"body", Value::string(std::move(body))},
                  {"capabilities", std::move(capabilities)},
                  {"domains", std::move(domains)},
                  {"inputs", objectOf({{"count", Value::string("int")}, {"place", Value::string("vec3")}})},
                  {"outputs", objectOf({{"rough", Value::string("float")}, {"shade", Value::string("color3")}})},
                  {"stages", std::move(stages)}}),
        objectOf({{"count", std::move(count)}, {"place", from(kPlace, "position")}}));
}

constexpr std::string_view kBody = "    const float band = frac(place.y * float(count));\n"
                                   "    shade = float3(band, 0.5, 1.0 - band);\n"
                                   "    rough = band;";

graph::Document banded(graph::Node bands = bandsOf(std::string{kBody})) {
    return surfaceOf(objectOf({{"base_color", from(kBanded, "shade")}, {"specular_roughness", from(kBanded, "rough")}}),
                     {nodeOf(kPlace, kWorldPositionType, Value::object(), Value::object()), std::move(bands)});
}

} // namespace

RAWFRAME_TEST(AnInlineNodeIsWrittenAsItsFunctionAndCalledOnce) {
    const graph::Document kMade = banded();
    RAWFRAME_EXPECT(validateSurface(kMade).has_value());
    RAWFRAME_EXPECT(refusedWith(compile(kMade), MaterialError::Unsupported));
    const auto kWritten = generateSlang(kMade);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    const std::string& kSource = kWritten->source;
    RAWFRAME_EXPECT(kSource.contains("void rawframeInline2f000000000000b2(in int count, in float3 place, out float "
                                     "rough, out float3 shade)\n{\n    const float band"));
    RAWFRAME_EXPECT(
        kSource.contains("        float v0_rough;\n        float3 v0_shade;\n"
                         "        rawframeInline2f000000000000b2(4, point.position, v0_rough, v0_shade);\n"));
    RAWFRAME_EXPECT(kSource.contains("made.roughness = v0_rough;") && kSource.contains("(v0_shade)"));
    std::size_t calls = 0;
    for (std::size_t at = kSource.find("rawframeInline2f000000000000b2("); at != std::string::npos;
         at = kSource.find("rawframeInline2f000000000000b2(", at + 1)) {
        ++calls;
    }
    RAWFRAME_EXPECT(calls == 2);
    // Trusted, it is admitted; untrusted, refused; a material without one
    // is admitted either way.
    RAWFRAME_EXPECT(admitted(kMade, Trust::Trusted).has_value());
    RAWFRAME_EXPECT(refusedWith(admitted(kMade, Trust::Untrusted), MaterialError::Untrusted));
    RAWFRAME_EXPECT(admitted(surfaceOf(Value::object(), {}), Trust::Untrusted).has_value());
}

RAWFRAME_TEST(AnInlineNodeSaysWhatItIsAndNoMore) {
    const auto kInvalid = [](graph::Node bands) {
        return refusedWith(validateSurface(banded(std::move(bands))), MaterialError::Invalid);
    };
    // A preprocessor line, an import, a string, a brace closed early or
    // left open, a double underscore, and nothing.
    RAWFRAME_EXPECT(kInvalid(bandsOf("#define X 1\n" + std::string{kBody})));
    RAWFRAME_EXPECT(kInvalid(bandsOf("import other;\n" + std::string{kBody})));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody} + "\n    const float s = \"\";")));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody} + "\n}\nvoid escaped()\n{")));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody} + "\n    {")));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody} + "\n    float __x = 0.0;")));
    RAWFRAME_EXPECT(kInvalid(bandsOf("")));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string(4097, ' '))));
    // No stage, an unknown one, or out of order; no surface among its
    // domains; an input not of its type.
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody}, Value::array({}))));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody}, namesOf({"geometry"}))));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody}, namesOf({"vertex", "fragment"}))));
    RAWFRAME_EXPECT(kInvalid(bandsOf(std::string{kBody}, namesOf({"fragment"}), namesOf({"canvas"}))));
    RAWFRAME_EXPECT(kInvalid(
        bandsOf(std::string{kBody}, namesOf({"fragment"}), namesOf({"surface"}), Value::array({}), Value::real(4.5))));
    // A capability, which generation 1 defines none of.
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(banded(
                        bandsOf(std::string{kBody}, namesOf({"fragment"}), namesOf({"surface"}), namesOf({"wave"})))),
                    MaterialError::Unsupported));
    // A node lifting vertices: written in the vertex stage where it
    // declares it, refused where it declares the fragment's alone.
    const auto kLifted = [](Value stages) {
        return surfaceOf(objectOf({{"position_offset", from(kBanded, "lift")}}),
                         {nodeOf(kBanded,
                                 kInlineSlangType,
                                 objectOf({{"body", Value::string("    lift = float3(0.0, 1.0, 0.0);")},
                                           {"capabilities", Value::array({})},
                                           {"domains", namesOf({"surface"})},
                                           {"inputs", Value::object()},
                                           {"outputs", objectOf({{"lift", Value::string("vec3")}})},
                                           {"stages", std::move(stages)}}),
                                 Value::object())});
    };
    const auto kPlaced = generateSlang(kLifted(namesOf({"fragment", "vertex"})));
    RAWFRAME_EXPECT(kPlaced.has_value() && kPlaced->moves &&
                    kPlaced->source.contains("void rawframeInline2f000000000000b2(out float3 lift)") &&
                    kPlaced->source.contains("return v0_lift;"));
    RAWFRAME_EXPECT(refusedWith(generateSlang(kLifted(namesOf({"fragment"}))), MaterialError::Invalid));
}
