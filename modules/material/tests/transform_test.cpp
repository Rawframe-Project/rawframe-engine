// The transform node (D714): a point, a vector, or a normal taken between
// the model's, the World's, the eye's, the tangent frame's, and the clip's
// spaces, through the World's axes, in either stage, and what it refuses.

#include "generated/spaced_graph.h"
#include "generated/spaced_material.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"
#include "surface_documents.h"

#include <string>
#include <string_view>
#include <utility>

using namespace rawframe;
using namespace rawframe::material;
using namespace rawframe::material::testing;

namespace {

constexpr graph::NodeId kPlace = 0x2f00000000000061ULL;
constexpr graph::NodeId kMoved = 0x2f00000000000062ULL;
constexpr graph::NodeId kParts = 0x2f00000000000063ULL;

/// The World's position taken from `source` to `target` as `kind`
/// says, its result's first channel, or a clip's w, the roughness; or, given `offset`, the
/// vertex moved by it.
graph::Document transformed(std::string_view source,
                            std::string_view target,
                            std::string_view kind,
                            bool offset = false,
                            Value in = from(kPlace, "position")) {
    Value params = objectOf({{"from", Value::string(std::string{source})},
                             {"kind", Value::string(std::string{kind})},
                             {"to", Value::string(std::string{target})}});
    graph::Node moved = nodeOf(kMoved, kTransformType, std::move(params), objectOf({{"in", std::move(in)}}));
    const bool kFour = target == "clip";
    if (offset) {
        return surfaceOf(objectOf({{"position_offset", from(kMoved, "out")}}),
                         {nodeOf(kPlace, kWorldPositionType, Value::object(), Value::object()), std::move(moved)});
    }
    return surfaceOf(objectOf({{"specular_roughness", from(kParts, kFour ? "a" : "r")}}),
                     {nodeOf(kPlace, kWorldPositionType, Value::object(), Value::object()),
                      std::move(moved),
                      nodeOf(kParts,
                             kFour ? kSeparate4Type : kSeparate3Type,
                             Value::object(),
                             objectOf({{"in", from(kMoved, "out")}}))});
}

bool written(const graph::Document& surface, std::string_view expected) {
    const auto kWritten = generateSlang(surface);
    return kWritten.has_value() && kWritten->source.contains(expected);
}

} // namespace

RAWFRAME_TEST(ATransformGoesThroughTheWorldsAxes) {
    // Each space to the World's and back, as each kind.
    RAWFRAME_EXPECT(validateSurface(transformed("world", "object", "point")).has_value());
    RAWFRAME_EXPECT(written(transformed("world", "object", "point"), "toObject(point.spaces, point.position, kPoint)"));
    RAWFRAME_EXPECT(
        written(transformed("object", "world", "vector"), "fromObject(point.spaces, point.position, kVector)"));
    RAWFRAME_EXPECT(written(transformed("view", "world", "normal"), "fromView(point.spaces, point.position, kNormal)"));
    RAWFRAME_EXPECT(written(transformed("world", "tangent", "vector"),
                            "toTangent(point.normal, point.tangent, point.position, kVector)"));
    // Two spaces apart: through the World's.
    RAWFRAME_EXPECT(written(transformed("object", "view", "point"),
                            "toView(point.spaces, fromObject(point.spaces, point.position, kPoint), kPoint)"));
    RAWFRAME_EXPECT(written(transformed("tangent", "object", "normal"),
                            "toObject(point.spaces, fromTangent(point.normal, point.tangent, point.position, "
                            "kNormal), kNormal)"));
    // A point to the clip's space, four wide.
    RAWFRAME_EXPECT(written(transformed("world", "clip", "point"), "const float4 v0 = toClip(point.spaces"));
    // A literal input, three numbers read as a vec3.
    const Value kUp = Value::array({Value::real(0), Value::real(1), Value::real(0)});
    RAWFRAME_EXPECT(written(transformed("object", "world", "normal", false, kUp),
                            "fromObject(point.spaces, float3(0.0, 1.0, 0.0), kNormal)"));
    // In the vertex stage, a vertex's model rows.
    RAWFRAME_EXPECT(
        written(transformed("object", "world", "vector", true), "fromObject(placed.spaces, placed.position, kVector)"));
}

RAWFRAME_TEST(ATransformSaysItsSpacesAndItsKind) {
    const auto kInvalid = [](const graph::Document& surface) {
        return refusedWith(validateSurface(surface), MaterialError::Invalid);
    };
    // An unknown space or kind; the same space twice; a point in the
    // tangent frame; a vector to the clip's space; an input not a vec3.
    RAWFRAME_EXPECT(kInvalid(transformed("world", "screen", "point")));
    RAWFRAME_EXPECT(kInvalid(transformed("world", "object", "direction")));
    RAWFRAME_EXPECT(kInvalid(transformed("world", "world", "point")));
    RAWFRAME_EXPECT(kInvalid(transformed("tangent", "world", "point")));
    RAWFRAME_EXPECT(kInvalid(transformed("world", "clip", "vector")));
    RAWFRAME_EXPECT(kInvalid(transformed("world", "object", "point", false, Value::real(1))));
    // From the clip's space, which waits; the tangent frame in the vertex
    // stage, which has none yet.
    RAWFRAME_EXPECT(refusedWith(validateSurface(transformed("clip", "world", "point")), MaterialError::Unsupported));
    RAWFRAME_EXPECT(
        refusedWith(generateSlang(transformed("world", "tangent", "vector", true)), MaterialError::Unsupported));
    // A program's alone: the blob folds none.
    RAWFRAME_EXPECT(refusedWith(compileQualities(transformed("world", "object", "point")), MaterialError::Unsupported));
}

RAWFRAME_TEST(AGraphColoredByItsSpacesIsWrittenAsTheSlangTheScenesTestsDraw) {
    // spaced.material is red by how far its point lies from its model's
    // middle along the model's z, green by how squarely its normal faces
    // the eye (D714); the scene's tests draw what it is written as,
    // spaced_graph.slang, word for word.
    const std::string_view kDocument{reinterpret_cast<const char*>(kSpacedMaterial.data()), kSpacedMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kSpacedGraph.data()), kSpacedGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source == kGolden);
}
