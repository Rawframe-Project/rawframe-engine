#include "rawframe/material/material.h"

#include "common.h"
#include "parameters.h"
#include "rawframe/graph/errors.h"
#include "rawframe/material/errors.h"
#include "surface_graph.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace rawframe::material {

namespace {

using document::Value;
using surface_graph::Carried;
using surface_graph::carriedBy;
using surface_graph::kBlends;
using surface_graph::known;
using surface_graph::kShadings;
using surface_graph::kSwitchInputs;
using surface_graph::math;
using surface_graph::Math;
using surface_graph::mathIn;
using surface_graph::NormalMap;
using surface_graph::normalMapIn;
using surface_graph::Operand;
using surface_graph::operandOf;
using surface_graph::sampleIn;
using surface_graph::Sampling;
using surface_graph::separate3In;
using surface_graph::statesIn;
using surface_graph::statesOf;
using surface_graph::surfaceNode;
using surface_graph::Switch;
using surface_graph::switchIn;
using surface_graph::typeOf;
using surface_graph::uvIn;

/// A literal's numbers, if it is one of the parameter's shape in its
/// range.
std::optional<std::array<double, 3>> literalOf(const Parameter& parameter, const Value& value) {
    std::array<double, 3> made{};
    const auto kNumber = [&parameter](const Value& each) -> std::optional<double> {
        const std::optional<double> kReal = each.kind() == Value::Kind::Number ? each.real() : std::nullopt;
        if (!kReal.has_value() || !std::isfinite(*kReal) || *kReal < parameter.lowest || *kReal > parameter.highest) {
            return std::nullopt;
        }
        return kReal;
    };
    if (parameter.channels == 1) {
        const std::optional<double> kOne = kNumber(value);
        if (!kOne.has_value()) {
            return std::nullopt;
        }
        made[0] = *kOne;
        return made;
    }
    if (value.kind() != Value::Kind::Array || value.items().size() != 3) {
        return std::nullopt;
    }
    for (std::size_t at = 0; at < 3; ++at) {
        const std::optional<double> kChannel = kNumber(value.items()[at]);
        if (!kChannel.has_value()) {
            return std::nullopt;
        }
        made[at] = *kChannel;
    }
    return made;
}

/// A float as the double of its shortest text, so 0.8F is written 0.8.
Value numberOf(float value) {
    std::array<char, 32> text{};
    const auto [kEnd, kWritten] = std::to_chars(text.data(), text.data() + text.size(), value);
    double read = 0;
    const auto [kStop, kRead] = std::from_chars(text.data(), kEnd, read);
    static_cast<void>(kWritten);
    static_cast<void>(kStop);
    return Value::real(kRead == std::errc{} ? read : static_cast<double>(value));
}

/// The surface node's inputs, checked; a connected one is left at its
/// default and reported.
result::Result<bool> inputsIn(const graph::Node& node, Surface& into) {
    const Value* params = node.record.find("params");
    const Value* inputs = node.record.find("inputs");
    if (node.record.names().size() != 3 || params == nullptr || inputs == nullptr ||
        node.record.names()[1] != "params" || !params->names().empty()) {
        return invalid("a surface node is its type, no params, and its inputs");
    }
    bool connected = false;
    for (std::size_t at = 0; at < inputs->names().size(); ++at) {
        const Parameter* kParameter = parameterNamed(inputs->names()[at]);
        const bool kNormal = inputs->names()[at] == "geometry_normal";
        if ((kParameter == nullptr && !kNormal) || (at > 0 && !(inputs->names()[at - 1] < inputs->names()[at]))) {
            return invalid("a surface node's inputs are the Surface contract's core parameters, in name order");
        }
        if (graph::connectionOf(inputs->items()[at]).has_value()) {
            connected = true;
            continue;
        }
        if (kNormal) {
            return invalid("a surface's geometry normal is connected: a stream or a map, never a literal");
        }
        const std::optional<std::array<double, 3>> kLiteral = literalOf(*kParameter, inputs->items()[at]);
        if (!kLiteral.has_value()) {
            return invalid("a surface input's literal is its parameter's shape, in its range");
        }
        bool initial = true;
        float* place = kParameter->place(into);
        for (std::size_t channel = 0; channel < kParameter->channels; ++channel) {
            initial = initial && (*kLiteral)[channel] == kParameter->initial;
            place[channel] = static_cast<float>((*kLiteral)[channel]);
        }
        if (initial) {
            return invalid("a surface input at its default is left out");
        }
    }
    return connected;
}

/// A map of a mesh's first coordinates: what folding `multiply` and `add`
/// with literals down to a `uv` node gives (SPEC-0026's `tile_and_offset`).
struct Affine {
    std::array<double, 2> scale{1, 1};
    std::array<double, 2> offset{0, 0};
};

result::Result<Affine> affineOf(const graph::Document& surface, const graph::Connection& from) {
    const graph::Node& kNode = *nodeOf(surface, from.node);
    if (typeOf(kNode) == kUvType) {
        RAWFRAME_TRY_ASSIGN(const std::uint32_t kChannel, uvIn(kNode));
        if (kChannel != 0) {
            return unsupported("a mesh's texture coordinates past its first set wait for meshes to hold them");
        }
        return Affine{};
    }
    if (!math(typeOf(kNode))) {
        return unsupported("a texture is sampled at a mesh's coordinates, scaled and moved, in generation 1");
    }
    RAWFRAME_TRY_ASSIGN(const Math kMath, mathIn(surface, kNode));
    if (kMath.a.from.has_value() == kMath.b.from.has_value()) {
        return unsupported("a mesh's coordinates are scaled and moved by literals in generation 1");
    }
    const Operand& kConnected = kMath.a.from.has_value() ? kMath.a : kMath.b;
    const Operand& kLiteral = kMath.a.from.has_value() ? kMath.b : kMath.a;
    RAWFRAME_TRY_ASSIGN(Affine made, affineOf(surface, *kConnected.from));
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const double kBy = kLiteral.carried == Carried::Float ? kLiteral.literal[0] : kLiteral.literal[axis];
        if (typeOf(kNode) == kMultiplyType) {
            made.scale[axis] *= kBy;
            made.offset[axis] *= kBy;
        } else {
            made.offset[axis] += kBy;
        }
    }
    return made;
}

/// What feeds a surface input: a sampler's color, or one of its channels,
/// or that times a literal, the factor (one when none).
struct Fed {
    graph::NodeId sampler = 0;
    /// The color, or the channel.
    bool color = false;
    Channel channel = Channel::None;
    std::array<double, 3> factor{1, 1, 1};
};

result::Result<Fed> fedBy(const graph::Document& surface, const graph::Connection& from, bool factored = false) {
    const auto kRefused = [] {
        return unsupported("a surface input connected to anything but a sampled texture's color or channel, or that "
                           "times a literal, waits for the node library");
    };
    const graph::Node& kNode = *nodeOf(surface, from.node);
    const std::string_view kType = typeOf(kNode);
    if (kType == kSampleTexture2dType) {
        return Fed{.sampler = kNode.id,
                   .color = from.output == "color",
                   .channel = from.output == "alpha" ? Channel::Alpha : Channel::None};
    }
    if (kType == kSeparate3Type) {
        RAWFRAME_TRY_ASSIGN(const Operand kIn, separate3In(surface, kNode));
        if (!kIn.from.has_value() || kIn.from->output != "color" ||
            typeOf(*nodeOf(surface, kIn.from->node)) != kSampleTexture2dType) {
            return kRefused();
        }
        return Fed{.sampler = kIn.from->node,
                   .channel = from.output == "r"   ? Channel::Red
                              : from.output == "g" ? Channel::Green
                                                   : Channel::Blue};
    }
    if (kType != kMultiplyType || factored) {
        return kRefused();
    }
    RAWFRAME_TRY_ASSIGN(const Math kMath, mathIn(surface, kNode));
    if (kMath.a.from.has_value() == kMath.b.from.has_value()) {
        return kRefused();
    }
    const Operand& kConnected = kMath.a.from.has_value() ? kMath.a : kMath.b;
    const Operand& kLiteral = kMath.a.from.has_value() ? kMath.b : kMath.a;
    RAWFRAME_TRY_ASSIGN(Fed made, fedBy(surface, *kConnected.from, true));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        made.factor[channel] = kLiteral.carried == Carried::Float ? kLiteral.literal[0] : kLiteral.literal[channel];
    }
    return made;
}

} // namespace

graph::Document documentOf(const Material& made, graph::NodeId node) {
    const Surface kDefaults;
    Surface surface = made.surface;
    Surface initial = kDefaults;
    const Textures& kTextures = made.textures;
    const auto kRecord = [](std::string_view type, Value params, Value nodeInputs) {
        Value record = Value::object();
        record.add("type", Value::string(std::string{type}));
        record.add("params", std::move(params));
        record.add("inputs", std::move(nodeInputs));
        return record;
    };
    const auto kMath = [&kRecord](std::string_view type, Value a, Value b) {
        Value operands = Value::object();
        operands.add("a", std::move(a));
        operands.add("b", std::move(b));
        return kRecord(type, Value::object(), std::move(operands));
    };
    const auto kFrom = [](graph::NodeId id, std::string output) {
        return graph::connectionValue({.node = id, .output = std::move(output)});
    };
    // Each texture's nodes by the ids after the surface's, in order: its
    // sampler, the coordinates, their scale, their offset, and the packed
    // texture's channels; then each factor.
    graph::NodeId next = node + 1;
    std::vector<graph::Node> after;
    const auto kPlace = [&](const SampledTexture& texture, bool separated) {
        const graph::NodeId kSampler = next++;
        const bool kScaled = texture.scale != std::array<float, 2>{1, 1};
        const bool kMoved = texture.offset != std::array<float, 2>{0, 0};
        const graph::NodeId kUv = kScaled || kMoved ? next++ : 0;
        const graph::NodeId kScale = kScaled ? next++ : 0;
        const graph::NodeId kOffset = kMoved ? next++ : 0;
        Value params = Value::object();
        if (texture.address == Address::Clamp) {
            params.add("address", Value::string("clamp"));
        }
        if (texture.filter == Filter::Nearest) {
            params.add("filter", Value::string("nearest"));
        }
        params.add("texture", Value::string(graph::nodeIdText(texture.id)));
        Value sampled = Value::object();
        if (kScaled || kMoved) {
            sampled.add("uv", kFrom(kMoved ? kOffset : kScale, "out"));
        }
        after.push_back(
            {.id = kSampler, .record = kRecord(kSampleTexture2dType, std::move(params), std::move(sampled))});
        if (kScaled || kMoved) {
            after.push_back({.id = kUv, .record = kRecord(kUvType, Value::object(), Value::object())});
        }
        if (kScaled) {
            after.push_back({.id = kScale,
                             .record = kMath(kMultiplyType,
                                             kFrom(kUv, "uv"),
                                             Value::array({numberOf(texture.scale[0]), numberOf(texture.scale[1])}))});
        }
        if (kMoved) {
            after.push_back(
                {.id = kOffset,
                 .record = kMath(kAddType,
                                 kScaled ? kFrom(kScale, "out") : kFrom(kUv, "uv"),
                                 Value::array({numberOf(texture.offset[0]), numberOf(texture.offset[1])}))});
        }
        graph::NodeId separate = 0;
        if (separated) {
            separate = next++;
            Value in = Value::object();
            in.add("in", kFrom(kSampler, "color"));
            after.push_back({.id = separate, .record = kRecord(kSeparate3Type, Value::object(), std::move(in))});
        }
        return std::pair{kSampler, separate};
    };
    const auto kRgb = [](Channel channel) {
        return channel == Channel::Red || channel == Channel::Green || channel == Channel::Blue;
    };
    const auto [kBase, kUnused] =
        kTextures.base.id != 0 ? kPlace(kTextures.base, false) : std::pair<graph::NodeId, graph::NodeId>{};
    const auto [kPacked, kSeparate] =
        kTextures.packed.id != 0
            ? kPlace(kTextures.packed,
                     kRgb(kTextures.metalness) || kRgb(kTextures.roughness) || kRgb(kTextures.occlusion))
            : std::pair<graph::NodeId, graph::NodeId>{};
    const graph::NodeId kEmission = kTextures.emission.id != 0 ? kPlace(kTextures.emission, false).first : 0;
    static_cast<void>(kUnused);
    // The normal texture through its `normal_map` (D313).
    graph::NodeId normalMap = 0;
    if (kTextures.normal.id != 0) {
        const graph::NodeId kSampler = kPlace(kTextures.normal, false).first;
        normalMap = next++;
        Value params = Value::object();
        if (kTextures.normalScale != 1) {
            params.add("scale", numberOf(kTextures.normalScale));
        }
        Value in = Value::object();
        in.add("in", kFrom(kSampler, "color"));
        after.push_back({.id = normalMap, .record = kRecord(kNormalMapType, std::move(params), std::move(in))});
    }
    const auto kChannelOf = [](Channel channel) -> std::string {
        switch (channel) {
        case Channel::Red:
            return "r";
        case Channel::Green:
            return "g";
        case Channel::Blue:
            return "b";
        default:
            return "alpha";
        }
    };
    Value inputs = Value::object();
    for (const Parameter& parameter : kParameters) {
        const float* kValue = parameter.place(surface);
        const float* kInitial = parameter.place(initial);
        // What feeds it, if a texture does.
        std::optional<Value> fed;
        const std::string_view kName = parameter.name;
        // The geometry normal, in name order before the opacity.
        if (kName == "geometry_opacity" && normalMap != 0) {
            inputs.add("geometry_normal", kFrom(normalMap, "out"));
        }
        if (kBase != 0 && kName == "base_color" && kTextures.baseColor) {
            fed = kFrom(kBase, "color");
        } else if (kBase != 0 && kName == "geometry_opacity" && kTextures.opacity) {
            fed = kFrom(kBase, "alpha");
        } else if (kEmission != 0 && kName == "emission_color") {
            fed = kFrom(kEmission, "color");
        } else if (kPacked != 0) {
            const Channel kChannel = kName == "base_metalness"       ? kTextures.metalness
                                     : kName == "specular_roughness" ? kTextures.roughness
                                     : kName == "ambient_occlusion"  ? kTextures.occlusion
                                                                     : Channel::None;
            if (kChannel != Channel::None) {
                fed = kRgb(kChannel) ? kFrom(kSeparate, kChannelOf(kChannel)) : kFrom(kPacked, "alpha");
            }
        }
        if (fed.has_value()) {
            if (std::ranges::all_of(std::span{kValue, parameter.channels}, [](float value) {
                    return value == 1;
                })) {
                inputs.add(std::string{kName}, std::move(*fed));
            } else {
                const graph::NodeId kFactor = next++;
                after.push_back(
                    {.id = kFactor,
                     .record =
                         kMath(kMultiplyType,
                               std::move(*fed),
                               parameter.channels == 1
                                   ? numberOf(kValue[0])
                                   : Value::array({numberOf(kValue[0]), numberOf(kValue[1]), numberOf(kValue[2])}))});
                inputs.add(std::string{kName}, kFrom(kFactor, "out"));
            }
            continue;
        }
        if (std::equal(kValue, kValue + parameter.channels, kInitial)) {
            continue;
        }
        if (parameter.channels == 1) {
            inputs.add(std::string{kName}, numberOf(*kValue));
        } else {
            inputs.add(std::string{kName},
                       Value::array({numberOf(kValue[0]), numberOf(kValue[1]), numberOf(kValue[2])}));
        }
    }
    graph::Document document{.kind = "surface"};
    document.nodes.push_back({.id = node, .record = kRecord(kSurfaceType, Value::object(), std::move(inputs))});
    std::ranges::move(after, std::back_inserter(document.nodes));
    Value states = Value::object();
    if (made.shading != Shading::Lit) {
        states.add("shading", Value::string(std::string{kShadings[static_cast<std::size_t>(made.shading)]}));
    }
    if (made.blend != Blend::Opaque) {
        states.add("blend", Value::string(std::string{kBlends[static_cast<std::size_t>(made.blend)]}));
    }
    if (made.blend == Blend::Masked && made.alphaCutoff != 0.5F) {
        states.add("alpha_cutoff", numberOf(made.alphaCutoff));
    }
    if (made.doubleSided) {
        states.add("double_sided", Value::boolean(true));
    }
    if (!states.names().empty()) {
        document.sections.emplace_back("states", std::move(states));
    }
    return document;
}

result::Status validateSurface(const graph::Document& surface, const graph::Limits& limits) {
    if (auto valid = graph::validate(surface, limits); !valid.has_value()) {
        return asMaterial(std::move(valid).error());
    }
    if (surface.kind != "surface" || surface.interface.kind() != Value::Kind::Object ||
        !surface.interface.names().empty()) {
        return invalid("a surface material is of kind surface, its interface empty");
    }
    for (const auto& [kName, kSection] : surface.sections) {
        if (kName != "states") {
            return invalid("a surface material's one section is its states");
        }
    }
    const graph::Node* kSurface = surfaceNode(surface);
    if (kSurface == nullptr) {
        return invalid("a surface material has one surface node");
    }
    Surface ignored;
    RAWFRAME_TRY(inputsIn(*kSurface, ignored));
    RAWFRAME_TRY(statesIn(statesOf(surface)));
    for (const graph::Node& node : surface.nodes) {
        if (!known(node) || typeOf(node) == kSurfaceType) {
            continue;
        }
        if (typeOf(node) == kUvType) {
            RAWFRAME_TRY(uvIn(node));
        } else if (math(typeOf(node))) {
            RAWFRAME_TRY(mathIn(surface, node));
        } else if (typeOf(node) == kSeparate3Type) {
            RAWFRAME_TRY(separate3In(surface, node));
        } else if (typeOf(node) == kNormalMapType) {
            RAWFRAME_TRY(normalMapIn(surface, node));
        } else if (typeOf(node) == kQualitySwitchType) {
            RAWFRAME_TRY(switchIn(surface, node));
        } else {
            RAWFRAME_TRY(sampleIn(surface, node));
        }
    }
    const Value& kInputs = *kSurface->record.find("inputs");
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(kInputs.items()[at]);
        if (!kFrom.has_value()) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, carriedBy(surface, *kFrom));
        const Parameter* kParameter = parameterNamed(kInputs.names()[at]);
        const Carried kWanted = kParameter == nullptr       ? Carried::Vec3
                                : kParameter->channels == 3 ? Carried::Color3
                                                            : Carried::Float;
        if (kCarried.has_value() && *kCarried != kWanted) {
            return invalid("a surface input is connected to an output of its type");
        }
    }
    return {};
}

result::Result<std::string> writeMaterial(const graph::Document& surface, const graph::Limits& limits) {
    RAWFRAME_TRY(validateSurface(surface, limits));
    auto written = graph::writeDocument(surface, limits);
    if (!written.has_value()) {
        return asMaterial(std::move(written).error());
    }
    return std::move(*written);
}

result::Result<graph::Document> readMaterial(std::string_view text, const graph::Limits& limits) {
    constexpr std::array<std::string_view, 1> kSections = {"states"};
    auto read = graph::readDocument(text, "surface", kSections, limits);
    if (!read.has_value()) {
        return asMaterial(std::move(read).error());
    }
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeMaterial(*read, limits));
    if (kWritten != text) {
        return invalid("a surface material is not in its canonical form");
    }
    return std::move(*read);
}

namespace {

/// What an input is at `quality`: a quality switch's input for it, or its
/// default, followed through switches feeding switches; anything else as
/// it is.
Value atQuality(const graph::Document& surface, const Value& value, Quality quality) {
    Value chosen = value;
    for (std::size_t followed = 0; followed <= surface.nodes.size(); ++followed) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(chosen);
        const graph::Node* kSource = kFrom.has_value() ? nodeOf(surface, kFrom->node) : nullptr;
        if (kSource == nullptr || !known(*kSource) || typeOf(*kSource) != kQualitySwitchType) {
            return chosen;
        }
        const Value& kInputs = *kSource->record.find("inputs");
        constexpr std::array<std::string_view, 3> kNames = {"low", "medium", "high"};
        const Value* kFor = kInputs.find(kNames.at(static_cast<std::size_t>(quality)));
        chosen = kFor != nullptr ? *kFor : *kInputs.find("default");
    }
    return chosen;
}

/// The document as it is at `quality` (D318): every input a quality switch
/// feeds given what the switch gives there, and a surface input that comes
/// to its default left out.
graph::Document atQuality(const graph::Document& surface, Quality quality) {
    graph::Document made = surface;
    for (graph::Node& node : made.nodes) {
        const Value* kInputs = node.record.find("inputs");
        if (kInputs == nullptr || kInputs->kind() != Value::Kind::Object || !known(node)) {
            continue;
        }
        const bool kSurface = typeOf(node) == kSurfaceType;
        Value inputs = Value::object();
        for (std::size_t at = 0; at < kInputs->names().size(); ++at) {
            const std::string& kName = kInputs->names()[at];
            Value value = atQuality(surface, kInputs->items()[at], quality);
            const Parameter* kParameter = kSurface ? parameterNamed(kName) : nullptr;
            if (kParameter != nullptr && !graph::connectionOf(value).has_value()) {
                const auto kLiteral = literalOf(*kParameter, value);
                if (kLiteral.has_value() && std::ranges::all_of(std::span{kLiteral->data(), kParameter->channels},
                                                                [kParameter](double channel) {
                                                                    return channel == kParameter->initial;
                                                                })) {
                    continue;
                }
            }
            inputs.add(kName, std::move(value));
        }
        Value record = Value::object();
        for (std::size_t at = 0; at < node.record.names().size(); ++at) {
            const std::string& kName = node.record.names()[at];
            record.add(kName, kName == "inputs" ? std::move(inputs) : node.record.items()[at]);
        }
        node.record = std::move(record);
    }
    return made;
}

result::Result<Material> compiled(const graph::Document& surface) {
    RAWFRAME_TRY(validateSurface(surface));
    const graph::Node& kSurface = *surfaceNode(surface);
    RAWFRAME_TRY_ASSIGN(Material made, statesIn(statesOf(surface)));
    RAWFRAME_TRY(inputsIn(kSurface, made.surface));
    // Generation 1 (D312): the base, packed, and emission textures, one
    // sampler each, each input fed directly or times a factor.
    enum Slot : std::uint8_t {
        Base,
        Packed,
        Emission,
        Normal
    };
    std::array<std::optional<graph::NodeId>, 4> samplers;
    const Value& kInputs = *kSurface.record.find("inputs");
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(kInputs.items()[at]);
        if (!kFrom.has_value()) {
            continue;
        }
        const std::string& kName = kInputs.names()[at];
        if (kName == "geometry_normal") {
            // A normal texture through `normal_map` (D313).
            const graph::Node& kNode = *nodeOf(surface, kFrom->node);
            if (typeOf(kNode) != kNormalMapType) {
                return unsupported("a geometry normal is a normal texture through normal_map in generation 1");
            }
            RAWFRAME_TRY_ASSIGN(const NormalMap kMap, normalMapIn(surface, kNode));
            if (!kMap.in.from.has_value() || kMap.in.from->output != "color" ||
                typeOf(*nodeOf(surface, kMap.in.from->node)) != kSampleTexture2dType) {
                return unsupported("a normal map's input is a sampled texture's color in generation 1");
            }
            samplers[Normal] = kMap.in.from->node;
            made.textures.normalScale = static_cast<float>(kMap.scale);
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const Fed kFed, fedBy(surface, *kFrom));
        const bool kColored = kName == "base_color" || kName == "emission_color";
        const bool kPacked = kName == "base_metalness" || kName == "specular_roughness" || kName == "ambient_occlusion";
        if ((kColored && !kFed.color) || (kName == "geometry_opacity" && kFed.channel != Channel::Alpha) ||
            (kPacked && kFed.channel == Channel::None) || (!kColored && !kPacked && kName != "geometry_opacity")) {
            return unsupported(
                "a texture feeds the base color and opacity, the metalness, roughness, and occlusion by a "
                "channel, and the emission color, in generation 1");
        }
        const Slot kSlot = kPacked ? Packed : kName == "emission_color" ? Emission : Base;
        if (samplers[kSlot].has_value() && *samplers[kSlot] != kFed.sampler) {
            return unsupported("a material's base, packed, and emission textures are one sampler each in generation 1");
        }
        samplers[kSlot] = kFed.sampler;
        if (!std::ranges::all_of(kFed.factor, [](double factor) {
                return factor >= 0 && factor <= 1;
            })) {
            return invalid("a texture's factor is in its input's range, nought to one");
        }
        float* place = parameterNamed(kName)->place(made.surface);
        for (std::size_t channel = 0; channel < parameterNamed(kName)->channels; ++channel) {
            place[channel] = static_cast<float>(kFed.factor[channel]);
        }
        Textures& textures = made.textures;
        if (kName == "base_color") {
            textures.baseColor = true;
        } else if (kName == "geometry_opacity") {
            textures.opacity = true;
        } else if (kName == "base_metalness") {
            textures.metalness = kFed.channel;
        } else if (kName == "specular_roughness") {
            textures.roughness = kFed.channel;
        } else if (kName == "ambient_occlusion") {
            textures.occlusion = kFed.channel;
        }
    }
    for (const auto& [kSlot, kTexture] : {std::pair{Base, &made.textures.base},
                                          std::pair{Packed, &made.textures.packed},
                                          std::pair{Emission, &made.textures.emission},
                                          std::pair{Normal, &made.textures.normal}}) {
        if (!samplers[kSlot].has_value()) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const Sampling kSampling, sampleIn(surface, *nodeOf(surface, *samplers[kSlot])));
        SampledTexture& texture = *kTexture;
        texture = kSampling.texture;
        if (kSampling.uv.has_value()) {
            RAWFRAME_TRY_ASSIGN(const Affine kAffine, affineOf(surface, *kSampling.uv));
            for (std::size_t axis = 0; axis < 2; ++axis) {
                texture.scale[axis] = static_cast<float>(kAffine.scale[axis]);
                texture.offset[axis] = static_cast<float>(kAffine.offset[axis]);
            }
            const auto kFinite = [](float value) {
                return std::isfinite(value);
            };
            if (!std::ranges::all_of(texture.scale, kFinite) || !std::ranges::all_of(texture.offset, kFinite)) {
                return invalid("a texture's coordinates are scaled and moved by finite numbers");
            }
        }
    }
    return made;
}

} // namespace

result::Result<Material> compile(const graph::Document& surface, Quality quality) {
    RAWFRAME_TRY(validateSurface(surface));
    if (!std::ranges::all_of(surface.nodes, known)) {
        return unsupported("a material with a node of a type this engine does not know waits for the node library");
    }
    return compiled(atQuality(surface, quality));
}

result::Result<Qualities> compileQualities(const graph::Document& surface) {
    Qualities made;
    for (const Quality kQuality : {Quality::Low, Quality::Medium, Quality::High}) {
        RAWFRAME_TRY_ASSIGN(made.at(static_cast<std::size_t>(kQuality)), compile(surface, kQuality));
    }
    return made;
}

result::Result<base::Sha256Digest> semanticHash(const graph::Document& surface) {
    RAWFRAME_TRY(validateSurface(surface));
    if (!std::ranges::all_of(surface.nodes, known)) {
        return unsupported("a material with a node of a type this engine does not know has no semantic hash");
    }
    auto hashed = graph::semanticHash(surface.nodes,
                                      {.kind = surface.kind,
                                       .interface = surface.interface,
                                       .outputs = {{"surface", surfaceNode(surface)->id}},
                                       .sections = surface.sections});
    if (!hashed.has_value()) {
        return asMaterial(std::move(hashed).error());
    }
    return *hashed;
}

} // namespace rawframe::material
