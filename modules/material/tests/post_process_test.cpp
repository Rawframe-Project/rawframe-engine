// Post-process materials (D348): one canonical text on the graph grammar,
// the domain's contract held (the output a color4, the insertion in its
// set, the domain's nodes and their types), what folds compiled to its
// constant, picture, texture, and both, cooked and read back, and what
// generation 1 cannot fold refused, never guessed at.

#include "rawframe/material/errors.h"
#include "rawframe/material/post_process.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

using namespace rawframe;
using namespace rawframe::material;
using document::Value;

namespace {

bool refusedWith(const auto& outcome, MaterialError error) {
    return !outcome.has_value() && outcome.error().domain() == kMaterialDomain && outcome.error().code() == code(error);
}

std::string hexOf(graph::NodeId id) {
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(id));
    return std::string{text.data()};
}

Value from(graph::NodeId node, std::string_view output) {
    Value made = Value::object();
    made.add("node", Value::string(hexOf(node)));
    made.add("output", Value::string(std::string{output}));
    return made;
}

Value numbers(std::initializer_list<double> values) {
    Value made = Value::array();
    for (const double kValue : values) {
        made.push(Value::real(kValue));
    }
    return made;
}

/// A post process's document, its nodes added by id.
class Built {
public:
    Built& node(graph::NodeId id,
                std::string_view type,
                std::vector<std::pair<std::string, Value>> inputs = {},
                std::vector<std::pair<std::string, Value>> params = {}) {
        Value record = Value::object();
        record.add("type", Value::string(std::string{type}));
        Value paramsValue = Value::object();
        for (auto& [kName, value] : params) {
            paramsValue.add(kName, std::move(value));
        }
        record.add("params", std::move(paramsValue));
        Value inputsValue = Value::object();
        for (auto& [kName, value] : inputs) {
            inputsValue.add(kName, std::move(value));
        }
        record.add("inputs", std::move(inputsValue));
        nodes_[id] = std::move(record);
        return *this;
    }

    Built& insertion(std::string_view at) {
        insertion_ = std::string{at};
        return *this;
    }

    [[nodiscard]] graph::Document made() const {
        graph::Document document{.kind = "post_process"};
        for (const auto& [kId, kRecord] : nodes_) {
            document.nodes.push_back(graph::Node{.id = kId, .record = kRecord});
        }
        if (!insertion_.empty()) {
            Value states = Value::object();
            states.add("insertion", Value::string(insertion_));
            document.sections.emplace_back("states", std::move(states));
        }
        return document;
    }

private:
    std::map<graph::NodeId, Value> nodes_;
    std::string insertion_;
};

constexpr graph::NodeId kOut = 0x2f00000000000001ULL;
constexpr graph::NodeId kScene = 0x1f00000000000001ULL;
constexpr graph::NodeId kMath = 0x1f00000000000002ULL;
constexpr graph::NodeId kSample = 0x1f00000000000003ULL;
constexpr graph::NodeId kCombine = 0x1f00000000000004ULL;
constexpr graph::NodeId kUv = 0x1f00000000000005ULL;
constexpr graph::NodeId kTile = 0x1f00000000000006ULL;
constexpr graph::NodeId kMove = 0x1f00000000000007ULL;
constexpr graph::NodeId kOther = 0x1f00000000000008ULL;

std::pair<std::string, Value> texture(std::uint64_t id) {
    return {"texture", Value::string(hexOf(id))};
}

/// The picture tinted warm before the tonemapper.
Built tinted() {
    Built made;
    made.node(kScene, kSceneColorType)
        .node(kMath, kMultiplyType, {{"a", from(kScene, "color")}, {"b", numbers({1, 0.9, 0.8, 1})}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kMath, "out")}})
        .insertion("before_tonemap");
    return made;
}

} // namespace

RAWFRAME_TEST(APostProcessReadsBackAndFoldsToItsForm) {
    const auto kText = writePostProcess(tinted().made());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kRead = readPostProcess(*kText);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kTinted = compilePostProcess(*kRead);
    RAWFRAME_EXPECT(kTinted.has_value());
    if (!kTinted.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kTinted->insertion == Insertion::BeforeTonemap);
    RAWFRAME_EXPECT((kTinted->constant == std::array<float, 4>{0, 0, 0, 1}) &&
                    (kTinted->scene == std::array<float, 3>{1, 0.9F, 0.8F}) && kTinted->sampled.id == 0);
    // Anything but the canonical text is refused.
    std::string loose = *kText;
    loose.insert(loose.find("\"kind\""), " ");
    RAWFRAME_EXPECT(!readPostProcess(loose).has_value());
    // A fade: a literal, half black over the picture, after the tonemapper.
    Built fade;
    fade.node(kOut, kPostProcessOutputType, {{"color", numbers({0, 0, 0, 0.5})}});
    const auto kFade = compilePostProcess(fade.made());
    RAWFRAME_EXPECT(kFade.has_value() && kFade->insertion == Insertion::AfterTonemap &&
                    (kFade->constant == std::array<float, 4>{0, 0, 0, 0.5F}) &&
                    (kFade->scene == std::array<float, 3>{0, 0, 0}));
    // A vignette: the picture times a texture's color, sampled at the
    // picture's coordinates.
    Built vignette;
    vignette.node(kScene, kSceneColorType)
        .node(kMath, kMultiplyType, {{"a", from(kScene, "color")}, {"b", from(kCombine, "out")}})
        .node(kSample, kSampleTexture2dType, {}, {texture(0x5eed)})
        .node(kCombine, kCombine4Type, {{"in1", from(kSample, "color")}, {"in2", Value::real(1)}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kMath, "out")}});
    const auto kVignette = compilePostProcess(vignette.made());
    RAWFRAME_EXPECT(kVignette.has_value() && (kVignette->both == std::array<float, 3>{1, 1, 1}) &&
                    (kVignette->constant == std::array<float, 4>{0, 0, 0, 1}) &&
                    (kVignette->scene == std::array<float, 3>{0, 0, 0}) && kVignette->sampled.id == 0x5eed);
    // An overlay by its own alpha, the texture tiled twice and moved.
    Built overlay;
    overlay
        .node(kSample,
              kSampleTexture2dType,
              {{"uv", from(kMove, "out")}},
              {{"address", Value::string("clamp")}, texture(0x5eed)})
        .node(kCombine, kCombine4Type, {{"in1", from(kSample, "color")}, {"in2", from(kSample, "alpha")}})
        .node(kUv, kScreenUvType)
        .node(kTile, kMultiplyType, {{"a", from(kUv, "uv")}, {"b", Value::real(2)}})
        .node(kMove, kAddType, {{"a", from(kTile, "out")}, {"b", numbers({0.5, 0.25})}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kCombine, "out")}})
        .insertion("scene_output");
    const auto kOverlay = compilePostProcess(overlay.made());
    RAWFRAME_EXPECT(kOverlay.has_value() && kOverlay->insertion == Insertion::SceneOutput &&
                    (kOverlay->texture == std::array<float, 4>{1, 1, 1, 1}) &&
                    (kOverlay->constant == std::array<float, 4>{0, 0, 0, 0}) &&
                    kOverlay->sampled.address == Address::Clamp &&
                    (kOverlay->sampled.scale == std::array<float, 2>{2, 2}) &&
                    (kOverlay->sampled.offset == std::array<float, 2>{0.5F, 0.25F}));
    // Cooked and read back as it was; its blob as the device reads it.
    for (const auto& kMade : {*kTinted, *kFade, *kVignette, *kOverlay}) {
        const auto kDecoded = decodePostProcess(encodePostProcess(kMade));
        RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == kMade);
    }
    const auto kBlob = blobOf(*kOverlay);
    RAWFRAME_EXPECT(kBlob[8] == 1 && kBlob[11] == 1 && kBlob[16] == 2 && kBlob[18] == 0.5F);
}

RAWFRAME_TEST(ThePostProcessContractIsHeld) {
    RAWFRAME_EXPECT(validatePostProcess(tinted().made()).has_value());
    // The default insertion written, or one outside the five.
    RAWFRAME_EXPECT(
        refusedWith(validatePostProcess(Built{tinted()}.insertion("after_tonemap").made()), MaterialError::Invalid));
    RAWFRAME_EXPECT(
        refusedWith(validatePostProcess(Built{tinted()}.insertion("before_ui").made()), MaterialError::Invalid));
    // The output a color4: not a color3, nor an alpha past one, nor the
    // default written; and there is one output.
    Built three;
    three.node(kOut, kPostProcessOutputType, {{"color", numbers({1, 0, 0})}});
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(three.made()), MaterialError::Invalid));
    Built bright;
    bright.node(kOut, kPostProcessOutputType, {{"color", numbers({1, 0, 0, 2})}});
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(bright.made()), MaterialError::Invalid));
    Built nothing;
    nothing.node(kOut, kPostProcessOutputType, {{"color", numbers({0, 0, 0, 0})}});
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(nothing.made()), MaterialError::Invalid));
    Built twice = tinted();
    twice.node(kOther, kPostProcessOutputType);
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(twice.made()), MaterialError::Invalid));
    // A texture's color straight into the color4 is a type mismatch.
    Built mismatched;
    mismatched.node(kSample, kSampleTexture2dType, {}, {texture(0x5eed)})
        .node(kOut, kPostProcessOutputType, {{"color", from(kSample, "color")}});
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(mismatched.made()), MaterialError::Invalid));
    // An output the node does not have.
    Built missing;
    missing.node(kScene, kSceneColorType).node(kOut, kPostProcessOutputType, {{"color", from(kScene, "alpha")}});
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(missing.made()), MaterialError::Invalid));
    // A surface document is not a post process.
    graph::Document surface = tinted().made();
    surface.kind = "surface";
    RAWFRAME_EXPECT(refusedWith(validatePostProcess(surface), MaterialError::Invalid));
}

RAWFRAME_TEST(WhatDoesNotFoldIsRefused) {
    // The picture times itself.
    Built squared;
    squared.node(kScene, kSceneColorType)
        .node(kMath, kMultiplyType, {{"a", from(kScene, "color")}, {"b", from(kScene, "color")}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kMath, "out")}});
    RAWFRAME_EXPECT(refusedWith(compilePostProcess(squared.made()), MaterialError::Unsupported));
    // The depth.
    Built deep;
    deep.node(kScene, kSceneDepthType)
        .node(kCombine, kCombine4Type, {{"in1", numbers({1, 1, 1})}, {"in2", from(kScene, "depth")}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kCombine, "out")}});
    RAWFRAME_EXPECT(validatePostProcess(deep.made()).has_value());
    RAWFRAME_EXPECT(refusedWith(compilePostProcess(deep.made()), MaterialError::Unsupported));
    // A second texture.
    Built two;
    two.node(kSample, kSampleTexture2dType, {}, {texture(0x5eed)})
        .node(kOther, kSampleTexture2dType, {}, {texture(0xbeef)})
        .node(kMath, kAddType, {{"a", from(kSample, "color")}, {"b", from(kOther, "color")}})
        .node(kCombine, kCombine4Type, {{"in1", from(kMath, "out")}, {"in2", Value::real(1)}})
        .node(kOut, kPostProcessOutputType, {{"color", from(kCombine, "out")}});
    RAWFRAME_EXPECT(refusedWith(compilePostProcess(two.made()), MaterialError::Unsupported));
    // The texture's alpha in a color's channels.
    Built faded;
    faded.node(kScene, kSceneColorType)
        .node(kMath, kMultiplyType, {{"a", from(kScene, "color")}, {"b", from(kSample, "alpha")}})
        .node(kSample, kSampleTexture2dType, {}, {texture(0x5eed)})
        .node(kOut, kPostProcessOutputType, {{"color", from(kMath, "out")}});
    RAWFRAME_EXPECT(refusedWith(compilePostProcess(faded.made()), MaterialError::Unsupported));
    // A node of a type it does not know: kept, and refused only where used.
    Built unknown = tinted();
    unknown.node(kOther, "studio/effects/glow@1");
    RAWFRAME_EXPECT(compilePostProcess(unknown.made()).has_value());
    Built used;
    used.node(kOther, "studio/effects/glow@1").node(kOut, kPostProcessOutputType, {{"color", from(kOther, "out")}});
    RAWFRAME_EXPECT(refusedWith(compilePostProcess(used.made()), MaterialError::Unsupported));
}

RAWFRAME_TEST(CookedPostProcessesAreCheckedAsTheyAreRead) {
    const auto kTinted = compilePostProcess(tinted().made());
    RAWFRAME_EXPECT(kTinted.has_value());
    if (!kTinted.has_value()) {
        return;
    }
    const std::vector<std::byte> kBytes = encodePostProcess(*kTinted);
    RAWFRAME_EXPECT(kBytes.size() == 104);
    // Another magic, a shorter body, an insertion past the five, a texture's
    // coefficient with no texture.
    for (const auto& [kAt, kValue] :
         {std::pair{0U, std::byte{'X'}}, std::pair{8U, std::byte{5}}, std::pair{12U + (8 * 4) + 3, std::byte{0x3F}}}) {
        std::vector<std::byte> broken = kBytes;
        broken[kAt] = kValue;
        RAWFRAME_EXPECT(refusedWith(decodePostProcess(broken), MaterialError::Invalid));
    }
    RAWFRAME_EXPECT(refusedWith(decodePostProcess(std::span{kBytes}.first(100)), MaterialError::Invalid));
}
