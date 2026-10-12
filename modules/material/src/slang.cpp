// A surface material written as Slang (D483): the graph's nodes as code,
// each output once, the Surface's inputs gathered into what the scene's
// `IMaterial` gives (modules/render_scene_gpu/shaders/material.slang):
// the fragment stage's surface, and the vertex stage's position offset
// (D691).

#include "common.h"
#include "functions.h"
#include "parameters.h"
#include "rawframe/material/material.h"
#include "surface_graph.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
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
    case Carried::Bool:
        return "bool";
    case Carried::Vec4:
    case Carried::Color4:
        return "float4";
    case Carried::Texture2d:
        return "Texture2D";
    case Carried::Int:
        return "int";
    }
    return "float";
}

/// A literal of `count` numbers, a float for one.
std::string literalText(const std::array<double, 4>& literal, std::size_t count) {
    if (count == 1) {
        return numberText(literal[0]);
    }
    std::string made = "float" + std::to_string(count) + "(";
    for (std::size_t at = 0; at < count; ++at) {
        made += (at == 0 ? "" : ", ") + numberText(literal.at(at));
    }
    return made + ")";
}

std::size_t channelsOf(Carried carried);

/// A literal of `carried`'s type: a bool's word, an int's digits (D715),
/// or `literalText`'s numbers.
std::string valueText(Carried carried, const std::array<double, 4>& literal) {
    if (carried == Carried::Bool) {
        return literal[0] != 0 ? "true" : "false";
    }
    if (carried == Carried::Int) {
        return std::to_string(static_cast<std::int64_t>(literal[0]));
    }
    return literalText(literal, channelsOf(carried));
}

std::size_t channelsOf(Carried carried) {
    return carried == Carried::Float || carried == Carried::Int || carried == Carried::Bool ? 1
           : carried == Carried::Vec2                                                       ? 2
           : carried == Carried::Vec4 || carried == Carried::Color4                         ? 4
                                                                                            : 3;
}

/// The slots a generated material binds its textures at, in order.
constexpr std::array<std::string_view, 4> kSlots = {"base", "packed", "emission", "normal"};

/// Which of a quality switch's inputs a quality takes (`kSwitchInputs`'
/// order: default, high, low, medium).
std::size_t switchInputOf(Quality quality) {
    return quality == Quality::High ? 1 : quality == Quality::Low ? 2 : 3;
}

/// Where code is written: the fragment stage's `evaluate`, at a `Point`,
/// or the vertex stage's `offset`, at a `Placed` (D691), where no texture
/// is sampled.
enum class Stage : std::uint8_t {
    Fragment,
    Vertex
};

/// What a graph's writer and the writers of the functions it calls write
/// together: the statements, the textures bound and the inputs giving
/// them, the helper functions called, and the next name.
struct Code {
    std::string statements;
    std::vector<SampledTexture> textures;
    std::array<std::uint8_t, 4> inputs{};
    std::vector<std::string_view> helpers;
    std::size_t next = 0;
};

class Writer;

/// A function called, and the writer writing its nodes in its caller's
/// code (D711).
struct Called {
    functions::Call call;
    std::unique_ptr<Writer> writer;
};

/// The code a graph is written as: its statements, each node output's
/// expression once made, and the textures bound so far.
class Writer {
public:
    Writer(const graph::Document& surface, Quality quality, Stage stage)
        : surface_(surface), quality_(quality), at_(stage == Stage::Fragment ? "point" : "placed"),
          own_(std::make_unique<Code>()), code_(own_.get()) {
    }

    /// A function's writer, adding to `caller`'s code, each of the
    /// function's inputs the expression `bindings` gives it (D711), each
    /// texture input the texture `textures` does (D713).
    Writer(const graph::Document& function,
           Writer& caller,
           std::map<std::string, std::string, std::less<>> bindings,
           std::vector<functions::GivenTexture> textures)
        : surface_(function), quality_(caller.quality_), at_(caller.at_), code_(caller.code_),
          bindings_(std::move(bindings)), textures_(std::move(textures)), bound_(true) {
    }

    /// The expression of the output a connection names, its node's code
    /// written first if it is not yet.
    result::Result<std::string> outputOf(const graph::Connection& from) {
        const graph::Node* kNode = nodeOf(surface_, from.node);
        if (kNode == nullptr || !surface_graph::known(*kNode)) {
            return unsupported("a generated material's nodes are of the types this family knows");
        }
        const std::string_view kType = surface_graph::typeOf(*kNode);
        if (at_ == "placed" && (kType == kSampleTexture2dType || kType == kNormalMapType)) {
            return unsupported("a material's vertex stage samples no texture and bends no normal in generation 1");
        }
        if (kType == kWorldPositionType) {
            return at_ + ".position";
        }
        if (kType == kWorldNormalType) {
            return at_ + ".normal";
        }
        if (kType == kViewDirectionType) {
            return at_ + ".view";
        }
        if (kType == kWorldTangentType) {
            // A vertex is placed before its tangent is turned with it.
            if (at_ == "placed") {
                return unsupported("a material's vertex stage reads no tangent in generation 1");
            }
            return std::string{"point.tangent.xyz"};
        }
        if (kType == kVertexColorType) {
            return at_ + (from.output == "alpha" ? ".color.a" : ".color.rgb");
        }
        if (kType == kSeparate2Type) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Operand kIn, surface_graph::separate2In(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kInText, operandText(kIn));
            return "(" + kInText + ")." + from.output;
        }
        if (kType == kSwizzleType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Swizzle kSwizzle, surface_graph::swizzleIn(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kInText, operandText(kSwizzle.in));
            return "(" + kInText + ")." + kSwizzle.pattern;
        }
        if (kType == kParameterType) {
            // Its vector after the material's blob's nine (D708); in a
            // function, what its caller gives it (D711).
            RAWFRAME_TRY_ASSIGN(const surface_graph::InterfaceInput kInput,
                                surface_graph::parameterIn(surface_, *kNode));
            if (bound_) {
                return "(" + bindings_.find(kInput.name)->second + ")";
            }
            constexpr std::array<std::string_view, 5> kWidths = {"", ".x", ".xy", ".xyz", ""};
            const std::string kRead = "materials[blobOf(" + at_ + ".material) + " + std::to_string(9 + kInput.slot) +
                                      "u]" + std::string{kWidths.at(channelsOf(kInput.carried))};
            // An int or a bool held as a float, exactly (D715).
            if (kInput.carried == Carried::Int) {
                return "int(" + kRead + ")";
            }
            if (kInput.carried == Carried::Bool) {
                return "(" + kRead + " != 0.0)";
            }
            return kRead;
        }
        if (functions::isFunction(kType)) {
            return calledOutput(*kNode, from);
        }
        if (kType == kFrontFacingType) {
            // A vertex has no face yet.
            if (at_ == "placed") {
                return unsupported("a material's vertex stage reads no face in generation 1");
            }
            return std::string{"point.front"};
        }
        if (kType == kConstantType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Operand kConstant, surface_graph::constantIn(*kNode));
            return operandText(kConstant);
        }
        if (kType == kTimeType) {
            // Whole seconds and the fraction left (D702): wrapped at a
            // whole period before they are added, so a material reads a
            // time days long as finely as one a minute long.
            RAWFRAME_TRY_ASSIGN(const std::uint32_t kPeriod, surface_graph::timeIn(*kNode));
            if (kPeriod == 0) {
                return "(" + at_ + ".time.x + " + at_ + ".time.y)";
            }
            return "(fmod(" + at_ + ".time.x, " + numberText(kPeriod) + ") + " + at_ + ".time.y)";
        }
        if (kType == kSampleTexture2dType) {
            RAWFRAME_TRY_ASSIGN(const std::string kTexel, texelOf(*kNode, from.node));
            return kTexel + (from.output == "alpha" ? ".a" : ".rgb");
        }
        if (kType == kSeparate4Type) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Operand kIn, surface_graph::separate4In(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kInText, operandText(kIn));
            return "(" + kInText + ")." + from.output;
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
            return at_ + ".uv";
        }
        if (const auto kMade = made_.find(from.node); kMade != made_.end()) {
            return kMade->second;
        }
        std::string expression;
        Carried carried = Carried::Float;
        if (surface_graph::math(kType)) {
            // The node's code with its inputs in place (D694): in a call, a
            // float input made the out's vector.
            const surface_graph::MathNode& kShape = *surface_graph::mathNodeOf(kType);
            RAWFRAME_TRY_ASSIGN(const surface_graph::MathInputs kMath, surface_graph::mathInputsOf(surface_, *kNode));
            carried = kMath.out.value_or(Carried::Float);
            // Over ints, the code an int takes (D715).
            expression = std::string{carried == Carried::Int && kShape.rule != surface_graph::Rule::Choice
                                         ? *surface_graph::wholeCodeOf(kType)
                                         : kShape.code};
            for (const std::string_view kHelper : kShape.helpers) {
                if (!kHelper.empty() && std::ranges::find(code_->helpers, kHelper) == code_->helpers.end()) {
                    code_->helpers.push_back(kHelper);
                }
            }
            for (std::size_t at = 0; at < kMath.count; ++at) {
                const surface_graph::Operand& kInput = kMath.inputs.at(at);
                RAWFRAME_TRY_ASSIGN(std::string text, operandText(kInput));
                if (kShape.call && carried != Carried::Float && kInput.carried == Carried::Float) {
                    text = std::string{slangType(carried)} + "(" + text + ")";
                }
                const std::string kPlace = "$" + std::to_string(at);
                for (std::size_t found = expression.find(kPlace); found != std::string::npos;
                     found = expression.find(kPlace, found + text.size())) {
                    expression.replace(found, kPlace.size(), text);
                }
            }
        } else if (kType == kCompareType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Compare kCompare, surface_graph::compareIn(surface_, *kNode));
            RAWFRAME_TRY_ASSIGN(const std::string kA, operandText(kCompare.a));
            RAWFRAME_TRY_ASSIGN(const std::string kB, operandText(kCompare.b));
            expression = kA + " " + std::string{kCompare.symbol} + " " + kB;
            carried = Carried::Bool;
        } else if (kType == kTransformType) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Transform kTransform,
                                surface_graph::transformIn(surface_, *kNode));
            // A vertex has no tangent frame yet.
            if (at_ == "placed" && (kTransform.from == "tangent" || kTransform.to == "tangent")) {
                return unsupported("a material's vertex stage reads no tangent in generation 1");
            }
            RAWFRAME_TRY_ASSIGN(const std::string kIn, operandText(kTransform.in));
            // Through the World's axes (D714): from the space given, then to
            // the one asked.
            const std::string kKind = kTransform.kind == "point"    ? "kPoint"
                                      : kTransform.kind == "vector" ? "kVector"
                                                                    : "kNormal";
            const std::string kSpaces = at_ + ".spaces";
            const std::string kFrame = at_ + ".normal, " + at_ + ".tangent";
            std::string world = kIn;
            if (kTransform.from == "object") {
                world = "fromObject(" + kSpaces + ", " + kIn + ", " + kKind + ")";
            } else if (kTransform.from == "view") {
                world = "fromView(" + kSpaces + ", " + kIn + ", " + kKind + ")";
            } else if (kTransform.from == "tangent") {
                world = "fromTangent(" + kFrame + ", " + kIn + ", " + kKind + ")";
            }
            expression = world;
            carried = Carried::Vec3;
            if (kTransform.to == "object") {
                expression = "toObject(" + kSpaces + ", " + world + ", " + kKind + ")";
            } else if (kTransform.to == "view") {
                expression = "toView(" + kSpaces + ", " + world + ", " + kKind + ")";
            } else if (kTransform.to == "tangent") {
                expression = "toTangent(" + kFrame + ", " + world + ", " + kKind + ")";
            } else if (kTransform.to == "clip") {
                expression = "toClip(" + kSpaces + ", " + world + ")";
                carried = Carried::Vec4;
            }
        } else if (kType == kCombine3Type || kType == kCombine2Type || kType == kCombine4Type) {
            RAWFRAME_TRY_ASSIGN(const surface_graph::Combined kParts, surface_graph::combineIn(surface_, *kNode));
            expression = "float" + std::to_string(kParts.count) + "(";
            for (std::size_t at = 0; at < kParts.count; ++at) {
                RAWFRAME_TRY_ASSIGN(const std::string kPart, operandText(kParts.parts.at(at)));
                expression += (at == 0 ? "" : ", ") + kPart;
            }
            expression += ")";
            carried = kParts.count == 2 ? Carried::Vec2 : kParts.count == 3 ? Carried::Vec3 : Carried::Vec4;
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
            code_->statements += "        float3 " + kName + "Bent = " + kIn + " * 2.0 - 1.0;\n";
            code_->statements += "        " + kName + "Bent.xy *= " + numberText(kMap.scale) + ";\n";
            code_->statements += "        const float3 " + kName +
                                 "Across = normalize(point.tangent.xyz - point.normal * dot(point.normal, "
                                 "point.tangent.xyz));\n";
            code_->statements +=
                "        const float3 " + kName + "Up = cross(point.normal, " + kName + "Across) * point.tangent.w;\n";
            expression = "normalize(" + kName + "Across * " + kName + "Bent.x + " + kName + "Up * " + kName +
                         "Bent.y + point.normal * " + kName + "Bent.z)";
            carried = Carried::Vec3;
        } else {
            return unsupported("a generated material's nodes are of the types this family knows");
        }
        const std::string kName = name();
        code_->statements +=
            "        const " + std::string{slangType(carried)} + " " + kName + " = " + expression + ";\n";
        made_.emplace(from.node, kName);
        return kName;
    }

    [[nodiscard]] const std::string& statements() const noexcept {
        return code_->statements;
    }

    /// The functions the written code calls, each before those that call
    /// it (D698, D699).
    [[nodiscard]] const std::vector<std::string_view>& helpers() const noexcept {
        return code_->helpers;
    }

    /// Which interface input gives each slot's texture, one more than its
    /// slot among the inputs, nought for a fixed texture (D710).
    [[nodiscard]] const std::array<std::uint8_t, 4>& textureInputs() const noexcept {
        return code_->inputs;
    }

    [[nodiscard]] std::vector<SampledTexture> takeTextures() {
        return std::move(code_->textures);
    }

private:
    result::Result<std::string> operandText(const surface_graph::Operand& operand) {
        if (operand.from.has_value()) {
            return outputOf(*operand.from);
        }
        return valueText(operand.carried.value_or(Carried::Float), operand.literal);
    }

    /// A sampled texture's texel, sampled once a node: the texture bound
    /// at the next slot, or the slot it was bound at already.
    result::Result<std::string> texelOf(const graph::Node& node, graph::NodeId id) {
        if (const auto kMade = made_.find(id); kMade != made_.end()) {
            return kMade->second;
        }
        RAWFRAME_TRY_ASSIGN(surface_graph::Sampling sampling, surface_graph::sampleIn(surface_, node));
        if (bound_ && sampling.input.has_value()) {
            // A function's texture input: the texture its call gives it
            // (D713).
            const functions::GivenTexture& kGiven = textures_.at(*sampling.input);
            sampling.texture.id = kGiven.id;
            sampling.input = kGiven.input;
        }
        const surface_graph::Sampling& kSampling = sampling;
        std::string uv = at_ + ".uv";
        if (kSampling.uv.has_value()) {
            RAWFRAME_TRY_ASSIGN(uv, outputOf(*kSampling.uv));
        }
        std::size_t slot = 0;
        // One slot a texture and sampler state, a fixed texture's apart from
        // an input's, which an instance may change (D710).
        const std::uint8_t kInput =
            kSampling.input.has_value() ? static_cast<std::uint8_t>(*kSampling.input + 1) : std::uint8_t{0};
        while (slot < code_->textures.size() &&
               !(code_->textures[slot] == kSampling.texture && code_->inputs.at(slot) == kInput)) {
            ++slot;
        }
        if (slot == code_->textures.size()) {
            if (code_->textures.size() == kSlots.size()) {
                return unsupported("a generated material samples at most four textures");
            }
            code_->textures.push_back(kSampling.texture);
            code_->inputs.at(slot) = kInput;
        }
        const std::string kSlot{kSlots.at(slot)};
        const std::string kName = name();
        code_->statements +=
            "        const float4 " + kName + " = " + kSlot + "Texture.Sample(" + kSlot + "Sampler, " + uv + ");\n";
        made_.emplace(id, kName);
        return kName;
    }

    /// A call's output: its function written in this code once a call, each
    /// of its inputs what the call gives it, its default, or the stream it
    /// defaults to (D711).
    result::Result<std::string> calledOutput(const graph::Node& node, const graph::Connection& from) {
        std::unique_ptr<Called>& called = calls_[from.node];
        if (called == nullptr) {
            RAWFRAME_TRY_ASSIGN(functions::Call call, functions::callIn(surface_, node));
            std::map<std::string, std::string, std::less<>> bindings;
            for (std::size_t at = 0; at < call.function.inputs.size(); ++at) {
                const surface_graph::InterfaceInput& kInput = call.function.inputs[at];
                std::string text;
                if (kInput.carried == Carried::Texture2d) {
                    continue;
                }
                if (call.given[at].has_value()) {
                    RAWFRAME_TRY_ASSIGN(text, operandText(*call.given[at]));
                } else if (!kInput.stream.empty()) {
                    constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kFields = {
                        {{"uv", ".uv"},
                         {"world_position", ".position"},
                         {"world_normal", ".normal"},
                         {"view_direction", ".view"}}};
                    text = at_ + std::string{std::ranges::find(kFields,
                                                               kInput.stream,
                                                               &std::pair<std::string_view, std::string_view>::first)
                                                 ->second};
                } else {
                    text = valueText(kInput.carried, kInput.initial);
                }
                bindings.emplace(std::string{kInput.name}, std::move(text));
            }
            called = std::make_unique<Called>(Called{.call = std::move(call)});
            called->writer = std::make_unique<Writer>(
                called->call.function.document, *this, std::move(bindings), called->call.textures);
        }
        const auto kOutput = std::ranges::find(called->call.function.outputs, from.output, &functions::Output::name);
        if (kOutput == called->call.function.outputs.end()) {
            return invalid("a connection names an output its node has");
        }
        return called->writer->operandText(kOutput->from);
    }

    std::string name() {
        return "v" + std::to_string(code_->next++);
    }

    const graph::Document& surface_;
    Quality quality_;
    /// What the stage's function names where it is written.
    std::string at_;
    std::map<graph::NodeId, std::string> made_;
    /// The root's code, which a function's writer adds to (D711).
    std::unique_ptr<Code> own_;
    Code* code_ = nullptr;
    /// A function's writer's inputs: each interface input's expression in
    /// its caller's code.
    std::map<std::string, std::string, std::less<>> bindings_;
    /// A function's writer's texture inputs' textures, by input slot.
    std::vector<functions::GivenTexture> textures_;
    bool bound_ = false;
    /// Each call's function and its writer, by the calling node.
    std::map<graph::NodeId, std::unique_ptr<Called>> calls_;
};

} // namespace

result::Result<GeneratedSlang> generateSlang(const graph::Document& surface, Quality quality) {
    RAWFRAME_TRY(validateSurface(surface));
    RAWFRAME_TRY_ASSIGN(const Material kStates, surface_graph::statesIn(surface_graph::statesOf(surface)));
    const graph::Node& kSurface = *surface_graph::surfaceNode(surface);
    const Value& kInputs = *kSurface.record.find("inputs");
    Writer writer{surface, quality, Stage::Fragment};
    Writer placing{surface, quality, Stage::Vertex};
    std::string offset = "float3(0.0)";
    // Each core parameter's expression: its literal, what feeds it, or its
    // default.
    std::map<std::string_view, std::string> inputs;
    for (const Parameter& kParameter : kParameters) {
        std::array<double, 4> initial{};
        initial.fill(kParameter.initial);
        inputs.emplace(kParameter.name, literalText(initial, kParameter.channels));
    }
    std::string normal = "point.normal";
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::string& kName = kInputs.names()[at];
        const Value& kValue = kInputs.items()[at];
        std::string expression;
        if (kName == "position_offset") {
            RAWFRAME_TRY_ASSIGN(offset, placing.outputOf(*graph::connectionOf(kValue)));
            continue;
        }
        if (const std::optional<graph::Connection> kFrom = graph::connectionOf(kValue); kFrom.has_value()) {
            RAWFRAME_TRY_ASSIGN(expression, writer.outputOf(*kFrom));
        } else {
            const Parameter& kParameter = *parameterNamed(kName);
            std::array<double, 4> literal{};
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
    // The functions either stage calls, once each, before the material.
    std::vector<std::string_view> helpers = writer.helpers();
    for (const std::string_view kHelper : placing.helpers()) {
        if (std::ranges::find(helpers, kHelper) == helpers.end()) {
            helpers.push_back(kHelper);
        }
    }
    std::string helped;
    for (const std::string_view kHelper : helpers) {
        helped += std::string{kHelper} + "\n";
    }
    made.source = "// A surface material written by rawframe.material (D483); generated, never edited.\n"
                  "import material;\n\n" +
                  helped +
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
                  "    }\n\n"
                  "    static float3 offset(Placed placed)\n"
                  "    {\n" +
                  placing.statements() + "        return " + offset +
                  ";\n"
                  "    }\n"
                  "}\n\n"
                  "export struct Material : IMaterial = Generated;\n";
    made.textureInputs = writer.textureInputs();
    made.textures = writer.takeTextures();
    made.moves = kInputs.find("position_offset") != nullptr;
    made.shading = kStates.shading;
    made.blend = kStates.blend;
    made.alphaCutoff = kStates.alphaCutoff;
    made.doubleSided = kStates.doubleSided;
    // Each interface input's default in its own vector (D708).
    RAWFRAME_TRY_ASSIGN(const std::vector<surface_graph::InterfaceInput> kInterface,
                        surface_graph::interfaceOf(surface));
    for (const surface_graph::InterfaceInput& kInput : kInterface) {
        for (std::size_t channel = 0; channel < channelsOf(kInput.carried); ++channel) {
            made.parameters.at((kInput.slot * 4) + channel) = static_cast<float>(kInput.initial.at(channel));
        }
    }
    return made;
}

} // namespace rawframe::material
