// SPEC-0026's node library written as Slang (D691 onward): a material
// moving its vertices at the presented World's time, every math node of
// the atomic core, the channel, stream, logic, color, and noise nodes, in
// the fragment and the vertex stage, and what each refuses.

#include "generated/arithmetic_material.h"
#include "generated/facing_graph.h"
#include "generated/facing_material.h"
#include "generated/rising_graph.h"
#include "generated/rising_material.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"
#include "surface_documents.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::material;
using namespace rawframe::material::testing;

namespace {

constexpr graph::NodeId kTime = 0x2f00000000000021ULL;
constexpr graph::NodeId kWorld = 0x2f00000000000022ULL;
constexpr graph::NodeId kAxes = 0x2f00000000000023ULL;
constexpr graph::NodeId kPhase = 0x2f00000000000024ULL;
constexpr graph::NodeId kWave = 0x2f00000000000025ULL;
constexpr graph::NodeId kSlope = 0x2f00000000000026ULL;
constexpr graph::NodeId kLifted = 0x2f00000000000027ULL;
constexpr graph::NodeId kTilted = 0x2f00000000000028ULL;
constexpr graph::NodeId kFacing = 0x2f00000000000029ULL;

graph::Node timeOf(Value timeline) {
    Value params = Value::object();
    if (timeline.kind() != Value::Kind::Null) {
        params.add("timeline", std::move(timeline));
    }
    return nodeOf(kTime, kTimeType, std::move(params), Value::object());
}

graph::Node unaryOf(graph::NodeId id, std::string_view type, Value in) {
    return nodeOf(id, type, Value::object(), objectOf({{"in", std::move(in)}}));
}

graph::Node combineOf(graph::NodeId id, Value x, Value y, Value z) {
    return nodeOf(
        id, kCombine3Type, Value::object(), objectOf({{"x", std::move(x)}, {"y", std::move(y)}, {"z", std::move(z)}}));
}

/// A wave rolling along the World's x: each vertex lifted by sin(x + t),
/// and the surface's normal tilted by the wave's slope, cos(x + t).
graph::Document waved() {
    return surfaceOf(objectOf({{"geometry_normal", from(kFacing, "out")}, {"position_offset", from(kLifted, "out")}}),
                     {timeOf(Value::string("simulation")),
                      nodeOf(kWorld, kWorldPositionType, Value::object(), Value::object()),
                      unaryOf(kAxes, kSeparate3Type, from(kWorld, "position")),
                      mathOf(kPhase, kAddType, from(kAxes, "r"), from(kTime, "seconds")),
                      unaryOf(kWave, kSinType, from(kPhase, "out")),
                      unaryOf(kSlope, kCosType, from(kPhase, "out")),
                      combineOf(kLifted, Value::real(0), from(kWave, "out"), Value::real(0)),
                      combineOf(kTilted, from(kSlope, "out"), Value::real(-1), Value::real(0)),
                      nodeOf(kFacing, kNormalizeType, Value::object(), objectOf({{"in", from(kTilted, "out")}}))});
}

} // namespace

RAWFRAME_TEST(AMaterialMovesItsVerticesAtThePresentedWorldsTime) {
    // SPEC-0026's position offset (D691): the vertex stage's code, at a
    // vertex placed in the World and the World's time, which the fragment
    // stage reads as well; the blob folds none of it.
    const graph::Document kWaved = waved();
    RAWFRAME_EXPECT(validateSurface(kWaved).has_value());
    RAWFRAME_EXPECT(refusedWith(compileQualities(kWaved), MaterialError::Unsupported));
    RAWFRAME_EXPECT(semanticHash(kWaved).has_value());
    const auto kMade = generateSlang(kWaved);
    RAWFRAME_EXPECT(kMade.has_value() && kMade->moves && kMade->textures.empty());
    if (!kMade.has_value()) {
        return;
    }
    const std::string& kSource = kMade->source;
    const std::size_t kOffset = kSource.find("static float3 offset(Placed placed)");
    RAWFRAME_EXPECT(kOffset != std::string::npos);
    const std::string kFragment = kSource.substr(0, kOffset);
    const std::string kVertex = kOffset != std::string::npos ? kSource.substr(kOffset) : std::string{};
    RAWFRAME_EXPECT(kVertex.contains("(placed.position).r + (placed.time.x + placed.time.y)") &&
                    kVertex.contains("sin(") && kVertex.contains("float3(0.0, ") && !kVertex.contains("point.") &&
                    !kVertex.contains("cos("));
    RAWFRAME_EXPECT(kFragment.contains("(point.position).r + (point.time.x + point.time.y)") &&
                    kFragment.contains("cos(") && kFragment.contains("normalize(") && !kFragment.contains("placed.") &&
                    !kFragment.contains("sin("));
    // A material without one moves nothing.
    const auto kStill = generateSlang(sampledAt(0));
    RAWFRAME_EXPECT(kStill.has_value() && !kStill->moves &&
                    kStill->source.contains("static float3 offset(Placed placed)\n    {\n        return float3(0.0);"));
    // The timeline is the simulation's, named; the offset connected; a
    // combine3 of floats; a normalized vec3; no texture in the vertex
    // stage.
    for (const Value& kTimeline : {Value{}, Value::string("frame"), Value::real(1)}) {
        graph::Document wrong = kWaved;
        wrong.nodes[1] = timeOf(kTimeline);
        RAWFRAME_EXPECT(refusedWith(validateSurface(wrong), MaterialError::Invalid));
    }
    // Wrapped at a whole period (D702): its whole seconds wrapped before
    // the fraction left is added, in both stages; a period is whole
    // seconds, from one to below 2^24.
    const auto kPeriodOf = [&kWaved](Value period) {
        graph::Document wrapped = kWaved;
        wrapped.nodes[1] = nodeOf(kTime,
                                  kTimeType,
                                  objectOf({{"period", std::move(period)}, {"timeline", Value::string("simulation")}}),
                                  Value::object());
        return wrapped;
    };
    const auto kWrapped = generateSlang(kPeriodOf(Value::integer(600)));
    RAWFRAME_EXPECT(kWrapped.has_value() && kWrapped->source.contains("(fmod(placed.time.x, 600.0) + placed.time.y)") &&
                    kWrapped->source.contains("(fmod(point.time.x, 600.0) + point.time.y)"));
    for (const Value& kPeriod :
         {Value::integer(0), Value::real(1.5), Value::integer(std::int64_t{1} << 24), Value::string("600")}) {
        RAWFRAME_EXPECT(refusedWith(validateSurface(kPeriodOf(kPeriod)), MaterialError::Invalid));
    }
    RAWFRAME_EXPECT(refusedWith(
        validateSurface(surfaceOf(
            objectOf({{"position_offset", Value::array({Value::real(0), Value::real(1), Value::real(0)})}}), {})),
        MaterialError::Invalid));
    graph::Document wrong = kWaved;
    wrong.nodes[7] = combineOf(kLifted, Value::real(0), from(kWorld, "position"), Value::real(0));
    RAWFRAME_EXPECT(refusedWith(validateSurface(wrong), MaterialError::Invalid));
    wrong = kWaved;
    wrong.nodes[9] = unaryOf(kFacing, kNormalizeType, from(kWave, "out"));
    RAWFRAME_EXPECT(refusedWith(validateSurface(wrong), MaterialError::Invalid));
    wrong = kWaved;
    wrong.nodes[4] = mathOf(kPhase, kAddType, from(kSampler, "alpha"), from(kTime, "seconds"));
    wrong.nodes.insert(wrong.nodes.begin() + 1, samplerOf({}));
    RAWFRAME_EXPECT(validateSurface(wrong).has_value());
    RAWFRAME_EXPECT(refusedWith(generateSlang(wrong), MaterialError::Unsupported));
}

RAWFRAME_TEST(AGraphRisingWithTimeIsWrittenAsTheSlangTheScenesTestsDraw) {
    // rising.material lifts its model a meter a second (D691); the scene's
    // tests draw what it is written as, rising_graph.slang, word for word.
    const std::string_view kDocument{reinterpret_cast<const char*>(kRisingMaterial.data()), kRisingMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kRisingGraph.data()), kRisingGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->moves && kWritten->source == kGolden);
}

RAWFRAME_TEST(EveryMathNodeOfTheCoreIsWrittenAndTyped) {
    // D694: arithmetic.material uses every math node of SPEC-0026's atomic
    // core and both World streams, and the channel nodes and the vertex's
    // color (D695), in the fragment and the vertex stage; the cook's test
    // builds it through the shader toolchain.
    const std::string_view kDocument{reinterpret_cast<const char*>(kArithmeticMaterial.data()),
                                     kArithmeticMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(refusedWith(compileQualities(*kRead), MaterialError::Unsupported));
    const auto kMade = generateSlang(*kRead);
    RAWFRAME_EXPECT(kMade.has_value() && kMade->moves);
    if (!kMade.has_value()) {
        return;
    }
    for (const std::string_view kCode : {"abs(",
                                         "sign(",
                                         "floor(",
                                         "ceil(",
                                         "round(",
                                         "frac(",
                                         "-(",
                                         "1.0 / (",
                                         "sqrt(",
                                         "exp(",
                                         "exp2(",
                                         "log(",
                                         "log2(",
                                         "sin(",
                                         "cos(",
                                         "tan(",
                                         "asin(",
                                         "acos(",
                                         "saturate(",
                                         "1.0 - (",
                                         " / ",
                                         ") * floor((",
                                         "min(",
                                         "max(",
                                         "pow(",
                                         "step(",
                                         "atan2(",
                                         "lerp(",
                                         "clamp(",
                                         "smoothstep(",
                                         ") + ((",
                                         "length(",
                                         "dot(",
                                         "distance(",
                                         "cross(",
                                         "reflect(",
                                         "refract(",
                                         "normalize(",
                                         "point.normal",
                                         "placed.normal",
                                         "min(v",
                                         "float3(1.0))",
                                         "float3(1.0, 2.0, 3.0)",
                                         "float2(",
                                         ").zx",
                                         ").zyx",
                                         ").y",
                                         "point.color.rgb",
                                         "placed.color.a",
                                         "point.view",
                                         "placed.view",
                                         "point.tangent.xyz",
                                         " < ",
                                         " >= ",
                                         " && ",
                                         " || true",
                                         " != ",
                                         "!(",
                                         ") ? (",
                                         "const bool ",
                                         "rawframeRgbToHsv(v",
                                         "rawframeHsvToRgb(v",
                                         "float3(0.2126, 0.7152, 0.0722)",
                                         "rawframeValueNoise2(",
                                         "rawframeSimplex2(",
                                         "rawframeSimplex3(",
                                         "rawframeCell2(int2(floor("}) {
        RAWFRAME_EXPECT(kMade->source.contains(kCode));
        if (!kMade->source.contains(kCode)) {
            std::printf("not written: %s\n", std::string{kCode}.c_str());
        }
    }
    // Each helper a color node calls is written once, before the material
    // (D698).
    for (const std::string_view kHelper : {"float3 rawframeRgbToHsv(float3 c)",
                                           "float3 rawframeHsvToRgb(float3 c)",
                                           "uint rawframeHash(uint x)",
                                           "float rawframeSimplex3(float3 p)"}) {
        const std::size_t kFirst = kMade->source.find(kHelper);
        RAWFRAME_EXPECT(kFirst != std::string::npos && kFirst < kMade->source.find("struct Generated") &&
                        kMade->source.find(kHelper, kFirst + 1) == std::string::npos);
    }
    // The noise kernels' hash before the kernels that call it (D699).
    RAWFRAME_EXPECT(kMade->source.find("uint rawframeHash(uint x)") <
                    kMade->source.find("float rawframeValueNoise2(float2 p)"));
    // Types: a length of a float, a dot of a vec3 and a float, a cross of a
    // float, inputs of two vector types, and a refraction's vector ratio.
    const auto kRefused = [](std::string_view type, std::vector<std::pair<std::string, Value>> inputs) {
        return refusedWith(
            validateSurface(surfaceOf(objectOf({{"specular_roughness", from(kPhase, "out")}}),
                                      {timeOf(Value::string("simulation")),
                                       nodeOf(kWorld, kWorldPositionType, Value::object(), Value::object()),
                                       nodeOf(kPhase, type, Value::object(), objectOf(std::move(inputs)))})),
            MaterialError::Invalid);
    };
    RAWFRAME_EXPECT(kRefused("rawframe/length@1", {{"in", from(kTime, "seconds")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/luminance@1", {{"in", from(kWorld, "position")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/value_noise_2d@1", {{"in", from(kWorld, "position")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/simplex_noise_3d@1", {{"in", from(kTime, "seconds")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/dot@1", {{"a", from(kWorld, "position")}, {"b", from(kTime, "seconds")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/cross@1", {{"a", from(kTime, "seconds")}, {"b", from(kWorld, "position")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/min@1", {{"a", from(kWorld, "position")}, {"b", pair(1, 2)}}));
    RAWFRAME_EXPECT(kRefused(
        "rawframe/refract@1",
        {{"eta", from(kWorld, "position")}, {"in", from(kWorld, "position")}, {"normal", from(kWorld, "position")}}));
    // Logic (D697): an and of floats, a compare of vectors or by no known
    // operator, a select on a float, and a bool in arithmetic.
    RAWFRAME_EXPECT(kRefused("rawframe/and@1", {{"a", from(kTime, "seconds")}, {"b", Value::boolean(true)}}));
    RAWFRAME_EXPECT(kRefused("rawframe/select@1",
                             {{"a", Value::real(1)}, {"b", Value::real(0)}, {"condition", from(kTime, "seconds")}}));
    RAWFRAME_EXPECT(kRefused("rawframe/add@1", {{"a", Value::boolean(true)}, {"b", from(kTime, "seconds")}}));
    const auto kCompared = [](Value operation, Value a) {
        return refusedWith(
            validateSurface(surfaceOf(
                objectOf({{"specular_roughness", from(kWave, "out")}}),
                {timeOf(Value::string("simulation")),
                 nodeOf(kWorld, kWorldPositionType, Value::object(), Value::object()),
                 nodeOf(kPhase,
                        kCompareType,
                        objectOf({{"operator", std::move(operation)}}),
                        objectOf({{"a", std::move(a)}, {"b", Value::real(0)}})),
                 nodeOf(
                     kWave,
                     "rawframe/select@1",
                     Value::object(),
                     objectOf({{"a", Value::real(1)}, {"b", Value::real(0)}, {"condition", from(kPhase, "out")}}))})),
            MaterialError::Invalid);
    };
    RAWFRAME_EXPECT(!kCompared(Value::string("less"), from(kTime, "seconds")));
    RAWFRAME_EXPECT(kCompared(Value::string("less"), from(kWorld, "position")));
    RAWFRAME_EXPECT(kCompared(Value::string("lessish"), from(kTime, "seconds")));
    // A bool feeds no surface input.
    RAWFRAME_EXPECT(refusedWith(
        validateSurface(surfaceOf(objectOf({{"specular_roughness", from(kPhase, "out")}}),
                                  {timeOf(Value::string("simulation")),
                                   nodeOf(kPhase,
                                          kCompareType,
                                          objectOf({{"operator", Value::string("less")}}),
                                          objectOf({{"a", from(kTime, "seconds")}, {"b", Value::real(0)}}))})),
        MaterialError::Invalid));
    // A swizzle past its input's width, of a float, or of no letters.
    const auto kSwizzled = [](Value pattern, Value in) {
        return refusedWith(
            validateSurface(surfaceOf(objectOf({{"specular_roughness", from(kPhase, "out")}}),
                                      {timeOf(Value::string("simulation")),
                                       nodeOf(kWorld, kWorldPositionType, Value::object(), Value::object()),
                                       nodeOf(kAxes,
                                              kCombine2Type,
                                              Value::object(),
                                              objectOf({{"x", from(kTime, "seconds")}, {"y", Value::real(1)}})),
                                       nodeOf(kPhase,
                                              kSwizzleType,
                                              objectOf({{"pattern", std::move(pattern)}}),
                                              objectOf({{"in", std::move(in)}}))})),
            MaterialError::Invalid);
    };
    RAWFRAME_EXPECT(kSwizzled(Value::string("z"), from(kAxes, "out")));
    RAWFRAME_EXPECT(kSwizzled(Value::string("x"), from(kTime, "seconds")));
    RAWFRAME_EXPECT(kSwizzled(Value::string(""), from(kWorld, "position")));
    RAWFRAME_EXPECT(kSwizzled(Value::string("xyzx"), from(kWorld, "position")));
    RAWFRAME_EXPECT(!kSwizzled(Value::string("y"), from(kAxes, "out")));
    // An input left out, or one the node has not.
    RAWFRAME_EXPECT(kRefused("rawframe/clamp@1", {{"in", from(kTime, "seconds")}, {"low", Value::real(0)}}));
    RAWFRAME_EXPECT(kRefused("rawframe/abs@1", {{"a", from(kTime, "seconds")}}));
}

RAWFRAME_TEST(AConstantIsWrittenAsItsTypedValue) {
    // D704: a constant of each type the graph carries in generation 1, its
    // value inline wherever it is read; one of a type yet to come waits for
    // it, and one whose value is not of its type, or not one of the graph's
    // types, is refused.
    const auto kConstant = [](graph::NodeId id, std::string_view type, Value value) {
        return nodeOf(id,
                      kConstantType,
                      objectOf({{"type", Value::string(std::string{type})}, {"value", std::move(value)}}),
                      Value::object());
    };
    const auto kColored = [&kConstant](std::string_view type, Value value) {
        return surfaceOf(objectOf({{"base_color", from(kPhase, "out")}, {"specular_roughness", from(kWave, "out")}}),
                         {kConstant(kPhase, type, std::move(value)), kConstant(kWave, "float", Value::real(0.375))});
    };
    const auto kThree = [](double x, double y, double z) {
        return Value::array({Value::real(x), Value::real(y), Value::real(z)});
    };
    const graph::Document kColor = kColored("color3", kThree(0.25, 0.5, 0.75));
    RAWFRAME_EXPECT(validateSurface(kColor).has_value());
    const auto kMade = generateSlang(kColor);
    RAWFRAME_EXPECT(kMade.has_value() && kMade->source.contains("float3(0.25, 0.5, 0.75)") &&
                    kMade->source.contains("made.roughness = 0.375;"));
    // A bool selecting, a vec2 swizzled.
    const graph::Document kChosen = surfaceOf(
        objectOf({{"specular_roughness", from(kWave, "out")}}),
        {kConstant(kAxes, "vec2", pair(0.125, 0.625)),
         kConstant(kPhase, "bool", Value::boolean(false)),
         nodeOf(kWave,
                "rawframe/select@1",
                Value::object(),
                objectOf({{"a", Value::real(1)}, {"b", from(kSlope, "out")}, {"condition", from(kPhase, "out")}})),
         nodeOf(kSlope,
                kSwizzleType,
                objectOf({{"pattern", Value::string("y")}}),
                objectOf({{"in", from(kAxes, "out")}}))});
    const auto kSelected = generateSlang(kChosen);
    RAWFRAME_EXPECT(kSelected.has_value() && kSelected->source.contains("(false) ? (1.0) : (") &&
                    kSelected->source.contains("(float2(0.125, 0.625)).y"));
    // A vec3 feeds no color; a value not of its type; no such type; a type
    // generation 1 does not carry yet; the type or the value left out.
    RAWFRAME_EXPECT(refusedWith(validateSurface(kColored("vec3", kThree(0.25, 0.5, 0.75))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kColored("color3", pair(0.25, 0.5))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kColored("color3", Value::boolean(true))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kColored("matrix3", kThree(0, 0, 0))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kColored("color4", kThree(0, 0, 0))), MaterialError::Unsupported));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(
                        objectOf({{"specular_roughness", from(kWave, "out")}}),
                        {nodeOf(kWave, kConstantType, objectOf({{"type", Value::string("float")}}), Value::object())})),
                    MaterialError::Invalid));
}

RAWFRAME_TEST(AGraphColoredByItsFaceIsWrittenAsTheSlangTheScenesTestsDraw) {
    // facing.material is green on its front and red on its back (D705);
    // the scene's tests draw what it is written as, facing_graph.slang,
    // word for word. A vertex has no face to read.
    const std::string_view kDocument{reinterpret_cast<const char*>(kFacingMaterial.data()), kFacingMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kFacingGraph.data()), kFacingGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && !kWritten->moves && kWritten->source == kGolden);
    const graph::Document kLiftedByFace = surfaceOf(
        objectOf({{"position_offset", from(kLifted, "out")}}),
        {nodeOf(kPhase, kFrontFacingType, Value::object(), Value::object()),
         nodeOf(kWave,
                "rawframe/select@1",
                Value::object(),
                objectOf({{"a", Value::real(1)}, {"b", Value::real(0)}, {"condition", from(kPhase, "front")}})),
         combineOf(kLifted, Value::real(0), from(kWave, "out"), Value::real(0))});
    RAWFRAME_EXPECT(validateSurface(kLiftedByFace).has_value());
    RAWFRAME_EXPECT(refusedWith(generateSlang(kLiftedByFace), MaterialError::Unsupported));
}
