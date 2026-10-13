#include "functions.h"

#include "common.h"
#include "generated/checkerboard_function.h"
#include "generated/fractal_noise_2d_function.h"
#include "generated/fresnel_function.h"
#include "generated/polar_coordinates_function.h"
#include "generated/rotate_uv_function.h"
#include "generated/tile_and_offset_function.h"
#include "generated/triplanar_projection_function.h"
#include "generated/voronoi_2d_function.h"
#include "rawframe/material/material.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace rawframe::material::functions {

using document::Value;
using surface_graph::Carried;

namespace {

/// The engine's functions by their node types, each its document's text
/// (modules/material/library, written by tools/make_functions.py).
const std::array<std::pair<std::string_view, std::span<const std::uint8_t>>, 8> kLibrary = {{
    {"rawframe/checkerboard@1", kCheckerboardFunction},
    {"rawframe/fractal_noise_2d@1", kFractalNoise2dFunction},
    {"rawframe/fresnel@1", kFresnelFunction},
    {"rawframe/polar_coordinates@1", kPolarCoordinatesFunction},
    {"rawframe/rotate_uv@1", kRotateUvFunction},
    {"rawframe/tile_and_offset@1", kTileAndOffsetFunction},
    {"rawframe/triplanar_projection@1", kTriplanarProjectionFunction},
    {"rawframe/voronoi_2d@1", kVoronoi2dFunction},
}};

/// A function's outputs as its interface declares them: one to four, each
/// its type, in name order.
result::Result<std::vector<Output>> outputsOf(const graph::Document& function) {
    constexpr std::array<std::pair<std::string_view, Carried>, 6> kTypes = {{{"float", Carried::Float},
                                                                             {"vec2", Carried::Vec2},
                                                                             {"vec3", Carried::Vec3},
                                                                             {"color3", Carried::Color3},
                                                                             {"vec4", Carried::Vec4},
                                                                             {"color4", Carried::Color4}}};
    const Value& kOutputs = *function.interface.find("outputs");
    if (kOutputs.kind() != Value::Kind::Object || kOutputs.names().empty() || kOutputs.names().size() > 4) {
        return invalid("a function's interface has one to four outputs");
    }
    std::vector<Output> made;
    for (std::size_t at = 0; at < kOutputs.names().size(); ++at) {
        const Value& kOutput = kOutputs.items()[at];
        const Value* kType =
            kOutput.kind() == Value::Kind::Object && kOutput.names().size() == 1 ? kOutput.find("type") : nullptr;
        const auto kFound = kType != nullptr && kType->text() != nullptr
                                ? std::ranges::find(kTypes,
                                                    std::string_view{*kType->text()},
                                                    &std::pair<std::string_view, Carried>::first)
                                : kTypes.end();
        if (kFound == kTypes.end()) {
            return invalid("a function's output is its type alone, one of the graph's");
        }
        made.push_back(Output{.name = kOutputs.names()[at], .carried = kFound->second});
    }
    return made;
}

/// What a call gives a texture input (D713): the game's texture by 16
/// lowercase hex digits, or the caller's texture input.
result::Result<GivenTexture> textureGiven(const graph::Document& surface, const Value* given) {
    if (given != nullptr && given->text() != nullptr) {
        const std::optional<std::uint64_t> kId = graph::nodeIdOf(*given->text());
        if (!kId.has_value() || *kId == 0) {
            return invalid("a call gives a texture input the game's texture by 16 lowercase hex digits, never nought");
        }
        return GivenTexture{.id = *kId};
    }
    const std::optional<graph::Connection> kFrom =
        given != nullptr ? graph::connectionOf(*given) : std::optional<graph::Connection>{};
    const graph::Node* kSource = kFrom.has_value() ? nodeOf(surface, kFrom->node) : nullptr;
    if (kSource == nullptr || !surface_graph::known(*kSource) ||
        surface_graph::typeOf(*kSource) != kParameterTexture2dType || kFrom->output != "texture") {
        return invalid("a call gives each texture input a texture, or a parameter_texture_2d node's texture");
    }
    RAWFRAME_TRY_ASSIGN(const surface_graph::InterfaceInput kInput, surface_graph::parameterIn(surface, *kSource));
    return GivenTexture{.id = kInput.texture, .input = kInput.slot};
}

} // namespace

std::vector<std::string_view> types() {
    std::vector<std::string_view> made;
    for (const auto& [kType, kText] : kLibrary) {
        made.push_back(kType);
    }
    return made;
}

bool isFunction(std::string_view type) {
    return std::ranges::find(kLibrary, type, &std::pair<std::string_view, std::span<const std::uint8_t>>::first) !=
           kLibrary.end();
}

result::Result<Function> functionOf(std::string_view type) {
    const auto kFound =
        std::ranges::find(kLibrary, type, &std::pair<std::string_view, std::span<const std::uint8_t>>::first);
    if (kFound == kLibrary.end()) {
        return invalid("a function is one of the engine's");
    }
    return readFunction({reinterpret_cast<const char*>(kFound->second.data()), kFound->second.size()});
}

result::Result<Function> readFunction(std::string_view text) {
    auto read = graph::readDocument(text, "function");
    if (!read.has_value()) {
        return asMaterial(std::move(read).error());
    }
    auto written = graph::writeDocument(*read);
    if (!written.has_value() || *written != text) {
        return invalid("a function is in its canonical form");
    }
    Function made{.document = std::move(*read)};
    const graph::Document& kDocument = made.document;
    RAWFRAME_TRY_ASSIGN(made.inputs, surface_graph::interfaceOf(kDocument));
    RAWFRAME_TRY_ASSIGN(made.outputs, outputsOf(kDocument));
    std::vector<bool> fed(made.outputs.size(), false);
    for (const graph::Node& node : kDocument.nodes) {
        if (!surface_graph::known(node)) {
            return invalid("a function's nodes are of the atomic core");
        }
        const std::string_view kType = surface_graph::typeOf(node);
        if (kType == kSurfaceType || kType == kQualitySwitchType || kType == kInlineSlangType || isFunction(kType)) {
            return invalid("a function has no surface, quality switch, inline Slang, or call of another function");
        }
        if (kType != kOutputType) {
            RAWFRAME_TRY(surface_graph::nodeChecked(kDocument, node));
            continue;
        }
        // An output node: its output's name, and what feeds it.
        constexpr std::array<std::string_view, 1> kParams = {"name"};
        constexpr std::array<std::string_view, 1> kInputs = {"in"};
        RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
        const Value* kName = kParts.first->find("name");
        const Value* kIn = kParts.second->find("in");
        const auto kOutput = kName != nullptr && kName->text() != nullptr
                                 ? std::ranges::find(made.outputs, std::string_view{*kName->text()}, &Output::name)
                                 : made.outputs.end();
        if (kOutput == made.outputs.end() || kIn == nullptr) {
            return invalid("an output node names one of its function's outputs, its input given");
        }
        const auto kAt = static_cast<std::size_t>(kOutput - made.outputs.begin());
        if (fed.at(kAt)) {
            return invalid("a function's output is fed by one output node");
        }
        fed.at(kAt) = true;
        RAWFRAME_TRY_ASSIGN(kOutput->from, surface_graph::operandOf(kDocument, *kIn));
        // Three numbers are a vec3's as well as a color3's.
        const bool kFits = kOutput->from.carried == kOutput->carried ||
                           (!kOutput->from.from.has_value() &&
                            ((kOutput->from.carried == Carried::Color3 && kOutput->carried == Carried::Vec3) ||
                             (kOutput->from.carried == Carried::Color4 && kOutput->carried == Carried::Vec4)));
        if (!kFits) {
            return invalid("an output node is fed what its output's type says");
        }
    }
    if (!std::ranges::all_of(fed, std::identity{})) {
        return invalid("each of a function's outputs is fed by an output node");
    }
    return made;
}

result::Result<Call> callIn(const graph::Document& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(Function function, functionOf(surface_graph::typeOf(node)));
    std::vector<std::string_view> names;
    for (const surface_graph::InterfaceInput& kInput : function.inputs) {
        names.push_back(kInput.name);
    }
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, names));
    Call made{.given = std::vector<std::optional<surface_graph::Operand>>(function.inputs.size()),
              .textures = std::vector<GivenTexture>(function.inputs.size())};
    for (std::size_t at = 0; at < function.inputs.size(); ++at) {
        const Value* kGiven = kParts.second->find(function.inputs[at].name);
        if (function.inputs[at].carried == Carried::Texture2d) {
            RAWFRAME_TRY_ASSIGN(made.textures[at], textureGiven(surface, kGiven));
            continue;
        }
        if (kGiven == nullptr) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(surface_graph::Operand operand, surface_graph::operandOf(surface, *kGiven));
        const Carried kWanted = function.inputs[at].carried;
        if (!operand.from.has_value() && operand.carried == Carried::Color3 && kWanted == Carried::Vec3) {
            operand.carried = Carried::Vec3;
        }
        if (!operand.from.has_value() && operand.carried == Carried::Color4 && kWanted == Carried::Vec4) {
            operand.carried = Carried::Vec4;
        }
        if (operand.carried.has_value() && *operand.carried != kWanted) {
            return invalid("a call's input is of its function's input's type");
        }
        made.given[at] = std::move(operand);
    }
    made.function = std::move(function);
    return made;
}

} // namespace rawframe::material::functions

namespace rawframe::material {

result::Result<graph::Document> readFunction(std::string_view text) {
    RAWFRAME_TRY_ASSIGN(functions::Function read, functions::readFunction(text));
    return std::move(read.document);
}

} // namespace rawframe::material
