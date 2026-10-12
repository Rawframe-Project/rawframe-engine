// The engine's functions (D711): each standard subgraph reads and checks,
// a surface calls them, each call written in the material's code with its
// inputs given, defaulted, or streamed, and what a function document or a
// call may not say is refused.

#include "rawframe/graph/graph.h"
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

constexpr graph::NodeId kChecker = 0x2f00000000000051ULL;
constexpr graph::NodeId kTurned = 0x2f00000000000052ULL;
constexpr graph::NodeId kFresnel = 0x2f00000000000053ULL;
constexpr graph::NodeId kShine = 0x2f00000000000054ULL;

/// A surface whose base color is a checkerboard over coordinates turned a
/// quarter, and whose roughness falls with Schlick's reflectance.
graph::Document called(Value checker, Value turned = objectOf({{"angle", Value::real(1.5707964)}})) {
    return surfaceOf(
        objectOf({{"base_color", from(kChecker, "out")}, {"specular_roughness", from(kShine, "out")}}),
        {nodeOf(kChecker, "rawframe/checkerboard@1", Value::object(), std::move(checker)),
         nodeOf(kTurned, "rawframe/rotate_uv@1", Value::object(), std::move(turned)),
         nodeOf(kFresnel, "rawframe/fresnel@1", Value::object(), Value::object()),
         nodeOf(kShine, "rawframe/one_minus@1", Value::object(), objectOf({{"in", from(kFresnel, "out")}}))});
}

} // namespace

RAWFRAME_TEST(ASurfaceCallsTheEnginesFunctions) {
    const graph::Document kMade =
        called(objectOf({{"color_b", Value::array({Value::real(0.2), Value::real(0.4), Value::real(0.6)})},
                         {"tiling", pair(4, 4)},
                         {"uv", from(kTurned, "out")}}));
    RAWFRAME_EXPECT(validateSurface(kMade).has_value());
    RAWFRAME_EXPECT(refusedWith(compileQualities(kMade), MaterialError::Unsupported));
    RAWFRAME_EXPECT(semanticHash(kMade).has_value());
    const auto kWritten = generateSlang(kMade);
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    const std::string& kSource = kWritten->source;
    // The checkerboard's colors given and defaulted, its tiling given, its
    // coordinates the turned ones; the turn's center defaulted and its
    // coordinates the mesh's; the reflectance's normal and view the
    // point's own.
    RAWFRAME_EXPECT(kSource.contains("(float3(0.2, 0.4, 0.6))") && kSource.contains("(float3(1.0, 1.0, 1.0))") &&
                    kSource.contains("(float2(4.0, 4.0))") && kSource.contains("(float2(0.5, 0.5))") &&
                    kSource.contains("(point.uv)") && kSource.contains("(point.normal)") &&
                    kSource.contains("(point.view)") && kSource.contains("(1.5707964)") && kSource.contains("(1.5)"));
    RAWFRAME_EXPECT(kSource.contains("floor(") && kSource.contains("cos(") && kSource.contains("pow(") &&
                    kSource.contains("saturate("));
    // Each function written once a call, though its output is read twice.
    const graph::Document kTwice =
        surfaceOf(objectOf({{"base_color", from(kShine, "out")}}),
                  {nodeOf(kChecker, "rawframe/checkerboard@1", Value::object(), Value::object()),
                   nodeOf(kShine,
                          "rawframe/multiply@1",
                          Value::object(),
                          objectOf({{"a", from(kChecker, "out")}, {"b", from(kChecker, "out")}}))});
    const auto kOnce = generateSlang(kTwice);
    RAWFRAME_EXPECT(kOnce.has_value());
    if (kOnce.has_value()) {
        std::size_t floors = 0;
        for (std::size_t at = kOnce->source.find("floor("); at != std::string::npos;
             at = kOnce->source.find("floor(", at + 1)) {
            ++floors;
        }
        RAWFRAME_EXPECT(floors == 1);
    }
    // A call in the vertex stage, where its streams are the vertex's.
    const graph::Document kMoved =
        surfaceOf(objectOf({{"position_offset", from(kShine, "out")}}),
                  {nodeOf(kFresnel, "rawframe/fresnel@1", Value::object(), Value::object()),
                   nodeOf(kShine,
                          "rawframe/combine3@1",
                          Value::object(),
                          objectOf({{"x", Value::real(0)}, {"y", from(kFresnel, "out")}, {"z", Value::real(0)}}))});
    const auto kPlaced = generateSlang(kMoved);
    RAWFRAME_EXPECT(kPlaced.has_value() && kPlaced->source.contains("(placed.normal)") &&
                    kPlaced->source.contains("(placed.view)"));
}

RAWFRAME_TEST(ACallSaysOnlyWhatItsFunctionTakes) {
    const auto kRefused = [](Value checker) {
        return refusedWith(validateSurface(called(std::move(checker))), MaterialError::Invalid);
    };
    // An input of the wrong type, one the function has not, and a
    // connection to an output the function has not.
    RAWFRAME_EXPECT(kRefused(objectOf({{"tiling", Value::real(4)}})));
    RAWFRAME_EXPECT(kRefused(objectOf({{"shade", Value::real(4)}})));
    RAWFRAME_EXPECT(kRefused(objectOf({{"uv", from(kTurned, "angle")}})));
    // A call with params.
    graph::Document withParams = called(Value::object());
    withParams.nodes[1] =
        nodeOf(kChecker, "rawframe/checkerboard@1", objectOf({{"tiling", pair(2, 2)}}), Value::object());
    RAWFRAME_EXPECT(refusedWith(validateSurface(withParams), MaterialError::Invalid));
    // An output node in a surface.
    graph::Document stray = called(Value::object());
    stray.nodes.push_back(nodeOf(0x2f00000000000055ULL,
                                 kOutputType,
                                 objectOf({{"name", Value::string("out")}}),
                                 objectOf({{"in", Value::real(1)}})));
    RAWFRAME_EXPECT(refusedWith(validateSurface(stray), MaterialError::Invalid));
}

RAWFRAME_TEST(AFunctionSaysOnlyWhatAFunctionMay) {
    // A function doubling its input; then what it may not say.
    const auto kText = [](Value inputs, Value outputs, std::vector<graph::Node> nodes) {
        graph::Document made{.kind = "function"};
        made.interface = objectOf({{"inputs", std::move(inputs)}, {"outputs", std::move(outputs)}});
        made.nodes = std::move(nodes);
        return *graph::writeDocument(made);
    };
    const auto kIn = [] {
        return objectOf({{"in", objectOf({{"default", Value::real(1)}, {"type", Value::string("float")}})}});
    };
    const auto kOut = [](std::string_view type) {
        return objectOf({{"out", objectOf({{"type", Value::string(std::string{type})}})}});
    };
    const auto kRead = [] {
        return nodeOf(kChecker, kParameterType, objectOf({{"name", Value::string("in")}}), Value::object());
    };
    const auto kTwice = [] {
        return nodeOf(kTurned,
                      "rawframe/multiply@1",
                      Value::object(),
                      objectOf({{"a", from(kChecker, "out")}, {"b", Value::real(2)}}));
    };
    const auto kFed = [](graph::NodeId id, Value in) {
        return nodeOf(id, kOutputType, objectOf({{"name", Value::string("out")}}), objectOf({{"in", std::move(in)}}));
    };
    const std::string kGood = kText(kIn(), kOut("float"), {kRead(), kTwice(), kFed(kFresnel, from(kTurned, "out"))});
    RAWFRAME_EXPECT(readFunction(kGood).has_value());
    // Not canonical; no output node, or two; fed another type; a texture
    // input; a surface inside; a call inside; no outputs.
    RAWFRAME_EXPECT(refusedWith(readFunction(kGood + "\n"), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(readFunction(kText(kIn(), kOut("float"), {kRead(), kTwice()})), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(
        readFunction(kText(kIn(),
                           kOut("float"),
                           {kRead(), kTwice(), kFed(kFresnel, from(kTurned, "out")), kFed(kShine, Value::real(1))})),
        MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(readFunction(kText(kIn(), kOut("vec2"), {kRead(), kTwice(), kFed(kFresnel, from(kTurned, "out"))})),
                    MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(readFunction(kText(objectOf({{"in",
                                                              objectOf({{"default", Value::string("a44ecb4a39ac5cc8")},
                                                                        {"type", Value::string("texture_2d")}})}}),
                                                   kOut("float"),
                                                   {kFed(kFresnel, Value::real(1))})),
                                MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(readFunction(kText(kIn(),
                                                   kOut("float"),
                                                   {kRead(),
                                                    nodeOf(kTurned, kSurfaceType, Value::object(), Value::object()),
                                                    kFed(kFresnel, Value::real(1))})),
                                MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(readFunction(kText(kIn(),
                                       kOut("float"),
                                       {kRead(),
                                        nodeOf(kTurned, "rawframe/fresnel@1", Value::object(), Value::object()),
                                        kFed(kFresnel, from(kTurned, "out"))})),
                    MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(readFunction(kText(kIn(), Value::object(), {kRead()})), MaterialError::Invalid));
}
