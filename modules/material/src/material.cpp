#include "rawframe/material/material.h"

#include "rawframe/graph/errors.h"
#include "rawframe/material/errors.h"

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

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kMaterialDomain, code(MaterialError::Invalid), why);
}

std::unexpected<result::Error> unsupported(std::string_view why) {
    return result::fail(result::ErrorClass::Unsupported, kMaterialDomain, code(MaterialError::Unsupported), why);
}

/// A graph error as this module's.
std::unexpected<result::Error> asMaterial(result::Error error) {
    if (error.domain() != graph::kGraphDomain) {
        return std::unexpected<result::Error>{std::move(error)};
    }
    return result::fail(result::ErrorClass::InvalidArgument,
                        kMaterialDomain,
                        code(error.code() == graph::code(graph::GraphError::OverLimit) ? MaterialError::OverLimit
                                                                                       : MaterialError::Invalid),
                        error.description());
}

/// A core parameter: its name, a color's three channels or one number,
/// its range, its default, and where a Surface holds it.
struct Parameter {
    std::string_view name;
    std::size_t channels;
    double lowest;
    double highest;
    double initial;
    float* (*place)(Surface&);
};

/// In name order, as a node's inputs are.
const std::array<Parameter, 10> kParameters = {{
    {"ambient_occlusion",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.ambientOcclusion;
     }},
    {"base_color",
     3,
     0,
     1,
     0.8,
     [](Surface& s) {
         return s.baseColor.data();
     }},
    {"base_metalness",
     1,
     0,
     1,
     0,
     [](Surface& s) {
         return &s.baseMetalness;
     }},
    {"emission_color",
     3,
     0,
     1,
     1,
     [](Surface& s) {
         return s.emissionColor.data();
     }},
    {"emission_luminance",
     1,
     0,
     1e9,
     0,
     [](Surface& s) {
         return &s.emissionLuminance;
     }},
    {"geometry_opacity",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.geometryOpacity;
     }},
    {"specular_color",
     3,
     0,
     1,
     1,
     [](Surface& s) {
         return s.specularColor.data();
     }},
    {"specular_ior",
     1,
     1,
     3,
     1.5,
     [](Surface& s) {
         return &s.specularIor;
     }},
    {"specular_roughness",
     1,
     0,
     1,
     0.3,
     [](Surface& s) {
         return &s.specularRoughness;
     }},
    {"specular_weight",
     1,
     0,
     1,
     1,
     [](Surface& s) {
         return &s.specularWeight;
     }},
}};

/// In the contract's order (SPEC-0026's table), as cooked bytes are.
const std::array<Parameter, 10> kContractOrder = {kParameters[1],
                                                  kParameters[2],
                                                  kParameters[9],
                                                  kParameters[6],
                                                  kParameters[8],
                                                  kParameters[7],
                                                  kParameters[3],
                                                  kParameters[4],
                                                  kParameters[5],
                                                  kParameters[0]};

const Parameter* parameterNamed(std::string_view name) {
    const auto kFound = std::ranges::find(kParameters, name, &Parameter::name);
    return kFound == kParameters.end() ? nullptr : &*kFound;
}

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

constexpr std::array<std::string_view, 2> kShadings = {"lit", "unlit"};
constexpr std::array<std::string_view, 3> kBlends = {"opaque", "masked", "translucent"};

const graph::Node* surfaceNode(const graph::Document& surface) {
    const graph::Node* found = nullptr;
    for (const graph::Node& node : surface.nodes) {
        const Value* kType = node.record.find("type");
        if (kType != nullptr && kType->text() != nullptr && *kType->text() == kSurfaceType) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &node;
        }
    }
    return found;
}

const Value* statesOf(const graph::Document& surface) {
    const auto kFound = std::ranges::find(surface.sections, std::string_view{"states"}, [](const auto& section) {
        return std::string_view{section.first};
    });
    return kFound == surface.sections.end() ? nullptr : &kFound->second;
}

/// The states section's, checked.
result::Result<Material> statesIn(const Value* states) {
    Material made;
    if (states == nullptr) {
        return made;
    }
    constexpr std::array<std::string_view, 4> kOrder = {"shading", "blend", "alpha_cutoff", "double_sided"};
    if (states->kind() != Value::Kind::Object || states->names().empty()) {
        return invalid("a material's states are an object, left out when all are at their defaults");
    }
    std::size_t next = 0;
    for (std::size_t at = 0; at < states->names().size(); ++at) {
        const std::string& name = states->names()[at];
        const Value& value = states->items()[at];
        const auto kPlace = std::ranges::find(kOrder, name);
        if (kPlace == kOrder.end() || static_cast<std::size_t>(kPlace - kOrder.begin()) < next) {
            return invalid("a material's states are shading, blend, alpha_cutoff, and double_sided, in that order");
        }
        next = static_cast<std::size_t>(kPlace - kOrder.begin()) + 1;
        if (name == "shading" || name == "blend") {
            const bool kShading = name == "shading";
            const std::span<const std::string_view> kSet =
                kShading ? std::span<const std::string_view>{kShadings} : std::span<const std::string_view>{kBlends};
            const auto kFound =
                value.kind() == Value::Kind::String ? std::ranges::find(kSet, *value.text()) : kSet.end();
            if (kFound == kSet.end() || kFound == kSet.begin()) {
                return invalid("a material's shading is unlit, its blend masked or translucent, when not the default");
            }
            const auto kIndex = static_cast<std::uint8_t>(kFound - kSet.begin());
            if (kShading) {
                made.shading = static_cast<Shading>(kIndex);
            } else {
                made.blend = static_cast<Blend>(kIndex);
            }
        } else if (name == "alpha_cutoff") {
            const std::optional<double> kCutoff = value.kind() == Value::Kind::Number ? value.real() : std::nullopt;
            if (!kCutoff.has_value() || !(*kCutoff >= 0 && *kCutoff <= 1) || *kCutoff == 0.5) {
                return invalid("a material's alpha cutoff is from nought to one, when not 0.5");
            }
            made.alphaCutoff = static_cast<float>(*kCutoff);
        } else if (value.truth() != true) {
            return invalid("a material is double sided when it says so");
        }
        made.doubleSided = made.doubleSided || name == "double_sided";
    }
    if (states->find("alpha_cutoff") != nullptr && made.blend != Blend::Masked) {
        return invalid("a material's alpha cutoff is a masked one's");
    }
    return made;
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

/// What an output of a node this family knows carries.
enum class Carried : std::uint8_t {
    Float,
    Color3,
    Vec2,
    Vec3
};

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr &&
           (*kText == kSurfaceType || *kText == kSampleTexture2dType || *kText == kUvType || *kText == kMultiplyType ||
            *kText == kAddType || *kText == kSeparate3Type || *kText == kNormalMapType || *kText == kQualitySwitchType);
}

bool math(std::string_view type) {
    return type == kMultiplyType || type == kAddType;
}

std::string_view typeOf(const graph::Node& node) {
    return *node.record.find("type")->text();
}

const graph::Node* nodeOf(const graph::Document& surface, graph::NodeId id) {
    const auto kFound = std::ranges::find(surface.nodes, id, &graph::Node::id);
    return kFound == surface.nodes.end() ? nullptr : &*kFound;
}

/// A node of the library's params and inputs: the record's only members
/// after its type, objects, their names among `params` and `inputs`, in
/// name order.
result::Result<std::pair<const Value*, const Value*>>
partsOf(const graph::Node& node, std::span<const std::string_view> params, std::span<const std::string_view> inputs) {
    const Value* kParams = node.record.find("params");
    const Value* kInputs = node.record.find("inputs");
    if (node.record.names().size() != 3 || kParams == nullptr || kInputs == nullptr ||
        node.record.names()[1] != "params" || kParams->kind() != Value::Kind::Object ||
        kInputs->kind() != Value::Kind::Object) {
        return invalid("a node of the library is its type, its params, and its inputs");
    }
    for (const auto& [kNames, kAllowed] : {std::pair{kParams, params}, std::pair{kInputs, inputs}}) {
        std::size_t next = 0;
        for (const std::string& name : kNames->names()) {
            const auto kPlace = std::ranges::find(kAllowed, name);
            if (kPlace == kAllowed.end() || static_cast<std::size_t>(kPlace - kAllowed.begin()) < next) {
                return invalid("a node of the library has its type's params and inputs, in name order");
            }
            next = static_cast<std::size_t>(kPlace - kAllowed.begin()) + 1;
        }
    }
    return std::pair{kParams, kInputs};
}

/// What a `uv` node's params say: the channel.
result::Result<std::uint32_t> uvIn(const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"channel"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, {}));
    const Value* kChannel = kParts.first->find("channel");
    if (kChannel == nullptr) {
        return 0U;
    }
    const std::optional<std::int64_t> kNumber =
        kChannel->kind() == Value::Kind::Number ? kChannel->integer() : std::nullopt;
    if (!kNumber.has_value() || *kNumber < 1 || *kNumber > 7) {
        return invalid("a uv node's channel is from one to seven, when not nought");
    }
    return static_cast<std::uint32_t>(*kNumber);
}

result::Result<std::optional<Carried>> carriedBy(const graph::Document& surface, const graph::Connection& from);

/// An input of `multiply` or `add` (D311): connected, or a literal, and
/// what it carries when that is known.
struct Operand {
    std::optional<graph::Connection> from;
    std::array<double, 3> literal{};
    std::optional<Carried> carried;
};

result::Result<Operand> operandOf(const graph::Document& surface, const Value& value) {
    Operand made;
    made.from = graph::connectionOf(value);
    if (made.from.has_value()) {
        RAWFRAME_TRY_ASSIGN(made.carried, carriedBy(surface, *made.from));
        return made;
    }
    const auto kFinite = [](const Value& each) -> std::optional<double> {
        const std::optional<double> kReal = each.kind() == Value::Kind::Number ? each.real() : std::nullopt;
        return kReal.has_value() && std::isfinite(*kReal) ? kReal : std::nullopt;
    };
    if (const std::optional<double> kOne = kFinite(value); kOne.has_value()) {
        made.literal[0] = *kOne;
        made.carried = Carried::Float;
        return made;
    }
    const std::size_t kCount = value.kind() == Value::Kind::Array ? value.items().size() : 0;
    if (kCount != 2 && kCount != 3) {
        return invalid("a math node's literal is a number, or two or three of them");
    }
    for (std::size_t at = 0; at < kCount; ++at) {
        const std::optional<double> kChannel = kFinite(value.items()[at]);
        if (!kChannel.has_value()) {
            return invalid("a math node's literal is a number, or two or three of them");
        }
        made.literal[at] = *kChannel;
    }
    made.carried = kCount == 2 ? Carried::Vec2 : Carried::Color3;
    return made;
}

/// A `multiply` or `add` node's inputs, checked, and what its `out`
/// carries: their type, or the one not a float when the other is.
struct Math {
    Operand a;
    Operand b;
    std::optional<Carried> out;
};

result::Result<Math> mathIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 2> kInputs = {"a", "b"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    const Value* kA = kParts.second->find("a");
    const Value* kB = kParts.second->find("b");
    if (kA == nullptr || kB == nullptr) {
        return invalid("a math node's inputs are a and b, both given");
    }
    Math made;
    RAWFRAME_TRY_ASSIGN(made.a, operandOf(surface, *kA));
    RAWFRAME_TRY_ASSIGN(made.b, operandOf(surface, *kB));
    if (made.a.carried.has_value() && made.b.carried.has_value()) {
        const Carried kA2 = *made.a.carried;
        const Carried kB2 = *made.b.carried;
        if (kA2 != kB2 && kA2 != Carried::Float && kB2 != Carried::Float) {
            return invalid("a math node's inputs are of one type, or one of them is a float");
        }
        made.out = kA2 == Carried::Float ? kB2 : kA2;
    }
    return made;
}

/// A `quality_switch` node's inputs, checked, and what its `out` carries
/// (D318): `default` given, all of one type where it is known.
struct Switch {
    std::array<std::optional<Operand>, 4> inputs;
    std::optional<Carried> out;
};

constexpr std::array<std::string_view, 4> kSwitchInputs = {"default", "high", "low", "medium"};

result::Result<Switch> switchIn(const graph::Document& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kSwitchInputs));
    if (kParts.second->find("default") == nullptr) {
        return invalid("a quality switch's default is given");
    }
    Switch made;
    for (std::size_t at = 0; at < kSwitchInputs.size(); ++at) {
        const Value* kInput = kParts.second->find(kSwitchInputs.at(at));
        if (kInput == nullptr) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(made.inputs.at(at), operandOf(surface, *kInput));
        const std::optional<Carried> kCarried = made.inputs.at(at)->carried;
        if (kCarried.has_value() && made.out.has_value() && *kCarried != *made.out) {
            return invalid("a quality switch's inputs are of one type");
        }
        made.out = made.out.has_value() ? made.out : kCarried;
    }
    return made;
}

/// A `separate3` node's input, checked: a color3 (D312).
result::Result<Operand> separate3In(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a separate3 node's input is in, given");
    }
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(surface, *kIn));
    if (made.carried.has_value() && *made.carried != Carried::Color3) {
        return invalid("a separate3 node's input is a color3");
    }
    return made;
}

/// A `normal_map` node's input and scale, checked (D313).
struct NormalMap {
    Operand in;
    double scale = 1;
};

result::Result<NormalMap> normalMapIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"scale"};
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a normal map's input is in, given");
    }
    NormalMap made;
    RAWFRAME_TRY_ASSIGN(made.in, operandOf(surface, *kIn));
    if (made.in.carried.has_value() && *made.in.carried != Carried::Color3) {
        return invalid("a normal map's input is a color3");
    }
    if (const Value* kScale = kParts.first->find("scale"); kScale != nullptr) {
        const std::optional<double> kReal = kScale->kind() == Value::Kind::Number ? kScale->real() : std::nullopt;
        if (!kReal.has_value() || !std::isfinite(*kReal) || *kReal == 1) {
            return invalid("a normal map's scale is a finite number, left out at one");
        }
        made.scale = *kReal;
    }
    return made;
}

/// What a `sample_texture_2d` node's params say, and what its `uv` comes
/// from, if connected.
struct Sampling {
    SampledTexture texture;
    std::optional<graph::Connection> uv;
};

result::Result<Sampling> sampleIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kParams = {"address", "filter", "texture"};
    constexpr std::array<std::string_view, 1> kInputs = {"uv"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    Sampling made;
    const Value* kTexture = kParts.first->find("texture");
    const std::optional<std::uint64_t> kId =
        kTexture != nullptr && kTexture->text() != nullptr ? graph::nodeIdOf(*kTexture->text()) : std::nullopt;
    if (!kId.has_value() || *kId == 0) {
        return invalid("a sampled texture names the game's texture by 16 lowercase hex digits, never nought");
    }
    made.texture.id = *kId;
    if (const Value* kAddress = kParts.first->find("address"); kAddress != nullptr) {
        if (kAddress->text() == nullptr || *kAddress->text() != "clamp") {
            return invalid("a sampled texture's address is clamp, when not the default");
        }
        made.texture.address = Address::Clamp;
    }
    if (const Value* kFilter = kParts.first->find("filter"); kFilter != nullptr) {
        if (kFilter->text() == nullptr || *kFilter->text() != "nearest") {
            return invalid("a sampled texture's filter is nearest, when not the default");
        }
        made.texture.filter = Filter::Nearest;
    }
    if (const Value* kUv = kParts.second->find("uv"); kUv != nullptr) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(*kUv);
        if (!kFrom.has_value()) {
            return invalid("a sampled texture's uv is connected, or left out for a mesh's first");
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, carriedBy(surface, *kFrom));
        if (kCarried.has_value() && *kCarried != Carried::Vec2) {
            return invalid("a sampled texture's uv comes from a vec2 output");
        }
        made.uv = kFrom;
    }
    return made;
}

/// What the output a connection names carries, if its node is of a type
/// this family knows; refused when the node has no such output.
result::Result<std::optional<Carried>> carriedBy(const graph::Document& surface, const graph::Connection& from) {
    const graph::Node* kSource = nodeOf(surface, from.node);
    if (kSource == nullptr || !known(*kSource)) {
        return std::optional<Carried>{};
    }
    const std::string_view kType = typeOf(*kSource);
    if (kType == kSampleTexture2dType && from.output == "color") {
        return std::optional{Carried::Color3};
    }
    if (kType == kSampleTexture2dType && from.output == "alpha") {
        return std::optional{Carried::Float};
    }
    if (kType == kUvType && from.output == "uv") {
        return std::optional{Carried::Vec2};
    }
    if (math(kType) && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Math kMath, mathIn(surface, *kSource));
        return kMath.out;
    }
    if (kType == kNormalMapType && from.output == "out") {
        RAWFRAME_TRY(normalMapIn(surface, *kSource));
        return std::optional{Carried::Vec3};
    }
    if (kType == kQualitySwitchType && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Switch kSwitch, switchIn(surface, *kSource));
        return kSwitch.out;
    }
    if (kType == kSeparate3Type && (from.output == "r" || from.output == "g" || from.output == "b")) {
        RAWFRAME_TRY(separate3In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    return invalid("a connection names an output its node has");
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

namespace {

/// A material's bytes after the format: its states, Surface, textures,
/// what they feed, and the normal's scale.
constexpr std::size_t kBodyBytes = 8 + (4 * 16) + (4 * 28) + 8 + 4;

void putWord(std::vector<std::byte>& bytes, std::uint32_t word) {
    for (std::size_t at = 0; at < 4; ++at) {
        bytes.push_back(static_cast<std::byte>((word >> (at * 8)) & 0xFFU));
    }
}

void putBody(std::vector<std::byte>& bytes, const Material& made) {
    const auto kPut = [&bytes](std::uint32_t word) {
        putWord(bytes, word);
    };
    bytes.push_back(static_cast<std::byte>(made.shading));
    bytes.push_back(static_cast<std::byte>(made.blend));
    bytes.push_back(static_cast<std::byte>(made.doubleSided ? 1 : 0));
    bytes.push_back(std::byte{0});
    kPut(std::bit_cast<std::uint32_t>(made.alphaCutoff));
    Surface surface = made.surface;
    for (const Parameter& parameter : kContractOrder) {
        const float* kValue = parameter.place(surface);
        for (std::size_t channel = 0; channel < parameter.channels; ++channel) {
            kPut(std::bit_cast<std::uint32_t>(kValue[channel]));
        }
    }
    for (const SampledTexture* kTexture :
         {&made.textures.base, &made.textures.packed, &made.textures.emission, &made.textures.normal}) {
        kPut(static_cast<std::uint32_t>(kTexture->id));
        kPut(static_cast<std::uint32_t>(kTexture->id >> 32U));
        bytes.push_back(static_cast<std::byte>(kTexture->filter));
        bytes.push_back(static_cast<std::byte>(kTexture->address));
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{0});
        for (const float kValue : {kTexture->scale[0], kTexture->scale[1], kTexture->offset[0], kTexture->offset[1]}) {
            kPut(std::bit_cast<std::uint32_t>(kValue));
        }
    }
    for (const std::uint8_t kFeeds : {static_cast<std::uint8_t>(made.textures.baseColor ? 1 : 0),
                                      static_cast<std::uint8_t>(made.textures.opacity ? 1 : 0),
                                      static_cast<std::uint8_t>(made.textures.metalness),
                                      static_cast<std::uint8_t>(made.textures.roughness),
                                      static_cast<std::uint8_t>(made.textures.occlusion),
                                      std::uint8_t{0},
                                      std::uint8_t{0},
                                      std::uint8_t{0}}) {
        bytes.push_back(static_cast<std::byte>(kFeeds));
    }
    kPut(std::bit_cast<std::uint32_t>(made.textures.normalScale));
}

/// A material's bytes as `putBody` writes them, `kBodyBytes` long, as
/// `decode` refuses them.
result::Result<Material> bodyOf(std::span<const std::byte> bytes) {
    // Offsets below are the whole record's, whose body follows 8 bytes.
    const auto kWord = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at - 8 + each]) << (each * 8);
        }
        return word;
    };
    const auto kByte = [&bytes](std::size_t at) {
        return bytes[at - 8];
    };
    const auto kShading = std::to_integer<std::uint8_t>(kByte(8));
    const auto kBlend = std::to_integer<std::uint8_t>(kByte(9));
    const auto kSided = std::to_integer<std::uint8_t>(kByte(10));
    if (kShading > 1 || kBlend > 2 || kSided > 1 || kByte(11) != std::byte{0}) {
        return invalid("a cooked material's states are in their sets");
    }
    Material made{.shading = static_cast<Shading>(kShading),
                  .blend = static_cast<Blend>(kBlend),
                  .alphaCutoff = std::bit_cast<float>(kWord(12)),
                  .doubleSided = kSided == 1};
    std::size_t at = 16;
    for (const Parameter& parameter : kContractOrder) {
        float* place = parameter.place(made.surface);
        for (std::size_t channel = 0; channel < parameter.channels; ++channel, at += 4) {
            place[channel] = std::bit_cast<float>(kWord(at));
        }
    }
    for (SampledTexture* texture :
         {&made.textures.base, &made.textures.packed, &made.textures.emission, &made.textures.normal}) {
        const auto kFilter = std::to_integer<std::uint8_t>(kByte(at + 8));
        const auto kAddress = std::to_integer<std::uint8_t>(kByte(at + 9));
        if (kFilter > 1 || kAddress > 1 || kByte(at + 10) != std::byte{0} || kByte(at + 11) != std::byte{0}) {
            return invalid("a cooked material's texture's sampler state is in its sets");
        }
        *texture =
            SampledTexture{.id = kWord(at) | (std::uint64_t{kWord(at + 4)} << 32U),
                           .filter = static_cast<Filter>(kFilter),
                           .address = static_cast<Address>(kAddress),
                           .scale = {std::bit_cast<float>(kWord(at + 12)), std::bit_cast<float>(kWord(at + 16))},
                           .offset = {std::bit_cast<float>(kWord(at + 20)), std::bit_cast<float>(kWord(at + 24))}};
        at += 28;
    }
    std::array<std::uint8_t, 8> feeds{};
    for (std::size_t each = 0; each < feeds.size(); ++each) {
        feeds.at(each) = std::to_integer<std::uint8_t>(kByte(at + each));
    }
    if (feeds[0] > 1 || feeds[1] > 1 || feeds[2] > 4 || feeds[3] > 4 || feeds[4] > 4 || feeds[5] != 0 ||
        feeds[6] != 0 || feeds[7] != 0) {
        return invalid("a cooked material's textures feed what they may");
    }
    made.textures.baseColor = feeds[0] == 1;
    made.textures.opacity = feeds[1] == 1;
    made.textures.metalness = static_cast<Channel>(feeds[2]);
    made.textures.roughness = static_cast<Channel>(feeds[3]);
    made.textures.occlusion = static_cast<Channel>(feeds[4]);
    made.textures.normalScale = std::bit_cast<float>(kWord(at + 8));
    // Its ranges are the document's: written as one and checked.
    if (!(made.alphaCutoff >= 0 && made.alphaCutoff <= 1)) {
        return invalid("a cooked material's alpha cutoff is from nought to one");
    }
    Material checked = made;
    checked.alphaCutoff = made.blend == Blend::Masked ? made.alphaCutoff : 0.5F;
    // A material a document compiles to, and nothing else: its ranges,
    // its factors, and a texture's map as the document's.
    const auto kCompiled = compile(documentOf(checked, 1));
    if (!kCompiled.has_value() || *kCompiled != checked) {
        return invalid("a cooked material is one a surface document compiles to");
    }
    return made;
}

} // namespace

std::vector<std::byte> encode(const Qualities& made) {
    std::vector<std::byte> bytes;
    for (const char kLetter : std::string_view{"RFMT"}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 6);
    const Material& kHigh = made.at(static_cast<std::size_t>(Quality::High));
    putBody(bytes, kHigh);
    const bool kLow = made.at(static_cast<std::size_t>(Quality::Low)) != kHigh;
    const bool kMedium = made.at(static_cast<std::size_t>(Quality::Medium)) != kHigh;
    putWord(bytes, (kLow ? 1U : 0U) | (kMedium ? 2U : 0U));
    for (const auto& [kDiffers, kQuality] : {std::pair{kLow, Quality::Low}, std::pair{kMedium, Quality::Medium}}) {
        if (kDiffers) {
            putBody(bytes, made.at(static_cast<std::size_t>(kQuality)));
        }
    }
    return bytes;
}

std::vector<std::byte> encode(const Material& made) {
    return encode(Qualities{made, made, made});
}

result::Result<Qualities> decode(std::span<const std::byte> bytes) {
    constexpr std::size_t kHead = 8;
    const auto kWordAt = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at + each]) << (each * 8);
        }
        return word;
    };
    if (bytes.size() < kHead + kBodyBytes + 4 ||
        std::string_view{reinterpret_cast<const char*>(bytes.data()), 4} != "RFMT" || kWordAt(4) != 6) {
        return invalid("a cooked material is RFMT, format 6, and its material at the high quality");
    }
    const std::uint32_t kDiffer = kWordAt(kHead + kBodyBytes);
    const std::size_t kMore = ((kDiffer & 1U) != 0 ? 1 : 0) + ((kDiffer & 2U) != 0 ? 1 : 0);
    if (kDiffer > 3 || bytes.size() != kHead + kBodyBytes + 4 + (kMore * kBodyBytes)) {
        return invalid("a cooked material holds the qualities that differ, and nothing else");
    }
    RAWFRAME_TRY_ASSIGN(const Material kHigh, bodyOf(bytes.subspan(kHead, kBodyBytes)));
    Qualities made{kHigh, kHigh, kHigh};
    std::size_t at = kHead + kBodyBytes + 4;
    for (const auto& [kBit, kQuality] : {std::pair{1U, Quality::Low}, std::pair{2U, Quality::Medium}}) {
        if ((kDiffer & kBit) == 0) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(made.at(static_cast<std::size_t>(kQuality)), bodyOf(bytes.subspan(at, kBodyBytes)));
        if (made.at(static_cast<std::size_t>(kQuality)) == kHigh) {
            return invalid("a cooked material holds a quality apart only where it differs");
        }
        at += kBodyBytes;
    }
    return made;
}

std::array<float, kBlobFloats> blobOf(const Material& made) noexcept {
    const Surface& kSurface = made.surface;
    std::array<float, kBlobFloats> blob{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        blob[channel] = kSurface.baseColor[channel];
        blob[4 + channel] = kSurface.specularColor[channel] * kSurface.specularWeight;
        blob[8 + channel] = kSurface.emissionColor[channel] * kSurface.emissionLuminance;
    }
    blob[3] = kSurface.baseMetalness;
    blob[7] = kSurface.specularRoughness;
    blob[11] = kSurface.specularIor;
    blob[12] = kSurface.geometryOpacity;
    blob[13] = kSurface.ambientOcclusion;
    blob[14] = made.blend == Blend::Masked ? made.alphaCutoff : 0;
    const Textures& kTextures = made.textures;
    blob[15] = static_cast<float>((made.shading == Shading::Unlit ? 1U : 0U) | (kTextures.baseColor ? 2U : 0U) |
                                  (kTextures.opacity ? 4U : 0U) | (kTextures.emission.id != 0 ? 8U : 0U) |
                                  (kTextures.normal.id != 0 ? 16U : 0U));
    std::size_t at = 16;
    for (const SampledTexture* kTexture :
         {&kTextures.base, &kTextures.packed, &kTextures.emission, &kTextures.normal}) {
        blob[at++] = kTexture->scale[0];
        blob[at++] = kTexture->scale[1];
        blob[at++] = kTexture->offset[0];
        blob[at++] = kTexture->offset[1];
    }
    blob[32] = static_cast<float>(kTextures.metalness);
    blob[33] = static_cast<float>(kTextures.roughness);
    blob[34] = static_cast<float>(kTextures.occlusion);
    blob[35] = kTextures.normalScale;
    return blob;
}

} // namespace rawframe::material
