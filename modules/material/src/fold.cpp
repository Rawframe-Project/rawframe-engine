#include "fold.h"

#include "common.h"
#include "rawframe/material/post_process.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace rawframe::material {

namespace {

using document::Value;

std::string_view typeOf(const graph::Node& node) {
    return *node.record.find("type")->text();
}

bool known(const FoldDomain& domain, const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    if (kText == nullptr) {
        return false;
    }
    return *kText == domain.output || *kText == domain.coordinates || *kText == kSampleTexture2dType ||
           *kText == kMultiplyType || *kText == kAddType || *kText == kCombine4Type ||
           (domain.picture && (*kText == kSceneColorType || *kText == kSceneDepthType));
}

std::string refusal(const FoldDomain& domain, std::string_view rest) {
    return std::string{domain.called} + std::string{rest};
}

std::optional<Literal> literalOf(const Value& value) {
    const auto kFinite = [](const Value& each) -> std::optional<double> {
        const std::optional<double> kReal = each.kind() == Value::Kind::Number ? each.real() : std::nullopt;
        return kReal.has_value() && std::isfinite(*kReal) ? kReal : std::nullopt;
    };
    if (const std::optional<double> kOne = kFinite(value); kOne.has_value()) {
        return Literal{.values = {*kOne, *kOne, *kOne, *kOne}, .carried = Carried::Float};
    }
    const std::size_t kCount = value.kind() == Value::Kind::Array ? value.items().size() : 0;
    if (kCount < 2 || kCount > 4) {
        return std::nullopt;
    }
    Literal made{.carried = kCount == 2 ? Carried::Vec2 : kCount == 3 ? Carried::Color3 : Carried::Color4};
    for (std::size_t at = 0; at < kCount; ++at) {
        const std::optional<double> kChannel = kFinite(value.items()[at]);
        if (!kChannel.has_value()) {
            return std::nullopt;
        }
        made.values.at(at) = *kChannel;
    }
    return made;
}

/// A color3's fourth channel is nought.
Form tidied(Form made) {
    if (made.carried == Carried::Color3) {
        made.constant[3] = 0;
        made.scene[3] = 0;
        made.texture[3] = 0;
        made.both[3] = 0;
    }
    return made;
}

} // namespace

Checker::Checker(const graph::Document& document, const FoldDomain& domain) noexcept
    : document_(document), domain_(domain) {
}

result::Result<std::optional<Carried>> Checker::carriedBy(const graph::Connection& from) {
    const auto kKey = std::pair{from.node, from.output};
    if (const auto kSeen = seen_.find(kKey); kSeen != seen_.end()) {
        return kSeen->second;
    }
    RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, find(from));
    seen_.emplace(kKey, kCarried);
    return kCarried;
}

result::Result<Operand> Checker::operandOf(const Value& value) {
    Operand made;
    made.from = graph::connectionOf(value);
    if (made.from.has_value()) {
        RAWFRAME_TRY_ASSIGN(made.carried, carriedBy(*made.from));
        return made;
    }
    const std::optional<Literal> kLiteral = literalOf(value);
    if (!kLiteral.has_value()) {
        return invalid(refusal(domain_, "'s literal is one to four finite numbers"));
    }
    made.literal = *kLiteral;
    made.carried = kLiteral->carried;
    return made;
}

result::Result<std::pair<Operand, Operand>>
Checker::pairIn(const graph::Node& node, std::string_view first, std::string_view second) {
    const std::array<std::string_view, 2> kInputs = {first, second};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    const Value* kFirst = kParts.second->find(first);
    const Value* kSecond = kParts.second->find(second);
    if (kFirst == nullptr || kSecond == nullptr) {
        return invalid(refusal(domain_, "'s math and combine4 nodes have both their inputs"));
    }
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(*kFirst));
    RAWFRAME_TRY_ASSIGN(Operand other, operandOf(*kSecond));
    return std::pair{std::move(made), std::move(other)};
}

/// A `multiply` or `add` node's out: its inputs' type, or the one not a
/// float when the other is.
result::Result<std::optional<Carried>> Checker::mathOut(const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kPair, pairIn(node, "a", "b"));
    if (!kPair.first.carried.has_value() || !kPair.second.carried.has_value()) {
        return std::optional<Carried>{};
    }
    const Carried kA = *kPair.first.carried;
    const Carried kB = *kPair.second.carried;
    if (kA != kB && kA != Carried::Float && kB != Carried::Float) {
        return invalid("a math node's inputs are of one type, or one of them is a float");
    }
    return std::optional{kA == Carried::Float ? kB : kA};
}

/// A `combine4` node's inputs: a color3 and a float.
result::Status Checker::combineIn(const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kPair, pairIn(node, "in1", "in2"));
    if ((kPair.first.carried.has_value() && *kPair.first.carried != Carried::Color3) ||
        (kPair.second.carried.has_value() && *kPair.second.carried != Carried::Float)) {
        return invalid("a combine4 node's in1 is a color3 and its in2 a float");
    }
    return {};
}

result::Result<std::pair<SampledTexture, std::optional<graph::Connection>>> Checker::sampleIn(const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kParams = {"address", "filter", "texture"};
    constexpr std::array<std::string_view, 1> kInputs = {"uv"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    SampledTexture made;
    const Value* kTexture = kParts.first->find("texture");
    const std::optional<std::uint64_t> kId =
        kTexture != nullptr && kTexture->text() != nullptr ? graph::nodeIdOf(*kTexture->text()) : std::nullopt;
    if (!kId.has_value() || *kId == 0) {
        return invalid("a sampled texture names the game's texture by 16 lowercase hex digits, never nought");
    }
    made.id = *kId;
    if (const Value* kAddress = kParts.first->find("address"); kAddress != nullptr) {
        if (kAddress->text() == nullptr || *kAddress->text() != "clamp") {
            return invalid("a sampled texture's address is clamp, when not the default");
        }
        made.address = Address::Clamp;
    }
    if (const Value* kFilter = kParts.first->find("filter"); kFilter != nullptr) {
        if (kFilter->text() == nullptr || *kFilter->text() != "nearest") {
            return invalid("a sampled texture's filter is nearest, when not the default");
        }
        made.filter = Filter::Nearest;
    }
    std::optional<graph::Connection> uv;
    if (const Value* kUv = kParts.second->find("uv"); kUv != nullptr) {
        uv = graph::connectionOf(*kUv);
        if (!uv.has_value()) {
            return invalid("a sampled texture's uv is connected, or left out for the default coordinates");
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, carriedBy(*uv));
        if (kCarried.has_value() && *kCarried != Carried::Vec2) {
            return invalid("a sampled texture's uv comes from a vec2 output");
        }
    }
    return std::pair{made, uv};
}

result::Status Checker::check(const graph::Node& node) {
    if (!known(domain_, node)) {
        return {};
    }
    const std::string_view kType = typeOf(node);
    if (kType == domain_.output) {
        return {};
    }
    if (kType == kSceneColorType || kType == kSceneDepthType || kType == domain_.coordinates) {
        RAWFRAME_TRY(partsOf(node, {}, {}));
        return {};
    }
    if (kType == kSampleTexture2dType) {
        RAWFRAME_TRY(sampleIn(node));
        return {};
    }
    if (kType == kCombine4Type) {
        return combineIn(node);
    }
    RAWFRAME_TRY(mathOut(node));
    return {};
}

result::Result<std::optional<Carried>> Checker::find(const graph::Connection& from) {
    const graph::Node* kSource = nodeOf(document_, from.node);
    if (kSource == nullptr || !known(domain_, *kSource)) {
        return std::optional<Carried>{};
    }
    const std::string_view kType = typeOf(*kSource);
    const std::string_view kOutput = from.output;
    if (kType == kSceneColorType && kOutput == "color") {
        return std::optional{Carried::Color4};
    }
    if (kType == kSceneDepthType && kOutput == "depth") {
        return std::optional{Carried::Float};
    }
    if (kType == domain_.coordinates && kOutput == "uv") {
        return std::optional{Carried::Vec2};
    }
    if (kType == kSampleTexture2dType && (kOutput == "color" || kOutput == "alpha")) {
        return std::optional{kOutput == "color" ? Carried::Color3 : Carried::Float};
    }
    if ((kType == kMultiplyType || kType == kAddType) && kOutput == "out") {
        return mathOut(*kSource);
    }
    if (kType == kCombine4Type && kOutput == "out") {
        RAWFRAME_TRY(combineIn(*kSource));
        return std::optional{Carried::Color4};
    }
    return invalid("a connection names an output its node has");
}

Folder::Folder(const graph::Document& document, const FoldDomain& domain) noexcept
    : document_(document), domain_(domain), checker_(document, domain) {
}

result::Result<Form> Folder::formOf(const Value& value) {
    const std::optional<graph::Connection> kFrom = graph::connectionOf(value);
    if (kFrom.has_value()) {
        return formOf(*kFrom);
    }
    const std::optional<Literal> kLiteral = literalOf(value);
    if (kLiteral->carried == Carried::Vec2) {
        return unsupported("a vec2 feeds only a sampled texture's coordinates in generation 1");
    }
    Form made{.carried = kLiteral->carried, .constant = kLiteral->values};
    if (made.carried == Carried::Color3) {
        made.constant[3] = 0;
    }
    return made;
}

result::Result<Form> Folder::formOf(const graph::Connection& from) {
    const auto kKey = std::pair{from.node, from.output};
    if (const auto kSeen = seen_.find(kKey); kSeen != seen_.end()) {
        return kSeen->second;
    }
    RAWFRAME_TRY_ASSIGN(const Form kMade, fold(from));
    seen_.emplace(kKey, kMade);
    return kMade;
}

const SampledTexture& Folder::sampled() const noexcept {
    return sampled_;
}

result::Result<Form> Folder::fold(const graph::Connection& from) {
    const graph::Node& kNode = *nodeOf(document_, from.node);
    if (!known(domain_, kNode)) {
        return unsupported("a node of a type generation 1 does not know waits for the node library");
    }
    const std::string_view kType = typeOf(kNode);
    if (kType == kSceneColorType) {
        return Form{.carried = Carried::Color4, .constant = {0, 0, 0, 1}, .scene = {1, 1, 1, 0}};
    }
    if (kType == kSceneDepthType) {
        return unsupported("the picture's depth waits for the node library");
    }
    if (kType == domain_.coordinates) {
        return unsupported("the coordinates feed only a sampled texture in generation 1");
    }
    if (kType == kSampleTexture2dType) {
        RAWFRAME_TRY_ASSIGN(const auto kSampling, checker_.sampleIn(kNode));
        SampledTexture texture = kSampling.first;
        if (kSampling.second.has_value()) {
            RAWFRAME_TRY_ASSIGN(const Affine kMap, affineOf(*kSampling.second));
            for (std::size_t axis = 0; axis < 2; ++axis) {
                texture.scale.at(axis) = static_cast<float>(kMap.scale.at(axis));
                texture.offset.at(axis) = static_cast<float>(kMap.offset.at(axis));
            }
        }
        if (sampler_.has_value() && *sampler_ != kNode.id) {
            return unsupported(refusal(domain_, " samples one texture in generation 1"));
        }
        sampler_ = kNode.id;
        sampled_ = texture;
        if (from.output == "color") {
            return Form{.carried = Carried::Color3, .texture = {1, 1, 1, 0}};
        }
        return Form{.carried = Carried::Float, .texture = {0, 0, 0, 1}, .alphaOnly = true};
    }
    if (kType == kCombine4Type) {
        const Value& kInputs = *kNode.record.find("inputs");
        RAWFRAME_TRY_ASSIGN(const Form kColor, formOf(*kInputs.find("in1")));
        RAWFRAME_TRY_ASSIGN(const Form kAlpha, formOf(*kInputs.find("in2")));
        Form made = kColor;
        made.carried = Carried::Color4;
        made.constant[3] = kAlpha.constant[3];
        made.texture[3] = kAlpha.texture[3];
        return made;
    }
    const Value& kInputs = *kNode.record.find("inputs");
    RAWFRAME_TRY_ASSIGN(const Form kA, formOf(*kInputs.find("a")));
    RAWFRAME_TRY_ASSIGN(const Form kB, formOf(*kInputs.find("b")));
    return kType == kAddType ? sum(kA, kB) : product(kA, kB);
}

/// The type a math node's out carries, and whether a float of the
/// texture's alpha would reach a color's channel.
result::Result<Carried> Folder::outOf(const Form& a, const Form& b) const {
    const Carried kOut = a.carried == Carried::Float ? b.carried : a.carried;
    if (kOut != Carried::Float && (a.alphaOnly || b.alphaOnly)) {
        return unsupported(refusal(domain_, "'s texture alpha folds only into its alpha in generation 1"));
    }
    return kOut;
}

result::Result<Form> Folder::sum(const Form& a, const Form& b) const {
    RAWFRAME_TRY_ASSIGN(const Carried kOut, outOf(a, b));
    Form made{.carried = kOut, .alphaOnly = a.alphaOnly || b.alphaOnly};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        made.constant.at(channel) = a.constant.at(channel) + b.constant.at(channel);
        made.scene.at(channel) = a.scene.at(channel) + b.scene.at(channel);
        made.texture.at(channel) = a.texture.at(channel) + b.texture.at(channel);
        made.both.at(channel) = a.both.at(channel) + b.both.at(channel);
    }
    return tidied(made);
}

result::Result<Form> Folder::product(const Form& a, const Form& b) const {
    RAWFRAME_TRY_ASSIGN(const Carried kOut, outOf(a, b));
    Form made{.carried = kOut, .alphaOnly = a.alphaOnly || b.alphaOnly};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        const double kA1 = a.constant.at(channel);
        const double kAs = a.scene.at(channel);
        const double kAt = a.texture.at(channel);
        const double kAst = a.both.at(channel);
        const double kB1 = b.constant.at(channel);
        const double kBs = b.scene.at(channel);
        const double kBt = b.texture.at(channel);
        const double kBst = b.both.at(channel);
        if ((kAs != 0 && (kBs != 0 || kBst != 0)) || (kAt != 0 && (kBt != 0 || kBst != 0)) ||
            (kAst != 0 && (kBs != 0 || kBt != 0 || kBst != 0))) {
            return unsupported(refusal(
                domain_, " multiplies the picture and the texture each at most once in a term in generation 1"));
        }
        made.constant.at(channel) = kA1 * kB1;
        made.scene.at(channel) = (kA1 * kBs) + (kAs * kB1);
        made.texture.at(channel) = (kA1 * kBt) + (kAt * kB1);
        made.both.at(channel) = (kA1 * kBst) + (kAst * kB1) + (kAs * kBt) + (kAt * kBs);
    }
    return tidied(made);
}

result::Result<Folder::Affine> Folder::affineOf(const graph::Connection& from) {
    const graph::Node& kNode = *nodeOf(document_, from.node);
    if (!known(domain_, kNode)) {
        return unsupported("a node of a type generation 1 does not know waits for the node library");
    }
    if (typeOf(kNode) == domain_.coordinates) {
        return Affine{};
    }
    if (typeOf(kNode) != kMultiplyType && typeOf(kNode) != kAddType) {
        return unsupported("a texture is sampled at its coordinates, scaled and moved, in generation 1");
    }
    RAWFRAME_TRY_ASSIGN(const auto kPair, checker_.pairIn(kNode, "a", "b"));
    if (kPair.first.from.has_value() == kPair.second.from.has_value()) {
        return unsupported("a texture's coordinates are scaled and moved by literals in generation 1");
    }
    const Operand& kConnected = kPair.first.from.has_value() ? kPair.first : kPair.second;
    const Operand& kLiteral = kPair.first.from.has_value() ? kPair.second : kPair.first;
    RAWFRAME_TRY_ASSIGN(Affine made, affineOf(*kConnected.from));
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const double kBy = kLiteral.literal.values.at(axis);
        if (typeOf(kNode) == kMultiplyType) {
            made.scale.at(axis) *= kBy;
            made.offset.at(axis) *= kBy;
        } else {
            made.offset.at(axis) += kBy;
        }
    }
    return made;
}

const graph::Node* outputOf(const graph::Document& document, const FoldDomain& domain) {
    const graph::Node* found = nullptr;
    for (const graph::Node& node : document.nodes) {
        const Value* kType = node.record.find("type");
        if (kType != nullptr && kType->text() != nullptr && *kType->text() == domain.output) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &node;
        }
    }
    return found;
}

void putWord(std::vector<std::byte>& bytes, std::uint32_t word) {
    for (std::size_t each = 0; each < 4; ++each) {
        bytes.push_back(static_cast<std::byte>((word >> (each * 8)) & 0xFFU));
    }
}

void putFloat(std::vector<std::byte>& bytes, float value) {
    putWord(bytes, std::bit_cast<std::uint32_t>(value));
}

} // namespace rawframe::material
