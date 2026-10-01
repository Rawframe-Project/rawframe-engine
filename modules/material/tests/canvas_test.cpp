// Canvas materials (D355): one canonical text on the graph grammar, the
// domain's contract held (a color4 color, a color3 emission, the states in
// their sets, each left out at its default), what folds compiled to its
// constants and texture terms, cooked and read back, and what generation
// 1 cannot fold refused, never guessed at.

#include "rawframe/material/canvas.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/post_process.h"
#include "rawframe/test/test.h"

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

/// A canvas material's document, its nodes added by id.
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

    Built& state(std::string_view name, std::string_view value) {
        states_[std::string{name}] = std::string{value};
        return *this;
    }

    [[nodiscard]] graph::Document made() const {
        graph::Document document{.kind = "canvas"};
        for (const auto& [kId, kRecord] : nodes_) {
            document.nodes.push_back(graph::Node{.id = kId, .record = kRecord});
        }
        if (!states_.empty()) {
            Value states = Value::object();
            for (const auto& [kName, kValue] : states_) {
                states.add(kName, Value::string(kValue));
            }
            document.sections.emplace_back("states", std::move(states));
        }
        return document;
    }

private:
    std::map<graph::NodeId, Value> nodes_;
    std::map<std::string, std::string> states_;
};

constexpr graph::NodeId kOut = 0x2f00000000000001ULL;
constexpr graph::NodeId kSample = 0x1f00000000000001ULL;
constexpr graph::NodeId kCombine = 0x1f00000000000002ULL;
constexpr graph::NodeId kTint = 0x1f00000000000003ULL;
constexpr graph::NodeId kUv = 0x1f00000000000004ULL;
constexpr graph::NodeId kTile = 0x1f00000000000005ULL;
constexpr graph::NodeId kOther = 0x1f00000000000006ULL;
constexpr std::uint64_t kSheet = 0x9f822820a44af4fcULL;

std::pair<std::string, Value> texture(std::uint64_t id) {
    return {"texture", Value::string(hexOf(id))};
}

/// A sheet's texture, its color and alpha, tinted red, tiled twice,
/// glowing faintly, added.
Built glowing() {
    Built made;
    made.node(kUv, kUvType)
        .node(kTile, kMultiplyType, {{"a", from(kUv, "uv")}, {"b", numbers({2, 2})}})
        .node(kSample, kSampleTexture2dType, {{"uv", from(kTile, "out")}}, {texture(kSheet)})
        .node(kCombine, kCombine4Type, {{"in1", from(kSample, "color")}, {"in2", from(kSample, "alpha")}})
        .node(kTint, kMultiplyType, {{"a", from(kCombine, "out")}, {"b", numbers({1, 0.5, 0.5, 1})}})
        .node(kOut, kCanvasOutputType, {{"color", from(kTint, "out")}, {"emission", numbers({0.2, 0.1, 0})}})
        .state("blend", "additive");
    return made;
}

} // namespace

RAWFRAME_TEST(ACanvasMaterialReadsBackAndFoldsToItsForm) {
    const auto kText = writeCanvas(glowing().made());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    const auto kRead = readCanvas(*kText);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const auto kMade = compileCanvas(*kRead);
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kMade->shading == Shading::Lit && kMade->blend == CanvasBlend::Additive);
    RAWFRAME_EXPECT((kMade->color == std::array<float, 4>{0, 0, 0, 0}) &&
                    (kMade->colorTexture == std::array<float, 4>{1, 0.5F, 0.5F, 1}));
    RAWFRAME_EXPECT((kMade->emission == std::array<float, 3>{0.2F, 0.1F, 0}) &&
                    (kMade->emissionTexture == std::array<float, 3>{0, 0, 0}));
    RAWFRAME_EXPECT(kMade->sampled.id == kSheet && (kMade->sampled.scale == std::array<float, 2>{2, 2}));
    // Cooked and read back as it was; bytes it would not make refused.
    const std::vector<std::byte> kBytes = encodeCanvas(*kMade);
    const auto kDecoded = decodeCanvas(kBytes);
    RAWFRAME_EXPECT(kDecoded.has_value() && *kDecoded == *kMade);
    std::vector<std::byte> broken = kBytes;
    broken[9] = std::byte{7};
    RAWFRAME_EXPECT(refusedWith(decodeCanvas(broken), MaterialError::Invalid));
    RAWFRAME_EXPECT(refusedWith(decodeCanvas(std::span{kBytes}.first(20)), MaterialError::Invalid));
    // Anything but the canonical text is refused.
    RAWFRAME_EXPECT(refusedWith(readCanvas(*kText + " "), MaterialError::Invalid));
    // Nothing given: white, giving off nothing, over what is behind.
    Built plain;
    plain.node(kOut, kCanvasOutputType).state("shading", "unlit");
    const auto kPlain = compileCanvas(plain.made());
    RAWFRAME_EXPECT(kPlain.has_value() && *kPlain == (CanvasMaterial{.shading = Shading::Unlit}));
}

RAWFRAME_TEST(ACanvasMaterialOutOfItsContractIsRefused) {
    const auto kInvalid = [](const Built& built) {
        return refusedWith(validateCanvas(built.made()), MaterialError::Invalid);
    };
    const auto kUnsupported = [](const Built& built) {
        return refusedWith(compileCanvas(built.made()), MaterialError::Unsupported);
    };
    // A color at its default, an emission below nought, a color3 color, a
    // state at its default or out of its set, two outputs.
    Built white;
    white.node(kOut, kCanvasOutputType, {{"color", numbers({1, 1, 1, 1})}});
    RAWFRAME_EXPECT(kInvalid(white));
    Built dark;
    dark.node(kOut, kCanvasOutputType, {{"emission", numbers({-1, 0, 0})}});
    RAWFRAME_EXPECT(kInvalid(dark));
    Built flat;
    flat.node(kSample, kSampleTexture2dType, {}, {texture(kSheet)})
        .node(kOut, kCanvasOutputType, {{"color", from(kSample, "color")}});
    RAWFRAME_EXPECT(kInvalid(flat));
    Built normal;
    normal.node(kOut, kCanvasOutputType).state("blend", "normal");
    RAWFRAME_EXPECT(kInvalid(normal));
    Built odd;
    odd.node(kOut, kCanvasOutputType).state("blend", "screen");
    RAWFRAME_EXPECT(kInvalid(odd));
    Built twice;
    twice.node(kOut, kCanvasOutputType).node(kOther, kCanvasOutputType);
    RAWFRAME_EXPECT(kInvalid(twice));
    // A post process's picture is no canvas's node: kept whole, and not
    // folded; nor a normal map, until 2D lights; nor two textures.
    Built picture;
    picture.node(kSample, kSceneColorType).node(kOut, kCanvasOutputType, {{"color", from(kSample, "color")}});
    RAWFRAME_EXPECT(validateCanvas(picture.made()).has_value() && kUnsupported(picture));
    Built bumped;
    bumped.node(kSample, kSampleTexture2dType, {}, {texture(kSheet)})
        .node(kOut, kCanvasOutputType, {{"normal_map", from(kSample, "color")}});
    RAWFRAME_EXPECT(validateCanvas(bumped.made()).has_value() && kUnsupported(bumped));
    Built two;
    two.node(kSample, kSampleTexture2dType, {}, {texture(kSheet)})
        .node(kOther, kSampleTexture2dType, {}, {texture(kSheet + 1)})
        .node(kTint, kAddType, {{"a", from(kSample, "color")}, {"b", from(kOther, "color")}})
        .node(kOut, kCanvasOutputType, {{"emission", from(kTint, "out")}});
    RAWFRAME_EXPECT(validateCanvas(two.made()).has_value() && kUnsupported(two));
    // Nor is a post process a canvas material.
    Built process;
    process.node(kOut, kPostProcessOutputType);
    graph::Document other = process.made();
    other.kind = "post_process";
    RAWFRAME_EXPECT(refusedWith(validateCanvas(other), MaterialError::Invalid));
}
