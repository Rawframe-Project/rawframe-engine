// The Surface contract's feature groups: the coat's (D716) and the
// anisotropy's (D717), their inputs given as literals or connected, the
// material then a program material written with them, and what the
// groups' inputs refuse.

#include "generated/brushed_graph.h"
#include "generated/brushed_material.h"
#include "generated/coated_graph.h"
#include "generated/coated_material.h"
#include "generated/glass_graph.h"
#include "generated/glass_material.h"
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

RAWFRAME_TEST(AnAnisotropyIsWrittenAlongItsTurn) {
    // brushed.material stretches its roughness along the tangent turned by
    // its turn parameter (D717); the scene's tests draw what it is written
    // as, brushed_graph.slang, word for word.
    const std::string_view kDocument{reinterpret_cast<const char*>(kBrushedMaterial.data()), kBrushedMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(refusedWith(compile(*kRead), MaterialError::Unsupported));
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kBrushedGraph.data()), kBrushedGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source == kGolden);
    // A turn is any angle but nought; a strength from nought to one.
    const auto kValid = [](Value inputs) {
        return validateSurface(coated(std::move(inputs))).has_value();
    };
    RAWFRAME_EXPECT(kValid(objectOf({{"specular_anisotropy_rotation", Value::real(-2.5)}})));
    RAWFRAME_EXPECT(!kValid(objectOf({{"specular_anisotropy_rotation", Value::real(0)}})));
    RAWFRAME_EXPECT(!kValid(objectOf({{"specular_roughness_anisotropy", Value::real(-0.5)}})));
}

RAWFRAME_TEST(ATransmissionIsWrittenWithItsVolume) {
    // D720: a weight, a tint and the depth it is reached at, a thickness,
    // and a thin wall, each written; the material transmits.
    const Value kTint = Value::array({Value::real(0.5), Value::real(0.9), Value::real(0.7)});
    const graph::Document kGlass = coated(objectOf({{"geometry_thickness", Value::real(0.25)},
                                                    {"geometry_thin_walled", Value::boolean(true)},
                                                    {"transmission_color", kTint},
                                                    {"transmission_depth", Value::real(2)},
                                                    {"transmission_weight", Value::real(1)}}));
    RAWFRAME_EXPECT(validateSurface(kGlass).has_value());
    RAWFRAME_EXPECT(refusedWith(compile(kGlass), MaterialError::Unsupported));
    const auto kWritten = generateSlang(kGlass);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->transmits && !kWritten->moves &&
                    kWritten->source.contains("made.transmission = 1.0;") &&
                    kWritten->source.contains("made.transmissionColor = float3(0.5, 0.9, 0.7);") &&
                    kWritten->source.contains("made.transmissionDepth = 2.0;") &&
                    kWritten->source.contains("made.thickness = 0.25;") &&
                    kWritten->source.contains("made.thinWalled = true;"));
    // A material whose transmission is not given does not transmit.
    const auto kPlain = generateSlang(coated(objectOf({{"coat_weight", Value::real(1)}})));
    RAWFRAME_EXPECT(kPlain.has_value() && !kPlain->transmits && kPlain->source.contains("made.thinWalled = false;"));
    // A distance of nought or below, white, a color out of range, a wall
    // not thin, and a flag that is a number.
    const auto kInvalid = [](Value inputs) {
        return refusedWith(validateSurface(coated(std::move(inputs))), MaterialError::Invalid);
    };
    RAWFRAME_EXPECT(kInvalid(objectOf({{"transmission_depth", Value::real(-1)}})));
    RAWFRAME_EXPECT(
        kInvalid(objectOf({{"transmission_color", Value::array({Value::real(1), Value::real(1), Value::real(1)})}})));
    RAWFRAME_EXPECT(
        kInvalid(objectOf({{"transmission_color", Value::array({Value::real(2), Value::real(1), Value::real(1)})}})));
    RAWFRAME_EXPECT(kInvalid(objectOf({{"geometry_thin_walled", Value::boolean(false)}})));
    RAWFRAME_EXPECT(kInvalid(objectOf({{"geometry_thin_walled", Value::real(1)}})));
}

RAWFRAME_TEST(AGlassIsWrittenAsTheSlangTheScenesTestsDraw) {
    // glass.material transmits all but its reflection, half a meter thick
    // (D720); the scene's tests draw what it is written as,
    // glass_graph.slang, word for word.
    const std::string_view kDocument{reinterpret_cast<const char*>(kGlassMaterial.data()), kGlassMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kGlassGraph.data()), kGlassGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->transmits && kWritten->source == kGolden);
}
