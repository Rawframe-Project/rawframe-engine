#include "rawframe/material/canvas.h"

#include "common.h"
#include "fold.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <utility>

namespace rawframe::material {

namespace {

using document::Value;

constexpr FoldDomain kDomain{.called = "a canvas material", .output = kCanvasOutputType, .coordinates = kUvType};

constexpr std::array<std::string_view, 3> kBlends = {"normal", "additive", "multiply"};

constexpr std::size_t kEncodedBytes = 8 + 4 + (16 * 4) + 8 + 4 + (4 * 4);

/// The states section's shading and blend, checked.
result::Result<std::pair<Shading, CanvasBlend>> statesIn(const graph::Document& material) {
    const auto kFound = std::ranges::find(material.sections, std::string_view{"states"}, [](const auto& section) {
        return std::string_view{section.first};
    });
    if (kFound == material.sections.end()) {
        return std::pair{Shading::Lit, CanvasBlend::Normal};
    }
    const Value& kStates = kFound->second;
    if (kStates.kind() != Value::Kind::Object || kStates.names().empty()) {
        return invalid("a canvas material's states are an object of its shading and its blend, left out when both "
                       "are their defaults");
    }
    std::pair made{Shading::Lit, CanvasBlend::Normal};
    for (const std::string& kName : kStates.names()) {
        const std::string* kValue = kStates.find(kName)->text();
        if (kName == "shading" && kValue != nullptr && *kValue == "unlit") {
            made.first = Shading::Unlit;
        } else if (const auto kBlend = kValue != nullptr ? std::ranges::find(kBlends, *kValue) : kBlends.end();
                   kName == "blend" && kBlend != kBlends.end() && kBlend != kBlends.begin()) {
            made.second = static_cast<CanvasBlend>(kBlend - kBlends.begin());
        } else {
            return invalid("a canvas material's shading is unlit and its blend additive or multiply, when not their "
                           "defaults");
        }
    }
    return made;
}

/// The output's inputs, checked: `color` a color4, `emission` and
/// `normal_map` color3s; a literal color's alpha from nought to one and an
/// emission never below nought, neither at its default.
result::Status checkOutput(const graph::Node& output, Checker& checker) {
    constexpr std::array<std::string_view, 3> kInputs = {"color", "emission", "normal_map"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(output, {}, kInputs));
    for (const std::string_view kInput : kInputs) {
        const Value* kValue = kParts.second->find(kInput);
        if (kValue == nullptr) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const Operand kOperand, checker.operandOf(*kValue));
        const Carried kWanted = kInput == "color" ? Carried::Color4 : Carried::Color3;
        if (kOperand.carried.has_value() && *kOperand.carried != kWanted) {
            return invalid("a canvas material's color is a color4, and its emission and normal map color3s");
        }
        if (kOperand.from.has_value() || kInput == "normal_map") {
            continue;
        }
        const std::array<double, 4>& kValues = kOperand.literal.values;
        if (kInput == "color" &&
            (!(kValues[3] >= 0 && kValues[3] <= 1) || kValues == std::array<double, 4>{1, 1, 1, 1})) {
            return invalid("a canvas material's color literal has an alpha from nought to one, and is left out at "
                           "white");
        }
        if (kInput == "emission" && (std::ranges::any_of(std::span{kValues}.first(3),
                                                         [](double each) {
                                                             return each < 0;
                                                         }) ||
                                     std::ranges::all_of(std::span{kValues}.first(3), [](double each) {
                                         return each == 0;
                                     }))) {
            return invalid("a canvas material's emission literal is never below nought, and is left out at black");
        }
    }
    return {};
}

bool allZero(std::span<const float> values) {
    return std::ranges::all_of(values, [](float each) {
        return each == 0;
    });
}

} // namespace

result::Status validateCanvas(const graph::Document& material, const graph::Limits& limits) {
    if (auto valid = graph::validate(material, limits); !valid.has_value()) {
        return asMaterial(std::move(valid).error());
    }
    if (material.kind != "canvas" || material.interface.kind() != Value::Kind::Object ||
        !material.interface.names().empty()) {
        return invalid("a canvas material is of kind canvas, with an empty interface");
    }
    for (const auto& [kName, kValue] : material.sections) {
        if (kName != "states") {
            return invalid("a canvas material's one section is its states");
        }
    }
    RAWFRAME_TRY(statesIn(material));
    const graph::Node* kOutput = outputOf(material, kDomain);
    if (kOutput == nullptr) {
        return invalid("a canvas material has one canvas node");
    }
    Checker checker{material, kDomain};
    for (const graph::Node& node : material.nodes) {
        RAWFRAME_TRY(checker.check(node));
    }
    return checkOutput(*kOutput, checker);
}

result::Result<std::string> writeCanvas(const graph::Document& material, const graph::Limits& limits) {
    RAWFRAME_TRY(validateCanvas(material, limits));
    auto written = graph::writeDocument(material, limits);
    if (!written.has_value()) {
        return asMaterial(std::move(written).error());
    }
    return std::move(*written);
}

result::Result<graph::Document> readCanvas(std::string_view text, const graph::Limits& limits) {
    constexpr std::array<std::string_view, 1> kSections = {"states"};
    auto read = graph::readDocument(text, "canvas", kSections, limits);
    if (!read.has_value()) {
        return asMaterial(std::move(read).error());
    }
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeCanvas(*read, limits));
    if (kWritten != text) {
        return invalid("a canvas material is not in its canonical form");
    }
    return std::move(*read);
}

result::Result<CanvasMaterial> compileCanvas(const graph::Document& material) {
    RAWFRAME_TRY(validateCanvas(material));
    CanvasMaterial made;
    RAWFRAME_TRY_ASSIGN(std::tie(made.shading, made.blend), statesIn(material));
    const Value& kInputs = *outputOf(material, kDomain)->record.find("inputs");
    if (kInputs.find("normal_map") != nullptr) {
        return unsupported("a canvas material's normal map waits for 2D lights");
    }
    Folder folder{material, kDomain};
    if (const Value* kColor = kInputs.find("color"); kColor != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Form kForm, folder.formOf(*kColor));
        for (std::size_t channel = 0; channel < 4; ++channel) {
            made.color.at(channel) = static_cast<float>(kForm.constant.at(channel));
            made.colorTexture.at(channel) = static_cast<float>(kForm.texture.at(channel));
        }
    }
    if (const Value* kEmission = kInputs.find("emission"); kEmission != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Form kForm, folder.formOf(*kEmission));
        for (std::size_t channel = 0; channel < 3; ++channel) {
            made.emission.at(channel) = static_cast<float>(kForm.constant.at(channel));
            made.emissionTexture.at(channel) = static_cast<float>(kForm.texture.at(channel));
        }
    }
    if (!allZero(made.colorTexture) || !allZero(made.emissionTexture)) {
        made.sampled = folder.sampled();
    }
    for (const std::span<const float> kValues : {std::span<const float>{made.color},
                                                 std::span<const float>{made.colorTexture},
                                                 std::span<const float>{made.emission},
                                                 std::span<const float>{made.emissionTexture},
                                                 std::span<const float>{made.sampled.scale},
                                                 std::span<const float>{made.sampled.offset}}) {
        if (!std::ranges::all_of(kValues, [](float each) {
                return std::isfinite(each);
            })) {
            return invalid("a canvas material folds to finite numbers");
        }
    }
    return made;
}

std::vector<std::byte> encodeCanvas(const CanvasMaterial& made) {
    std::vector<std::byte> bytes;
    bytes.reserve(kEncodedBytes);
    for (const char kLetter : std::string_view{"RFCM"}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 1);
    putWord(bytes, static_cast<std::uint32_t>(made.shading) | (static_cast<std::uint32_t>(made.blend) << 8U));
    for (const float kValue : made.color) {
        putFloat(bytes, kValue);
    }
    for (const float kValue : made.colorTexture) {
        putFloat(bytes, kValue);
    }
    for (const std::array<float, 3>& kThree : {made.emission, made.emissionTexture}) {
        for (const float kValue : kThree) {
            putFloat(bytes, kValue);
        }
        putFloat(bytes, 0);
    }
    putWord(bytes, static_cast<std::uint32_t>(made.sampled.id & 0xFFFFFFFFU));
    putWord(bytes, static_cast<std::uint32_t>(made.sampled.id >> 32U));
    putWord(bytes,
            static_cast<std::uint32_t>(made.sampled.filter) | (static_cast<std::uint32_t>(made.sampled.address) << 8U));
    for (const float kValue :
         {made.sampled.scale[0], made.sampled.scale[1], made.sampled.offset[0], made.sampled.offset[1]}) {
        putFloat(bytes, kValue);
    }
    return bytes;
}

result::Result<CanvasMaterial> decodeCanvas(std::span<const std::byte> bytes) {
    const auto kWordAt = [&bytes](std::size_t at) {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at + each]) << (each * 8);
        }
        return word;
    };
    const auto kFloatAt = [&kWordAt](std::size_t at) {
        return std::bit_cast<float>(kWordAt(at));
    };
    const std::uint32_t kStates = bytes.size() == kEncodedBytes ? kWordAt(8) : 0;
    if (bytes.size() != kEncodedBytes || std::string_view{reinterpret_cast<const char*>(bytes.data()), 4} != "RFCM" ||
        kWordAt(4) != 1 || (kStates & 0xFFU) > static_cast<std::uint32_t>(Shading::Unlit) ||
        ((kStates >> 8U) & 0xFFU) > static_cast<std::uint32_t>(CanvasBlend::Multiply) || (kStates >> 16U) != 0) {
        return invalid("a cooked canvas material is RFCM, format 1, its shading and blend in their sets");
    }
    CanvasMaterial made{.shading = static_cast<Shading>(kStates & 0xFFU),
                        .blend = static_cast<CanvasBlend>((kStates >> 8U) & 0xFFU)};
    std::array<float, 16> numbers{};
    for (std::size_t at = 0; at < numbers.size(); ++at) {
        numbers.at(at) = kFloatAt(12 + (at * 4));
    }
    std::ranges::copy(std::span{numbers}.subspan(0, 4), made.color.begin());
    std::ranges::copy(std::span{numbers}.subspan(4, 4), made.colorTexture.begin());
    std::ranges::copy(std::span{numbers}.subspan(8, 3), made.emission.begin());
    std::ranges::copy(std::span{numbers}.subspan(12, 3), made.emissionTexture.begin());
    constexpr std::size_t kTexture = 12 + (16 * 4);
    made.sampled.id = std::uint64_t{kWordAt(kTexture)} | (std::uint64_t{kWordAt(kTexture + 4)} << 32U);
    const std::uint32_t kState = kWordAt(kTexture + 8);
    made.sampled.filter = static_cast<Filter>(kState & 0xFFU);
    made.sampled.address = static_cast<Address>((kState >> 8U) & 0xFFU);
    made.sampled.scale = {kFloatAt(kTexture + 12), kFloatAt(kTexture + 16)};
    made.sampled.offset = {kFloatAt(kTexture + 20), kFloatAt(kTexture + 24)};
    const bool kFinite = std::ranges::all_of(numbers,
                                             [](float each) {
                                                 return std::isfinite(each);
                                             }) &&
                         std::isfinite(made.sampled.scale[0]) && std::isfinite(made.sampled.scale[1]) &&
                         std::isfinite(made.sampled.offset[0]) && std::isfinite(made.sampled.offset[1]);
    const bool kTextured = !allZero(made.colorTexture) || !allZero(made.emissionTexture);
    if (!kFinite || numbers[11] != 0 || numbers[15] != 0 || kState > 0x101U || (kState & 0xFEFEU) != 0 ||
        kTextured != (made.sampled.id != 0) || (made.sampled.id == 0 && made.sampled != SampledTexture{})) {
        return invalid("a cooked canvas material holds finite numbers, and a texture exactly where it samples one");
    }
    return made;
}

} // namespace rawframe::material
