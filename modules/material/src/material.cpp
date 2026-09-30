#include "rawframe/material/material.h"

#include "rawframe/graph/errors.h"
#include "rawframe/material/errors.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <optional>
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
        if (kParameter == nullptr || (at > 0 && !(inputs->names()[at - 1] < inputs->names()[at]))) {
            return invalid("a surface node's inputs are the Surface contract's core parameters, in name order");
        }
        if (graph::connectionOf(inputs->items()[at]).has_value()) {
            connected = true;
            continue;
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
    Vec2
};

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr && (*kText == kSurfaceType || *kText == kSampleTexture2dType || *kText == kUvType);
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

/// What a `sample_texture_2d` node's params say, and the node its `uv`
/// comes from, if connected.
struct Sampling {
    SampledTexture texture;
    std::optional<graph::NodeId> uv;
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
        const graph::Node* kSource = nodeOf(surface, kFrom->node);
        if (kSource != nullptr && known(*kSource) && (typeOf(*kSource) != kUvType || kFrom->output != "uv")) {
            return invalid("a sampled texture's uv comes from a uv node's output");
        }
        made.uv = kFrom->node;
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
    return invalid("a connection names an output its node has");
}

} // namespace

graph::Document documentOf(const Material& made, graph::NodeId node) {
    const Surface kDefaults;
    Surface surface = made.surface;
    Surface initial = kDefaults;
    Value inputs = Value::object();
    const graph::NodeId kSampler = node + 1;
    for (const Parameter& parameter : kParameters) {
        const float* kValue = parameter.place(surface);
        const float* kInitial = parameter.place(initial);
        if (made.texture.id != 0 && ((parameter.name == "base_color" && made.texture.color) ||
                                     (parameter.name == "geometry_opacity" && made.texture.alpha))) {
            inputs.add(std::string{parameter.name},
                       graph::connectionValue(
                           {.node = kSampler,
                            .output = made.texture.color && parameter.name == "base_color" ? "color" : "alpha"}));
            continue;
        }
        if (std::equal(kValue, kValue + parameter.channels, kInitial)) {
            continue;
        }
        if (parameter.channels == 1) {
            inputs.add(std::string{parameter.name}, numberOf(*kValue));
        } else {
            inputs.add(std::string{parameter.name},
                       Value::array({numberOf(kValue[0]), numberOf(kValue[1]), numberOf(kValue[2])}));
        }
    }
    Value record = Value::object();
    record.add("type", Value::string(std::string{kSurfaceType}));
    record.add("params", Value::object());
    record.add("inputs", std::move(inputs));
    graph::Document document{.kind = "surface"};
    document.nodes.push_back({.id = node, .record = std::move(record)});
    if (made.texture.id != 0) {
        Value params = Value::object();
        if (made.texture.address == Address::Clamp) {
            params.add("address", Value::string("clamp"));
        }
        if (made.texture.filter == Filter::Nearest) {
            params.add("filter", Value::string("nearest"));
        }
        params.add("texture", Value::string(graph::nodeIdText(made.texture.id)));
        Value sampler = Value::object();
        sampler.add("type", Value::string(std::string{kSampleTexture2dType}));
        sampler.add("params", std::move(params));
        sampler.add("inputs", Value::object());
        document.nodes.push_back({.id = kSampler, .record = std::move(sampler)});
    }
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
        const Carried kWanted = parameterNamed(kInputs.names()[at])->channels == 3 ? Carried::Color3 : Carried::Float;
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

result::Result<Material> compile(const graph::Document& surface) {
    RAWFRAME_TRY(validateSurface(surface));
    if (!std::ranges::all_of(surface.nodes, known)) {
        return unsupported("a material with a node of a type this engine does not know waits for the node library");
    }
    const graph::Node& kSurface = *surfaceNode(surface);
    RAWFRAME_TRY_ASSIGN(Material made, statesIn(statesOf(surface)));
    RAWFRAME_TRY(inputsIn(kSurface, made.surface));
    // Generation 1: one sampled texture, its color the base color and its
    // alpha the opacity.
    std::optional<graph::NodeId> sampler;
    const Value& kInputs = *kSurface.record.find("inputs");
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(kInputs.items()[at]);
        if (!kFrom.has_value()) {
            continue;
        }
        const std::string& kName = kInputs.names()[at];
        const bool kColor = kName == "base_color" && kFrom->output == "color";
        const bool kAlpha = kName == "geometry_opacity" && kFrom->output == "alpha";
        if ((!kColor && !kAlpha) || typeOf(*nodeOf(surface, kFrom->node)) != kSampleTexture2dType) {
            return unsupported("a surface input connected to anything but a sampled texture's color or alpha waits "
                               "for the node library");
        }
        if (sampler.has_value() && *sampler != kFrom->node) {
            return unsupported("a material samples one texture in generation 1");
        }
        sampler = kFrom->node;
        made.texture.color = made.texture.color || kColor;
        made.texture.alpha = made.texture.alpha || kAlpha;
    }
    if (sampler.has_value()) {
        RAWFRAME_TRY_ASSIGN(const Sampling kSampling, sampleIn(surface, *nodeOf(surface, *sampler)));
        if (kSampling.uv.has_value()) {
            RAWFRAME_TRY_ASSIGN(const std::uint32_t kChannel, uvIn(*nodeOf(surface, *kSampling.uv)));
            if (kChannel != 0) {
                return unsupported("a mesh's texture coordinates past its first set wait for meshes to hold them");
            }
        }
        made.texture.id = kSampling.texture.id;
        made.texture.filter = kSampling.texture.filter;
        made.texture.address = kSampling.texture.address;
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

std::vector<std::byte> encode(const Material& made) {
    std::vector<std::byte> bytes;
    const auto kPut = [&bytes](std::uint32_t word) {
        for (std::size_t at = 0; at < 4; ++at) {
            bytes.push_back(static_cast<std::byte>((word >> (at * 8)) & 0xFFU));
        }
    };
    for (const char kLetter : std::string_view{"RFMT"}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    kPut(2);
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
    kPut(static_cast<std::uint32_t>(made.texture.id));
    kPut(static_cast<std::uint32_t>(made.texture.id >> 32U));
    bytes.push_back(static_cast<std::byte>(made.texture.filter));
    bytes.push_back(static_cast<std::byte>(made.texture.address));
    bytes.push_back(static_cast<std::byte>((made.texture.color ? 1U : 0U) | (made.texture.alpha ? 2U : 0U)));
    bytes.push_back(std::byte{0});
    return bytes;
}

result::Result<Material> decode(std::span<const std::byte> bytes) {
    constexpr std::size_t kSize = 16 + (4 * 16) + 12;
    if (bytes.size() != kSize || std::string_view{reinterpret_cast<const char*>(bytes.data()), 4} != "RFMT") {
        return invalid("a cooked material is RFMT, its format, states, Surface, and texture");
    }
    const auto kWord = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at + each]) << (each * 8);
        }
        return word;
    };
    const auto kShading = std::to_integer<std::uint8_t>(bytes[8]);
    const auto kBlend = std::to_integer<std::uint8_t>(bytes[9]);
    const auto kSided = std::to_integer<std::uint8_t>(bytes[10]);
    if (kWord(4) != 2 || kShading > 1 || kBlend > 2 || kSided > 1 || bytes[11] != std::byte{0}) {
        return invalid("a cooked material is format 2, its states in their sets");
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
    const std::uint64_t kTexture = kWord(at) | (std::uint64_t{kWord(at + 4)} << 32U);
    const auto kFilter = std::to_integer<std::uint8_t>(bytes[at + 8]);
    const auto kAddress = std::to_integer<std::uint8_t>(bytes[at + 9]);
    const auto kFeeds = std::to_integer<std::uint8_t>(bytes[at + 10]);
    // A texture feeds something; none has no sampler state.
    if (kFilter > 1 || kAddress > 1 || kFeeds > 3 || bytes[at + 11] != std::byte{0} ||
        (kTexture == 0) != (kFeeds == 0) || (kTexture == 0 && (kFilter != 0 || kAddress != 0))) {
        return invalid("a cooked material's texture is none, or one feeding its color or opacity");
    }
    made.texture = SampledTexture{.id = kTexture,
                                  .filter = static_cast<Filter>(kFilter),
                                  .address = static_cast<Address>(kAddress),
                                  .color = (kFeeds & 1U) != 0,
                                  .alpha = (kFeeds & 2U) != 0};
    // Its ranges are the document's: written as one and checked.
    if (!(made.alphaCutoff >= 0 && made.alphaCutoff <= 1)) {
        return invalid("a cooked material's alpha cutoff is from nought to one");
    }
    Material checked = made;
    checked.alphaCutoff = made.blend == Blend::Masked ? made.alphaCutoff : 0.5F;
    // An input the texture feeds holds its default.
    const Surface kDefaults;
    if ((made.texture.color && made.surface.baseColor != kDefaults.baseColor) ||
        (made.texture.alpha && made.surface.geometryOpacity != kDefaults.geometryOpacity)) {
        return invalid("a cooked material's input its texture feeds holds its default");
    }
    if (!validateSurface(documentOf(checked, 1)).has_value()) {
        return invalid("a cooked material is within the Surface contract's ranges");
    }
    return made;
}

std::array<float, kBlobFloats> blobOf(const Material& made) noexcept {
    const Surface& kSurface = made.surface;
    std::array<float, kBlobFloats> blob{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        blob[channel] = made.texture.color ? 1.0F : kSurface.baseColor[channel];
        blob[4 + channel] = kSurface.specularColor[channel] * kSurface.specularWeight;
        blob[8 + channel] = kSurface.emissionColor[channel] * kSurface.emissionLuminance;
    }
    blob[3] = kSurface.baseMetalness;
    blob[7] = kSurface.specularRoughness;
    blob[11] = kSurface.specularIor;
    blob[12] = kSurface.geometryOpacity;
    blob[13] = kSurface.ambientOcclusion;
    blob[14] = made.blend == Blend::Masked ? made.alphaCutoff : 0;
    blob[15] = static_cast<float>((made.shading == Shading::Unlit ? 1U : 0U) | (made.texture.color ? 2U : 0U) |
                                  (made.texture.alpha ? 4U : 0U));
    return blob;
}

} // namespace rawframe::material
