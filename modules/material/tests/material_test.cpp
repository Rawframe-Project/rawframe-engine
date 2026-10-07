// Surface materials: one canonical text on the graph grammar, SPEC-0026's
// Surface contract held (shapes, ranges, defaults left out, states), a
// literal surface compiled to its blob, a sampled texture feeding its base
// color and opacity, and what generation 1 cannot compile refused, never
// guessed at.

#include "generated/faded_graph.h"
#include "generated/faded_material.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::material;

namespace {

bool refusedWith(const auto& outcome, MaterialError error) {
    return !outcome.has_value() && outcome.error().domain() == kMaterialDomain && outcome.error().code() == code(error);
}

constexpr graph::NodeId kNode = 0x2f00000000000002ULL;

/// A red, rougher, masked, double-sided material.
Material red() {
    Material made{.blend = Blend::Masked, .alphaCutoff = 0.25F, .doubleSided = true};
    made.surface.baseColor = {0.8F, 0.1F, 0.1F};
    made.surface.specularRoughness = 0.5F;
    return made;
}

} // namespace

RAWFRAME_TEST(AMaterialReadsBackAsItWasMade) {
    const auto kText = writeMaterial(documentOf(red(), kNode));
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    // The defaults left out; the red's channels as written.
    RAWFRAME_EXPECT(kText->contains("\"base_color\": [\n          0.8,\n          0.1,\n          0.1\n") &&
                    !kText->contains("specular_ior") && kText->contains("\"alpha_cutoff\": 0.25"));
    const auto kRead = readMaterial(*kText);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kCompiled = compile(*kRead);
    RAWFRAME_EXPECT(kCompiled.has_value() && *kCompiled == red());
    // The default material is one surface node with nothing on it.
    const auto kPlain = compile(documentOf({}, kNode));
    RAWFRAME_EXPECT(kPlain.has_value() && *kPlain == Material{});
    // Anything but the canonical text is refused.
    std::string loose = *kText;
    loose.insert(loose.find("\"kind\""), " ");
    RAWFRAME_EXPECT(!readMaterial(loose).has_value());
}

RAWFRAME_TEST(TheSurfaceContractIsHeld) {
    const auto kWith = [](std::string_view input, document::Value value) {
        graph::Document made = documentOf({}, kNode);
        document::Value inputs = document::Value::object();
        inputs.add(std::string{input}, std::move(value));
        document::Value record = document::Value::object();
        record.add("type", document::Value::string(std::string{kSurfaceType}));
        record.add("params", document::Value::object());
        record.add("inputs", std::move(inputs));
        made.nodes.front().record = std::move(record);
        return made;
    };
    using document::Value;
    RAWFRAME_EXPECT(validateSurface(kWith("base_metalness", Value::real(1))).has_value());
    // Out of range, the wrong shape, a default written, a name the contract
    // lacks.
    RAWFRAME_EXPECT(refusedWith(validateSurface(kWith("base_metalness", Value::real(1.5))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kWith("specular_ior", Value::real(0.5))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kWith("base_color", Value::real(0.5))), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(kWith("specular_roughness", Value::real(0.3))), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(kWith("fuzz_weight", Value::real(1))), MaterialError::Invalid));
    // A cutoff without masking, a state at its default.
    graph::Document cut = documentOf(red(), kNode);
    cut.sections.front().second = [] {
        Value states = Value::object();
        states.add("alpha_cutoff", Value::real(0.25));
        return states;
    }();
    RAWFRAME_EXPECT(refusedWith(validateSurface(cut), MaterialError::Invalid));
    graph::Document lit = documentOf(red(), kNode);
    lit.sections.front().second = [] {
        Value states = Value::object();
        states.add("shading", Value::string("lit"));
        return states;
    }();
    RAWFRAME_EXPECT(refusedWith(validateSurface(lit), MaterialError::Invalid));
    // Two surfaces, or none.
    graph::Document twice = documentOf({}, kNode);
    twice.nodes.push_back({.id = kNode + 1, .record = twice.nodes.front().record});
    RAWFRAME_EXPECT(refusedWith(validateSurface(twice), MaterialError::Invalid));
}

RAWFRAME_TEST(WhatCannotCompileYetIsRefusedNotGuessed) {
    using document::Value;
    // A node of the standard library's, not built yet, feeding the surface.
    graph::Document fed = documentOf({}, kNode);
    Value constant = Value::object();
    constant.add("type", Value::string("rawframe/constant@1"));
    constant.add("params", Value::object());
    constant.add("inputs", Value::object());
    fed.nodes.insert(fed.nodes.begin(), {.id = 0x1f00000000000001ULL, .record = constant});
    Value inputs = Value::object();
    inputs.add("base_metalness", graph::connectionValue({.node = 0x1f00000000000001ULL, .output = "out"}));
    fed.nodes.back().record = [&inputs] {
        Value record = Value::object();
        record.add("type", Value::string(std::string{kSurfaceType}));
        record.add("params", Value::object());
        record.add("inputs", inputs);
        return record;
    }();
    RAWFRAME_EXPECT(validateSurface(fed).has_value());
    const auto kText = writeMaterial(fed);
    RAWFRAME_EXPECT(kText.has_value() && readMaterial(*kText).has_value());
    RAWFRAME_EXPECT(refusedWith(compile(fed), MaterialError::Unsupported));
    RAWFRAME_EXPECT(refusedWith(semanticHash(fed), MaterialError::Unsupported));
}

RAWFRAME_TEST(TheHashAndTheBlobFollowTheMaterial) {
    const auto kFirst = semanticHash(documentOf(red(), kNode));
    const auto kRenamed = semanticHash(documentOf(red(), 0x0a0000000000000aULL));
    Material rougher = red();
    rougher.surface.specularRoughness = 0.75F;
    const auto kOther = semanticHash(documentOf(rougher, kNode));
    RAWFRAME_EXPECT(kFirst.has_value() && kRenamed.has_value() && kOther.has_value());
    if (kFirst.has_value() && kRenamed.has_value() && kOther.has_value()) {
        RAWFRAME_EXPECT(*kFirst == *kRenamed && *kFirst != *kOther);
    }
    Material glowing;
    glowing.surface.emissionColor = {1, 0.5F, 0};
    glowing.surface.emissionLuminance = 100;
    glowing.surface.specularWeight = 0.5F;
    glowing.shading = Shading::Unlit;
    const std::array<float, kBlobFloats> kBlob = blobOf(glowing);
    RAWFRAME_EXPECT(kBlob[0] == 0.8F && kBlob[4] == 0.5F && kBlob[7] == 0.3F && kBlob[8] == 100 && kBlob[9] == 50 &&
                    kBlob[10] == 0 && kBlob[11] == 1.5F && kBlob[14] == 0 && kBlob[15] == 1);
    RAWFRAME_EXPECT(blobOf(red())[14] == 0.25F);
}

RAWFRAME_TEST(ACookedMaterialDecodesAsItWasEncoded) {
    const std::vector<std::byte> kBytes = encode(red());
    const auto kDecoded = decode(kBytes);
    RAWFRAME_EXPECT(kBytes.size() == 208 && kDecoded.has_value() && *kDecoded == (Qualities{red(), red(), red()}));
    // Anything else is refused: short, another format, a value out of the
    // contract's range, a state out of its set.
    RAWFRAME_EXPECT(refusedWith(decode(std::span{kBytes}.first(207)), MaterialError::Invalid));
    std::vector<std::byte> other = kBytes;
    other[4] = std::byte{2};
    RAWFRAME_EXPECT(refusedWith(decode(other), MaterialError::Invalid));
    Material metallic = red();
    metallic.surface.baseMetalness = 2;
    RAWFRAME_EXPECT(refusedWith(decode(encode(metallic)), MaterialError::Invalid));
    std::vector<std::byte> blended = kBytes;
    blended[9] = std::byte{3};
    RAWFRAME_EXPECT(refusedWith(decode(blended), MaterialError::Invalid));
}

namespace {

using document::Value;

constexpr graph::NodeId kSampler = kNode + 1;
constexpr graph::NodeId kUv = kNode + 2;

/// A material whose base color and opacity are a texture's, sampled
/// nearest and clamped.
Material stencilled() {
    Material made{.blend = Blend::Translucent};
    made.surface.baseColor = {1, 1, 1};
    made.surface.specularRoughness = 0.5F;
    made.textures.base = {.id = 0xa44ecb4a39ac5cc8ULL, .filter = Filter::Nearest, .address = Address::Clamp};
    made.textures.baseColor = true;
    made.textures.opacity = true;
    return made;
}

Value objectOf(std::vector<std::pair<std::string, Value>> members) {
    Value made = Value::object();
    for (auto& [name, value] : members) {
        made.add(std::move(name), std::move(value));
    }
    return made;
}

graph::Node nodeOf(graph::NodeId id, std::string_view type, Value params, Value inputs) {
    return {.id = id,
            .record = objectOf({{"type", Value::string(std::string{type})},
                                {"params", std::move(params)},
                                {"inputs", std::move(inputs)}})};
}

/// A surface node given `inputs`, then `nodes`.
graph::Document surfaceOf(Value inputs, std::vector<graph::Node> nodes) {
    graph::Document made{.kind = "surface"};
    made.nodes.push_back(nodeOf(kNode, kSurfaceType, Value::object(), std::move(inputs)));
    std::ranges::move(nodes, std::back_inserter(made.nodes));
    return made;
}

Value from(graph::NodeId node, std::string output) {
    return graph::connectionValue({.node = node, .output = std::move(output)});
}

/// A sampler of the texture a44ecb4a39ac5cc8 with `params` besides.
graph::Node samplerOf(std::vector<std::pair<std::string, Value>> params, Value inputs = Value::object()) {
    params.emplace_back("texture", Value::string("a44ecb4a39ac5cc8"));
    return nodeOf(kSampler, kSampleTexture2dType, objectOf(std::move(params)), std::move(inputs));
}

/// Its color feeding the base color, sampled at a uv node's `channel`.
graph::Document sampledAt(std::int64_t channel) {
    std::vector<std::pair<std::string, Value>> params;
    if (channel != 0) {
        params.emplace_back("channel", Value::integer(channel));
    }
    return surfaceOf(objectOf({{"base_color", from(kSampler, "color")}}),
                     {samplerOf({}, objectOf({{"uv", from(kUv, "uv")}})),
                      nodeOf(kUv, kUvType, objectOf(std::move(params)), Value::object())});
}

} // namespace

RAWFRAME_TEST(ASampledTextureFeedsTheBaseColorAndOpacity) {
    const auto kText = writeMaterial(documentOf(stencilled(), kNode));
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kText->contains("\"type\": \"rawframe/sample_texture_2d@1\"") &&
                    kText->contains("\"texture\": \"a44ecb4a39ac5cc8\"") && kText->contains("\"output\": \"alpha\""));
    const auto kRead = readMaterial(*kText);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kCompiled = compile(*kRead);
    RAWFRAME_EXPECT(kCompiled.has_value() && *kCompiled == stencilled());
    // Cooked and read back; its blob white where the texture colors it.
    const auto kDecoded = decode(encode(stencilled()));
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == (Qualities{stencilled(), stencilled(), stencilled()}));
    const std::array<float, kBlobFloats> kBlob = blobOf(stencilled());
    RAWFRAME_EXPECT(kBlob[0] == 1 && kBlob[1] == 1 && kBlob[2] == 1 && kBlob[7] == 0.5F && kBlob[15] == 6);
    // Its alpha alone: the literal color stays.
    Material masked{.blend = Blend::Masked};
    masked.surface.baseColor = {0.2F, 0.3F, 0.4F};
    masked.textures.base.id = 7;
    masked.textures.opacity = true;
    const auto kMasked = compile(documentOf(masked, kNode));
    RAWFRAME_EXPECT(kMasked.has_value() && *kMasked == masked && blobOf(masked)[0] == 0.2F && blobOf(masked)[15] == 4);
    // Sampled at a mesh's first coordinates, named or not; the hash follows
    // the texture.
    const auto kNamed = compile(sampledAt(0));
    RAWFRAME_EXPECT(kNamed.has_value() && kNamed->textures.baseColor && !kNamed->textures.opacity);
    const auto kHash = semanticHash(documentOf(stencilled(), kNode));
    Material other = stencilled();
    other.textures.base.id = 8;
    const auto kOther = semanticHash(documentOf(other, kNode));
    RAWFRAME_EXPECT(kHash.has_value() && kOther.has_value() && *kHash != *kOther);
}

RAWFRAME_TEST(TheNodeLibraryIsHeld) {
    const Value kColor = objectOf({{"base_color", from(kSampler, "color")}});
    RAWFRAME_EXPECT(
        validateSurface(surfaceOf(kColor, {samplerOf({{"filter", Value::string("nearest")}})})).has_value());
    // The sampler state at its default or out of its set, a param it lacks,
    // out of name order.
    for (auto params : std::vector<std::vector<std::pair<std::string, Value>>>{
             {{"filter", Value::string("linear")}},
             {{"filter", Value::string("cubic")}},
             {{"address", Value::string("repeat")}},
             {{"lod", Value::real(1)}},
             {{"filter", Value::string("nearest")}, {"address", Value::string("clamp")}}}) {
        RAWFRAME_EXPECT(
            refusedWith(validateSurface(surfaceOf(kColor, {samplerOf(std::move(params))})), MaterialError::Invalid));
    }
    // A texture named by nought, or not in form.
    for (const std::string_view kName : {"0000000000000000", "A44ECB4A39AC5CC8", "a44e"}) {
        const graph::Node kNamed = nodeOf(kSampler,
                                          kSampleTexture2dType,
                                          objectOf({{"texture", Value::string(std::string{kName})}}),
                                          Value::object());
        RAWFRAME_EXPECT(refusedWith(validateSurface(surfaceOf(kColor, {kNamed})), MaterialError::Invalid));
    }
    // A uv literal, or from what is not a uv node's output; a channel past
    // seven.
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(
                        kColor, {samplerOf({}, objectOf({{"uv", Value::array({Value::real(0), Value::real(0)})}}))})),
                    MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(kColor, {samplerOf({}, objectOf({{"uv", from(kSampler, "color")}}))})),
                    MaterialError::Invalid));
    RAWFRAME_EXPECT(validateSurface(sampledAt(7)).has_value());
    RAWFRAME_EXPECT(refusedWith(validateSurface(sampledAt(8)), MaterialError::Invalid));
    // An input fed an output of another type, or one its node lacks.
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(objectOf({{"base_color", from(kSampler, "alpha")}}), {samplerOf({})})),
                    MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(objectOf({{"base_color", from(kSampler, "rgb")}}), {samplerOf({})})),
                    MaterialError::Invalid));
}

RAWFRAME_TEST(WhatGenerationOneCannotSampleIsRefused) {
    // Another input fed; a second texture; coordinates past the first set.
    const graph::Document kRough = surfaceOf(objectOf({{"specular_ior", from(kSampler, "alpha")}}), {samplerOf({})});
    RAWFRAME_EXPECT(validateSurface(kRough).has_value() && refusedWith(compile(kRough), MaterialError::Unsupported));
    graph::Node second = samplerOf({});
    second.id = kUv;
    const graph::Document kTwo =
        surfaceOf(objectOf({{"base_color", from(kSampler, "color")}, {"geometry_opacity", from(kUv, "alpha")}}),
                  {samplerOf({}), second});
    RAWFRAME_EXPECT(validateSurface(kTwo).has_value() && refusedWith(compile(kTwo), MaterialError::Unsupported));
    RAWFRAME_EXPECT(validateSurface(sampledAt(1)).has_value() &&
                    refusedWith(compile(sampledAt(1)), MaterialError::Unsupported));
    // Cooked bytes whose texture feeds nothing, or with no texture a
    // sampler state.
    std::vector<std::byte> feedsNothing = encode(stencilled());
    feedsNothing[192] = std::byte{0};
    feedsNothing[193] = std::byte{0};
    RAWFRAME_EXPECT(refusedWith(decode(feedsNothing), MaterialError::Invalid));
    std::vector<std::byte> stateless = encode(red());
    stateless[88] = std::byte{1};
    RAWFRAME_EXPECT(refusedWith(decode(stateless), MaterialError::Invalid));
}

namespace {

graph::Node mathOf(graph::NodeId id, std::string_view type, Value a, Value b) {
    return nodeOf(id, type, Value::object(), objectOf({{"a", std::move(a)}, {"b", std::move(b)}}));
}

Value pair(double x, double y) {
    return Value::array({Value::real(x), Value::real(y)});
}

/// Its color feeding the base color, sampled where `math` (ids after the
/// uv node's) says, the last of them feeding the sampler.
graph::Document sampledThrough(std::vector<graph::Node> math) {
    std::vector<graph::Node> nodes = {samplerOf({}, objectOf({{"uv", from(math.back().id, "out")}})),
                                      nodeOf(kUv, kUvType, Value::object(), Value::object())};
    std::ranges::move(math, std::back_inserter(nodes));
    return surfaceOf(objectOf({{"base_color", from(kSampler, "color")}}), std::move(nodes));
}

} // namespace

RAWFRAME_TEST(ATextureIsTiledMovedAndTinted) {
    // Four times across and twice down, moved half a texture, its color
    // times a factor and its alpha times a half.
    Material tiled = stencilled();
    tiled.surface.baseColor = {0.5F, 0.25F, 1};
    tiled.surface.geometryOpacity = 0.5F;
    tiled.textures.base.scale = {4, 2};
    tiled.textures.base.offset = {0.5F, 0};
    const auto kText = writeMaterial(documentOf(tiled, kNode));
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("rawframe/multiply@1") && kText->contains("rawframe/add@1"));
    const auto kRead = kText.has_value() ? readMaterial(*kText) : std::unexpected{kText.error().clone()};
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kCompiled = compile(*kRead);
    RAWFRAME_EXPECT(kCompiled.has_value() && *kCompiled == tiled);
    const auto kDecoded = decode(encode(tiled));
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == (Qualities{tiled, tiled, tiled}));
    const std::array<float, kBlobFloats> kBlob = blobOf(tiled);
    RAWFRAME_EXPECT(kBlob[0] == 0.5F && kBlob[1] == 0.25F && kBlob[12] == 0.5F && kBlob[15] == 6 && kBlob[16] == 4 &&
                    kBlob[17] == 2 && kBlob[18] == 0.5F && kBlob[19] == 0);
    // Moved, then scaled by one number, either operand first: the move is
    // scaled too.
    const auto kFolded =
        compile(sampledThrough({mathOf(kNode + 3, kAddType, from(kUv, "uv"), pair(1, -1)),
                                mathOf(kNode + 4, kMultiplyType, Value::real(3), from(kNode + 3, "out"))}));
    RAWFRAME_EXPECT(kFolded.has_value() && kFolded->textures.base.scale == (std::array<float, 2>{3, 3}) &&
                    kFolded->textures.base.offset == (std::array<float, 2>{3, -3}));
    // A color scaled by one number is tinted grey.
    const auto kGrey = compile(
        surfaceOf(objectOf({{"base_color", from(kNode + 3, "out")}}),
                  {samplerOf({}), mathOf(kNode + 3, kMultiplyType, from(kSampler, "color"), Value::real(0.25))}));
    RAWFRAME_EXPECT(kGrey.has_value() && kGrey->surface.baseColor == (std::array<float, 3>{0.25F, 0.25F, 0.25F}));
}

RAWFRAME_TEST(WhatTheMathNodesCannotSayIsRefused) {
    // Literals of the wrong count; an input left out; types that do not
    // meet; a sampler's coordinates of another type.
    for (const graph::Node& kBad :
         {mathOf(kNode + 3,
                 kMultiplyType,
                 from(kUv, "uv"),
                 Value::array({Value::real(1), Value::real(1), Value::real(1), Value::real(1)})),
          nodeOf(kNode + 3, kMultiplyType, Value::object(), objectOf({{"a", from(kUv, "uv")}})),
          mathOf(kNode + 3,
                 kMultiplyType,
                 from(kUv, "uv"),
                 Value::array({Value::real(1), Value::real(1), Value::real(1)})),
          mathOf(kNode + 3, kAddType, pair(1, 1), Value::array({Value::real(1), Value::real(1), Value::real(1)}))}) {
        RAWFRAME_EXPECT(refusedWith(validateSurface(sampledThrough({kBad})), MaterialError::Invalid));
    }
    // Generation 1 folds literals only; a factor stays in its input's range.
    RAWFRAME_EXPECT(
        refusedWith(compile(sampledThrough({mathOf(kNode + 3, kMultiplyType, from(kUv, "uv"), from(kUv, "uv"))})),
                    MaterialError::Unsupported));
    RAWFRAME_EXPECT(refusedWith(compile(sampledThrough({mathOf(kNode + 3, kAddType, pair(1, 1), pair(2, 2))})),
                                MaterialError::Unsupported));
    RAWFRAME_EXPECT(refusedWith(
        compile(surfaceOf(objectOf({{"base_color", from(kNode + 3, "out")}}),
                          {samplerOf({}), mathOf(kNode + 3, kMultiplyType, from(kSampler, "color"), Value::real(2))})),
        MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(compile(surfaceOf(
                        objectOf({{"specular_ior", from(kNode + 3, "out")}}),
                        {samplerOf({}), mathOf(kNode + 3, kMultiplyType, from(kSampler, "alpha"), Value::real(0.5))})),
                    MaterialError::Unsupported));
}

RAWFRAME_TEST(APackedTextureAndAnEmissionTextureFeedTheirInputs) {
    // glTF's occlusion, roughness, and metalness in one texture, red, green,
    // and blue, the roughness at a factor; a glowing texture of its own;
    // over a base texture's color.
    Material made = stencilled();
    made.textures.opacity = false;
    made.surface.specularRoughness = 0.5F;
    made.surface.emissionLuminance = 200;
    made.textures.packed = {.id = 0x77, .scale = {2, 2}};
    made.textures.metalness = Channel::Blue;
    made.textures.roughness = Channel::Green;
    made.textures.occlusion = Channel::Red;
    made.surface.baseMetalness = 1;
    made.surface.ambientOcclusion = 1;
    made.textures.emission = {.id = 0x99, .filter = Filter::Nearest};
    made.surface.emissionColor = {1, 0.5F, 0.25F};
    const auto kText = writeMaterial(documentOf(made, kNode));
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("rawframe/separate3@1"));
    const auto kRead = kText.has_value() ? readMaterial(*kText) : std::unexpected{kText.error().clone()};
    const auto kCompiled = kRead.has_value() ? compile(*kRead) : std::unexpected{kRead.error().clone()};
    RAWFRAME_EXPECT(kCompiled.has_value() && *kCompiled == made);
    const auto kDecoded = decode(encode(made));
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == (Qualities{made, made, made}));
    const std::array<float, kBlobFloats> kBlob = blobOf(made);
    RAWFRAME_EXPECT(kBlob[3] == 1 && kBlob[7] == 0.5F && kBlob[8] == 200 && kBlob[9] == 100 && kBlob[15] == 10 &&
                    kBlob[20] == 2 && kBlob[21] == 2 && kBlob[32] == 3 && kBlob[33] == 2 && kBlob[34] == 1 &&
                    kBlob[35] == 1);
    // The roughness from an alpha channel (Unity's packing), and the same
    // texture for everything.
    const auto kAlpha = compile(
        surfaceOf(objectOf({{"base_color", from(kSampler, "color")}, {"specular_roughness", from(kSampler, "alpha")}}),
                  {samplerOf({})}));
    RAWFRAME_EXPECT(kAlpha.has_value() && kAlpha->textures.roughness == Channel::Alpha &&
                    kAlpha->textures.packed.id == kAlpha->textures.base.id);
    // Two textures packed into one input's neighbors, a channel of the
    // base color's, and a separated literal are refused.
    graph::Node other = samplerOf({});
    other.id = kUv;
    const auto kTwoPacked = compile(
        surfaceOf(objectOf({{"base_metalness", from(kSampler, "alpha")}, {"specular_roughness", from(kUv, "alpha")}}),
                  {samplerOf({}), other}));
    RAWFRAME_EXPECT(refusedWith(kTwoPacked, MaterialError::Unsupported));
    const auto kLiteral = compile(
        surfaceOf(objectOf({{"base_metalness", from(kNode + 3, "r")}}),
                  {nodeOf(kNode + 3,
                          kSeparate3Type,
                          Value::object(),
                          objectOf({{"in", Value::array({Value::real(1), Value::real(1), Value::real(1)})}}))}));
    RAWFRAME_EXPECT(refusedWith(kLiteral, MaterialError::Unsupported));
    const auto kSeparatedUv = validateSurface(
        surfaceOf(objectOf({{"base_metalness", from(kNode + 3, "r")}}),
                  {nodeOf(kUv, kUvType, Value::object(), Value::object()),
                   nodeOf(kNode + 3, kSeparate3Type, Value::object(), objectOf({{"in", from(kUv, "uv")}}))}));
    RAWFRAME_EXPECT(refusedWith(kSeparatedUv, MaterialError::Invalid));
}

RAWFRAME_TEST(ANormalTextureBendsTheGeometryNormal) {
    // A normal texture through normal_map at half its strength, tiled.
    Material bumped;
    bumped.textures.normal = {.id = 0x88, .scale = {3, 3}};
    bumped.textures.normalScale = 0.5F;
    const auto kText = writeMaterial(documentOf(bumped, kNode));
    RAWFRAME_EXPECT(kText.has_value() && kText->contains("rawframe/normal_map@1") &&
                    kText->contains("\"geometry_normal\""));
    const auto kRead = kText.has_value() ? readMaterial(*kText) : std::unexpected{kText.error().clone()};
    const auto kCompiled = kRead.has_value() ? compile(*kRead) : std::unexpected{kRead.error().clone()};
    RAWFRAME_EXPECT(kCompiled.has_value() && *kCompiled == bumped);
    const auto kDecoded = decode(encode(bumped));
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == (Qualities{bumped, bumped, bumped}));
    const std::array<float, kBlobFloats> kBlob = blobOf(bumped);
    RAWFRAME_EXPECT(kBlob[15] == 16 && kBlob[28] == 3 && kBlob[29] == 3 && kBlob[35] == 0.5F);
    // A literal normal, a scale at its default, a normal from anything
    // but a normal map, and a normal map of a vec2 are refused.
    RAWFRAME_EXPECT(refusedWith(
        validateSurface(surfaceOf(
            objectOf({{"geometry_normal", Value::array({Value::real(0), Value::real(0), Value::real(1)})}}), {})),
        MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(validateSurface(surfaceOf(objectOf({{"geometry_normal", from(kNode + 3, "out")}}),
                                                          {samplerOf({}),
                                                           nodeOf(kNode + 3,
                                                                  kNormalMapType,
                                                                  objectOf({{"scale", Value::real(1)}}),
                                                                  objectOf({{"in", from(kSampler, "color")}}))})),
                                MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(
        validateSurface(surfaceOf(objectOf({{"geometry_normal", from(kSampler, "color")}}), {samplerOf({})})),
        MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validateSurface(surfaceOf(
                        objectOf({{"geometry_normal", from(kNode + 3, "out")}}),
                        {nodeOf(kUv, kUvType, Value::object(), Value::object()),
                         nodeOf(kNode + 3, kNormalMapType, Value::object(), objectOf({{"in", from(kUv, "uv")}}))})),
                    MaterialError::Invalid));
}

RAWFRAME_TEST(AQualitySwitchGivesEachQualityItsInput) {
    // The base color a texture's at every quality but the low, where it is
    // a plain grey; the roughness 0.8 but at the low quality its default.
    constexpr graph::NodeId kColor = kNode + 3;
    constexpr graph::NodeId kRough = kNode + 4;
    const auto kGrey = Value::array({Value::real(0.5), Value::real(0.5), Value::real(0.5)});
    const auto kSwitchOf = [](graph::NodeId id, std::vector<std::pair<std::string, Value>> inputs) {
        return nodeOf(id, kQualitySwitchType, Value::object(), objectOf(std::move(inputs)));
    };
    const graph::Document kSwitched =
        surfaceOf(objectOf({{"base_color", from(kColor, "out")}, {"specular_roughness", from(kRough, "out")}}),
                  {samplerOf({}),
                   kSwitchOf(kColor, {{"default", from(kSampler, "color")}, {"low", kGrey}}),
                   kSwitchOf(kRough, {{"default", Value::real(0.8)}, {"low", Value::real(0.3)}})});
    const auto kText = writeMaterial(kSwitched);
    RAWFRAME_EXPECT(kText.has_value() && readMaterial(*kText).has_value());
    const auto kQualities = compileQualities(kSwitched);
    RAWFRAME_EXPECT(kQualities.has_value());
    if (!kQualities.has_value()) {
        return;
    }
    const Material& kLow = kQualities->at(static_cast<std::size_t>(Quality::Low));
    const Material& kMedium = kQualities->at(static_cast<std::size_t>(Quality::Medium));
    const Material& kHigh = kQualities->at(static_cast<std::size_t>(Quality::High));
    RAWFRAME_EXPECT(kHigh.textures.baseColor && kHigh.textures.base.id != 0 && kHigh.surface.specularRoughness == 0.8F);
    RAWFRAME_EXPECT(!kLow.textures.baseColor && kLow.textures.base.id == 0 &&
                    kLow.surface.baseColor == (std::array<float, 3>{0.5F, 0.5F, 0.5F}) &&
                    kLow.surface.specularRoughness == 0.3F);
    // A quality the switch does not name takes its default.
    RAWFRAME_EXPECT(kMedium == kHigh && compile(kSwitched).has_value() && *compile(kSwitched) == kHigh);
    // Cooked with the low material apart, and read back whole.
    const std::vector<std::byte> kBytes = encode(*kQualities);
    RAWFRAME_EXPECT(kBytes.size() == 208 + 196);
    const auto kDecoded = decode(kBytes);
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == *kQualities);
    // A quality held apart that does not differ, or cut short, is refused.
    std::vector<std::byte> same = encode(kHigh);
    same[204] = std::byte{1};
    const std::vector<std::byte> kHighBody{same.begin() + 8, same.begin() + 204};
    same.insert(same.end(), kHighBody.begin(), kHighBody.end());
    RAWFRAME_EXPECT(refusedWith(decode(same), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(decode(std::span{kBytes}.first(kBytes.size() - 1)), MaterialError::Invalid));

    // No default, inputs of two types, a switch of the wrong type for its
    // input, and a literal out of range at one quality are refused.
    for (const graph::Document& kBad : {surfaceOf(objectOf({{"specular_roughness", from(kRough, "out")}}),
                                                  {kSwitchOf(kRough, {{"low", Value::real(0.3)}})}),
                                        surfaceOf(objectOf({{"specular_roughness", from(kRough, "out")}}),
                                                  {kSwitchOf(kRough, {{"default", Value::real(0.8)}, {"low", kGrey}})}),
                                        surfaceOf(objectOf({{"base_color", from(kRough, "out")}}),
                                                  {kSwitchOf(kRough, {{"default", Value::real(0.8)}})})}) {
        RAWFRAME_EXPECT(refusedWith(compile(kBad), MaterialError::Invalid));
    }
    const graph::Document kRange =
        surfaceOf(objectOf({{"specular_roughness", from(kRough, "out")}}),
                  {kSwitchOf(kRough, {{"default", Value::real(0.8)}, {"low", Value::real(2)}})});
    RAWFRAME_EXPECT(compile(kRange).has_value() && refusedWith(compile(kRange, Quality::Low), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(compileQualities(kRange), MaterialError::Invalid));
}

RAWFRAME_TEST(WhatTheBlobCannotFoldIsWrittenAsSlang) {
    // A texture's color times its own alpha, and the roughness that alpha
    // too: the blob folds neither, the generated material says both,
    // sampling the texture once, bound at the base slot (D483).
    const graph::Document kSurface =
        surfaceOf(objectOf({{"base_color", from(kNode + 3, "out")}, {"specular_roughness", from(kSampler, "alpha")}}),
                  {samplerOf({}), mathOf(kNode + 3, kMultiplyType, from(kSampler, "color"), from(kSampler, "alpha"))});
    RAWFRAME_EXPECT(refusedWith(compile(kSurface), MaterialError::Unsupported));
    const auto kMade = generateSlang(kSurface);
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return;
    }
    const std::string& kSource = kMade->source;
    RAWFRAME_EXPECT(kSource.contains("import material;") &&
                    kSource.contains("const float4 v0 = baseTexture.Sample(baseSampler, point.uv);") &&
                    kSource.contains("const float3 v1 = v0.rgb * v0.a;") &&
                    kSource.contains("made.color = point.color.rgb * (v1);") &&
                    kSource.contains("made.roughness = v0.a;") && kSource.contains("made.metalness = 0.0;") &&
                    kSource.contains("made.normal = point.normal;") &&
                    kSource.contains("export struct Material : IMaterial = Generated;"));
    RAWFRAME_EXPECT(kMade->textures.size() == 1 && kMade->textures[0].id == 0xa44ecb4a39ac5cc8ULL);
    std::size_t sampled = 0;
    for (std::size_t at = kSource.find(".Sample("); at != std::string::npos; at = kSource.find(".Sample(", at + 1)) {
        ++sampled;
    }
    RAWFRAME_EXPECT(sampled == 1);
    // Coordinates past a mesh's first are refused, as the blob refuses them.
    RAWFRAME_EXPECT(refusedWith(generateSlang(sampledAt(1)), MaterialError::Unsupported));
}

RAWFRAME_TEST(AProgramMaterialRoundTripsAndRefusesWhatItWouldNotWrite) {
    // States, two textures at their slots, and a scene's and a shadow's
    // container a backend (D484, D488).
    ProgramMaterial made{.shading = Shading::Unlit, .blend = Blend::Masked, .alphaCutoff = 0.25F, .doubleSided = true};
    made.textures = {{.id = 0xa44ecb4a39ac5cc8ULL}, {.id = 7, .filter = Filter::Nearest, .address = Address::Clamp}};
    for (std::size_t at = 0; at < made.containers.size(); ++at) {
        made.containers.at(at) = {std::byte{'M'}, std::byte{'R'}, std::byte{'S'}, std::byte{'C'}, std::byte(at)};
        made.shadows.at(at) = {std::byte{'M'}, std::byte{'R'}, std::byte{'S'}, std::byte{'C'}, std::byte(at + 3)};
    }
    const std::vector<std::byte> kBytes = encodeProgram(made);
    const auto kRead = decodeProgram(kBytes);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == made);
    // Every prefix, a byte more, a container not Maul RHI's or none, a
    // texture naming none, a fifth texture, and states out of their sets.
    for (std::size_t length = 0; length < kBytes.size(); ++length) {
        RAWFRAME_EXPECT(refusedWith(decodeProgram(std::span{kBytes}.first(length)), MaterialError::Invalid));
    }
    std::vector<std::byte> longer = kBytes;
    longer.push_back(std::byte{0});
    RAWFRAME_EXPECT(refusedWith(decodeProgram(longer), MaterialError::Invalid));
    ProgramMaterial bad = made;
    bad.containers[1][0] = std::byte{'X'};
    RAWFRAME_EXPECT(refusedWith(decodeProgram(encodeProgram(bad)), MaterialError::Invalid));
    bad = made;
    bad.shadows[2].clear();
    RAWFRAME_EXPECT(refusedWith(decodeProgram(encodeProgram(bad)), MaterialError::Invalid));
    // Format 1, without the shadow's, is read no more.
    std::vector<std::byte> older = kBytes;
    older[4] = std::byte{1};
    RAWFRAME_EXPECT(refusedWith(decodeProgram(older), MaterialError::Invalid));
    bad = made;
    bad.textures[1].id = 0;
    RAWFRAME_EXPECT(refusedWith(decodeProgram(encodeProgram(bad)), MaterialError::Invalid));
    bad = made;
    bad.textures.resize(5, SampledTexture{.id = 1});
    RAWFRAME_EXPECT(refusedWith(decodeProgram(encodeProgram(bad)), MaterialError::Invalid));
    std::vector<std::byte> blended = kBytes;
    blended[9] = std::byte{3};
    RAWFRAME_EXPECT(refusedWith(decodeProgram(blended), MaterialError::Invalid));
}

RAWFRAME_TEST(AGraphIsWrittenAsTheSlangItsHandwrittenTwinIsCheckedAgainst) {
    // ADR-0026's proof (D490): faded.material, which the blob cannot fold,
    // is written as faded_graph.slang word for word; the scene's tests
    // build that and faded.slang, written by hand, through one pipeline
    // and find the same pixels. A change to the writer shows here first.
    const std::string_view kDocument{reinterpret_cast<const char*>(kFadedMaterial.data()), kFadedMaterial.size()};
    const auto kRead = readMaterial(kDocument);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(refusedWith(compileQualities(*kRead), MaterialError::Unsupported));
    const auto kWritten = generateSlang(*kRead);
    const std::string_view kGolden{reinterpret_cast<const char*>(kFadedGraph.data()), kFadedGraph.size()};
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->source == kGolden);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->textures.size() == 1 &&
                    kWritten->textures[0].id == 0xa44ecb4a39ac5cc8ULL);
}
