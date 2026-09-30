// Surface materials: one canonical text on the graph grammar, SPEC-0026's
// Surface contract held (shapes, ranges, defaults left out, states), a
// literal surface compiled to its blob, and what generation 1 cannot
// compile refused, never guessed at.

#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"
#include "rawframe/test/test.h"

#include <string>

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
    RAWFRAME_EXPECT(kBytes.size() == 80 && kDecoded.has_value() && *kDecoded == red());
    // Anything else is refused: short, another format, a value out of the
    // contract's range, a state out of its set.
    RAWFRAME_EXPECT(refusedWith(decode(std::span{kBytes}.first(79)), MaterialError::Invalid));
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
