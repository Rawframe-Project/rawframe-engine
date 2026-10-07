// A surface material written as Slang (D483): the graph's nodes as code,
// each output once, the Surface's inputs gathered into what the scene's
// `IMaterial` gives (modules/render_scene_gpu/shaders/material.slang).

#include "common.h"
#include "parameters.h"
#include "rawframe/material/material.h"
#include "surface_graph.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::material {

namespace {

using document::Value;
using surface_graph::Carried;

/// A number as Slang reads a float: its shortest text, with a point.
std::string numberText(double value) {
    std::array<char, 32> text{};
    const auto [kEnd, kWritten] = std::to_chars(text.data(), text.data() + text.size(), static_cast<float>(value));
    static_cast<void>(kWritten);
    std::string made{text.data(), kEnd};
    if (made.find_first_of(".e") == std::string::npos) {
        made += ".0";
    }
    return made;
}

std::string_view slangType(Carried carried) {
    switch (carried) {
    case Carried::Float:
        return "float";
    case Carried::Vec2:
        return "float2";
    case Carried::Color3:
    case Carried::Vec3:
        return "float3";
    }
    return "float";
}

/// A literal of `count` numbers, a float for one.
std::string literalText(const std::array<double, 3>& literal, std::size_t count) {
    if (count == 1) {
        return numberText(literal[0]);
    }
    std::string made = count == 2 ? "float2(" : "float3(";
    for (std::size_t at = 0; at < count; ++at) {
        made += (at == 0 ? "" : ", ") + numberText(literal.at(at));
    }
    return made + ")";
}

std::size_t channelsOf(Carried carried) {
    return carried == Carried::Float ? 1 : carried == Carried::Vec2 ? 2 : 3;
}

/// The slots a generated material binds its textures at, in order.
constexpr std::array<std::string_view, 4> kSlots = {"base", "packed", "emission", "normal"};

/// Which of a quality switch's inputs a quality takes (`kSwitchInputs`'
/// order: default, high, low, medium).
std::size_t switchInputOf(Quality quality) {
    return quality == Quality::High ? 1 : quality == Quality::Low ? 2 : 3;
}

/// The code a graph is written as: its statements, each node output's
/// expression once made, and the textures bound so far.
class Writer {
public:
    Writer(const graph::Document& surface, Quality quality) : surface_(surface), quality_(quality) {
    }

    /// The expression of the output a connection names, its node's code
    /// written first if it is not yet.
    result::Result<std::string> outputOf(const graph::Connection& from) {
        const graph::Node* kNode = nodeOf(surface_, from.node);
        if (kNode == nullptr || !surface_graph::known(*kNode)) {
            return unsupported("a generated material's nodes are of the types this family knows");
        }
        const std::string_view kType = surface_graph::typeOf(*kNode);
        if (kType == kSampleTexture2dType) {
            RAWFRAME_TRY_ASSIGN(const std::string kTexel, texelOf(*kNode, from.node));
            return kTexel + (from.output == "alpha" ? ".a" : ".rgb");
        }
        if (kType == kSeparate3Type) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Operand kIn, surface_graph::separate3In(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kInText, operandText(kIn));
            return "(" + kInText + ")." + from.output;
        }
        if (kType == kUvType) {
            RAWFRAME_TRY_ASSIGN(const std::uint32_t kChannel, surface_graph::uvIn(*kNode));
            if (kChannel != 0) {
                return unsupported("a generated material reads a mesh's first coordinates only");
            }
            return std::string{"point.uv"};
        }
        if (const auto kMade = made_.find(from.node); kMade != made_.end()) {
            return kMade->second;
        }
        std::string expression;
        Carried carried = Carried::Float;
        if (surface_graph::math(kType)) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Math kMath, surface_graph::mathIn(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kA, operandText(kMath.a));
            RAWFRAME_TRY_ASSIGN(const std::string kB, operandText(kMath.b));
            expression = kA + (kType == kMultiplyType ? " * " : " + ") + kB;
            carried = kMath.out.value_or(Carried::Float);
        } else if (kType == kQualitySwitchType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Switch kSwitch, surface_graph::switchIn(surface_, *kNode));
            const std::optional<surface_graph::Operand>& kChosen = kSwitch.inputs.at(switchInputOf(quality_));
            RAWFRAME_TRY_ASSIGN(expression, operandText(kChosen.has_value() ? *kChosen : *kSwitch.inputs[0]));
            carried = kSwitch.out.value_or(Carried::Float);
        } else if (kType == kNormalMapType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::NormalMap kMap, surface_graph::normalMapIn(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kIn, operandText(kMap.in));
            // The tangent-space normal across the tangent frame (D313), its
            // x and y scaled, as the blob's normal texture bends it.
            const std::string kName = name();
            statements_ += "        float3 " + kName + "Bent = " + kIn + " * 2.0 - 1.0;\n";
            statements_ += "        " + kName + "Bent.xy *= " + numberText(kMap.scale) + ";\n";
            statements_ += "        const float3 " + kName +
                           "Across = normalize(point.tangent.xyz - point.normal * dot(point.normal, "
                           "point.tangent.xyz));\n";
            statements_ +=
                "        const float3 " + kName + "Up = cross(point.normal, " + kName + "Across) * point.tangent.w;\n";
            expression = "normalize(" + kName + "Across * " + kName + "Bent.x + " + kName + "Up * " + kName +
                         "Bent.y + point.normal * " + kName + "Bent.z)";
            carried = Carried::Vec3;
        } else {
            return unsupported("a generated material's nodes are of the types this family knows");
        }
        const std::string kName = name();
        statements_ += "        const " + std::string{slangType(carried)} + " " + kName + " = " + expression + ";\n";
        made_.emplace(from.node, kName);
        return kName;
    }

    [[nodiscard]] const std::string& statements() const noexcept {
        return statements_;
    }

    [[nodiscard]] std::vector<SampledTexture> takeTextures() {
        return std::move(textures_);
    }

private:
    result::Result<std::string> operandText(const surface_graph::Operand& operand) {
        if (operand.from.has_value()) {
            return outputOf(*operand.from);
        }
        return literalText(operand.literal, channelsOf(operand.carried.value_or(Carried::Float)));
    }

    /// A sampled texture's texel, sampled once a node: the texture bound
    /// at the next slot, or the slot it was bound at already.
    result::Result<std::string> texelOf(const graph::Node& node, graph::NodeId id) {
        if (const auto kMade = made_.find(id); kMade != made_.end()) {
            return kMade->second;
        }
        RAWFRAME_TRY_ASSIGN(const surface_graph::Sampling kSampling, surface_graph::sampleIn(surface_, node));
        std::string uv = "point.uv";
        if (kSampling.uv.has_value()) {
            RAWFRAME_TRY_ASSIGN(uv, outputOf(*kSampling.uv));
        }
        std::size_t slot = 0;
        while (slot < textures_.size() && !(textures_[slot] == kSampling.texture)) {
            ++slot;
        }
        if (slot == textures_.size()) {
            if (textures_.size() == kSlots.size()) {
                return unsupported("a generated material samples at most four textures");
            }
            textures_.push_back(kSampling.texture);
        }
        const std::string kSlot{kSlots.at(slot)};
        const std::string kName = name();
        statements_ +=
            "        const float4 " + kName + " = " + kSlot + "Texture.Sample(" + kSlot + "Sampler, " + uv + ");\n";
        made_.emplace(id, kName);
        return kName;
    }

    std::string name() {
        return "v" + std::to_string(next_++);
    }

    const graph::Document& surface_;
    Quality quality_;
    std::string statements_;
    std::map<graph::NodeId, std::string> made_;
    std::vector<SampledTexture> textures_;
    std::size_t next_ = 0;
};

} // namespace

result::Result<GeneratedSlang> generateSlang(const graph::Document& surface, Quality quality) {
    RAWFRAME_TRY(validateSurface(surface));
    RAWFRAME_TRY_ASSIGN(const Material kStates, surface_graph::statesIn(surface_graph::statesOf(surface)));
    const graph::Node& kSurface = *surface_graph::surfaceNode(surface);
    const Value& kInputs = *kSurface.record.find("inputs");
    Writer writer{surface, quality};
    // Each core parameter's expression: its literal, what feeds it, or its
    // default.
    std::map<std::string_view, std::string> inputs;
    for (const Parameter& kParameter : kParameters) {
        std::array<double, 3> initial{};
        initial.fill(kParameter.initial);
        inputs.emplace(kParameter.name, literalText(initial, kParameter.channels));
    }
    std::string normal = "point.normal";
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::string& kName = kInputs.names()[at];
        const Value& kValue = kInputs.items()[at];
        std::string expression;
        if (const std::optional<graph::Connection> kFrom = graph::connectionOf(kValue); kFrom.has_value()) {
            RAWFRAME_TRY_ASSIGN(expression, writer.outputOf(*kFrom));
        } else {
            const Parameter& kParameter = *parameterNamed(kName);
            std::array<double, 3> literal{};
            for (std::size_t channel = 0; channel < kParameter.channels; ++channel) {
                literal.at(channel) = *(kParameter.channels == 1 ? kValue.real() : kValue.items()[channel].real());
            }
            expression = literalText(literal, kParameter.channels);
        }
        if (kName == "geometry_normal") {
            normal = expression;
        } else {
            inputs[kName] = expression;
        }
    }
    GeneratedSlang made;
    made.source = "// A surface material written by rawframe.material (D483); generated, never edited.\n"
                  "import material;\n\n"
                  "struct Generated : IMaterial\n"
                  "{\n"
                  "    static Evaluated evaluate(Point point)\n"
                  "    {\n" +
                  writer.statements() +
                  "        Evaluated made;\n"
                  "        made.color = point.color.rgb * (" +
                  inputs.at("base_color") + ");\n        made.metalness = " + inputs.at("base_metalness") +
                  ";\n        made.specular = (" + inputs.at("specular_color") + ") * (" +
                  inputs.at("specular_weight") + ");\n        made.roughness = " + inputs.at("specular_roughness") +
                  ";\n        made.glowing = (" + inputs.at("emission_color") + ") * (" +
                  inputs.at("emission_luminance") + ");\n        made.ior = " + inputs.at("specular_ior") +
                  ";\n        made.opacity = point.color.a * (" + inputs.at("geometry_opacity") +
                  ");\n        made.occlusion = " + inputs.at("ambient_occlusion") +
                  ";\n        made.normal = " + normal +
                  ";\n        made.unlit = " + (kStates.shading == Shading::Unlit ? "true" : "false") +
                  ";\n"
                  "        return made;\n"
                  "    }\n"
                  "}\n\n"
                  "export struct Material : IMaterial = Generated;\n";
    made.textures = writer.takeTextures();
    return made;
}

} // namespace rawframe::material
