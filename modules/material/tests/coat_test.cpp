// The coat's feature group (D716): its inputs given as literals or
// connected, the material then a program material written with them, and
// what the group's inputs refuse.

#include "generated/coated_graph.h"
#include "generated/coated_material.h"
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

constexpr graph::NodeId kBent = 0x2f00000000000092ULL;
constexpr graph::NodeId kWorn = 0x2f00000000000093ULL;

/// A surface whose coat's inputs are `inputs`.
graph::Document coated(Value inputs, std::vector<graph::Node> nodes = {}) {
    return surfaceOf(std::move(inputs), std::move(nodes));
}

} // namespace

RAWFRAME_TEST(ACoatIsWrittenOverItsBase) {
    // Connected: its weight from the vertex's alpha, its normal the World's
    // up, the roughness left at nought.
    const graph::Document kMade =
        coated(objectOf({{"coat_normal", from(kBent, "out")}, {"coat_weight", from(kWorn, "alpha")}}),
               {nodeOf(kBent,
                       kConstantType,
                       objectOf({{"type", Value::string("vec3")},
                                 {"value", Value::array({Value::real(0), Value::real(1), Value::real(0)})}}),
                       Value::object()),
                nodeOf(kWorn, kVertexColorType, Value::object(), Value::object())});
    RAWFRAME_EXPECT(validateSurface(kMade).has_value());
    const auto kWritten = generateSlang(kMade);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source.contains("made.coat = point.color.a;") &&
                    kWritten->source.contains("made.coatRoughness = 0.0;") &&
                    kWritten->source.contains("made.coatNormal = float3(0.0, 1.0, 0.0);"));
    RAWFRAME_EXPECT(refusedWith(compile(kMade), MaterialError::Unsupported));
    // A number at its default of nought, past one, or not a number; a
    // literal normal; a weight connected to a color.
    const auto kInvalid = [](Value inputs) {
        return refusedWith(validateSurface(coated(std::move(inputs))), MaterialError::Invalid);
    };
    RAWFRAME_EXPECT(kInvalid(objectOf({{"coat_weight", Value::real(0)}})));
    RAWFRAME_EXPECT(kInvalid(objectOf({{"coat_weight", Value::real(1.5)}})));
    RAWFRAME_EXPECT(kInvalid(objectOf({{"coat_roughness", Value::boolean(true)}})));
    RAWFRAME_EXPECT(
        kInvalid(objectOf({{"coat_normal", Value::array({Value::real(0), Value::real(1), Value::real(0)})}})));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(coated(objectOf({{"coat_weight", from(kWorn, "color")}}),
                                           {nodeOf(kWorn, kVertexColorType, Value::object(), Value::object())})),
                    MaterialError::Invalid));
}
RAWFRAME_TEST(AGraphCoatedOverItsBaseIsWrittenAsTheSlangTheScenesTestsDraw) {
    // coated.material is rough beneath a smooth coat (D716): a program
    // material, which the blob holds no coat for; the scene's tests draw
    // what it is written as, coated_graph.slang, word for word.
    const std::string_view kDocument{reinterpret_cast<const char*>(kCoatedMaterial.data()), kCoatedMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(refusedWith(compile(*kRead), MaterialError::Unsupported));
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kCoatedGraph.data()), kCoatedGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source == kGolden);
}
