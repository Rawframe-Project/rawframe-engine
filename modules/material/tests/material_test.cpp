// Surface materials: one canonical text on the graph grammar, SPEC-0026's
// Surface contract held (shapes, ranges, defaults left out, states), a
// literal surface compiled to its blob, a sampled texture feeding its base
// color and opacity, and what generation 1 cannot compile refused, never
// guessed at.

#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <iterator>
#include <string>
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
    RAWFRAME_EXPECT(kBytes.size() == 92 && kDecoded.has_value() && *kDecoded == red());
    // Anything else is refused: short, another format, a value out of the
    // contract's range, a state out of its set.
    RAWFRAME_EXPECT(refusedWith(decode(std::span{kBytes}.first(91)), MaterialError::Invalid));
    std::vector<std::byte> other = kBytes;
    other[4] = std::byte{1};
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
    made.surface.specularRoughness = 0.5F;
    made.texture = {.id = 0xa44ecb4a39ac5cc8ULL,
                    .filter = Filter::Nearest,
                    .address = Address::Clamp,
                    .color = true,
                    .alpha = true};
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
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == stencilled());
    const std::array<float, kBlobFloats> kBlob = blobOf(stencilled());
    RAWFRAME_EXPECT(kBlob[0] == 1 && kBlob[1] == 1 && kBlob[2] == 1 && kBlob[7] == 0.5F && kBlob[15] == 6);
    // Its alpha alone: the literal color stays.
    Material masked{.blend = Blend::Masked};
    masked.surface.baseColor = {0.2F, 0.3F, 0.4F};
    masked.texture = {.id = 7, .alpha = true};
    const auto kMasked = compile(documentOf(masked, kNode));
    RAWFRAME_EXPECT(kMasked.has_value() && *kMasked == masked && blobOf(masked)[0] == 0.2F && blobOf(masked)[15] == 4);
    // Sampled at a mesh's first coordinates, named or not; the hash follows
    // the texture.
    const auto kNamed = compile(sampledAt(0));
    RAWFRAME_EXPECT(kNamed.has_value() && kNamed->texture.color && !kNamed->texture.alpha);
    const auto kHash = semanticHash(documentOf(stencilled(), kNode));
    Material other = stencilled();
    other.texture.id = 8;
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
    const graph::Document kRough =
        surfaceOf(objectOf({{"specular_roughness", from(kSampler, "alpha")}}), {samplerOf({})});
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
    feedsNothing[90] = std::byte{0};
    RAWFRAME_EXPECT(refusedWith(decode(feedsNothing), MaterialError::Invalid));
    std::vector<std::byte> stateless = encode(red());
    stateless[88] = std::byte{1};
    RAWFRAME_EXPECT(refusedWith(decode(stateless), MaterialError::Invalid));
}
