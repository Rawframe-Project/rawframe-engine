#include "rawframe/material/post_process.h"

#include "common.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace rawframe::material {

namespace {

using document::Value;

/// What an output of a node this domain knows carries.
enum class Carried : std::uint8_t {
    Float,
    Vec2,
    Color3,
    Color4
};

constexpr std::array<std::string_view, 5> kInsertions = {
    "after_temporal", "before_tonemap", "after_tonemap", "scene_output", "final_output"};

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr &&
           (*kText == kPostProcessOutputType || *kText == kSceneColorType || *kText == kSceneDepthType ||
            *kText == kScreenUvType || *kText == kSampleTexture2dType || *kText == kMultiplyType ||
            *kText == kAddType || *kText == kCombine4Type);
}

std::string_view typeOf(const graph::Node& node) {
    return *node.record.find("type")->text();
}

/// A literal: one to four finite numbers, a float, a vec2, a color3, or a
/// color4; a float's number in every channel.
struct Literal {
    std::array<double, 4> values{};
    Carried carried = Carried::Float;
};

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

/// An input: connected, or a literal, and what it carries when known.
struct Operand {
    std::optional<graph::Connection> from;
    Literal literal;
    std::optional<Carried> carried;
};

/// The domain's rules over a document's nodes, each output's type found
/// once however many inputs it feeds.
class Checker {
public:
    explicit Checker(const graph::Document& process) noexcept : process_(process) {
    }

    /// What the output a connection names carries, if its node is of a
    /// type this domain knows; refused when the node has no such output.
    result::Result<std::optional<Carried>> carriedBy(const graph::Connection& from) {
        const auto kKey = std::pair{from.node, from.output};
        if (const auto kSeen = seen_.find(kKey); kSeen != seen_.end()) {
            return kSeen->second;
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, find(from));
        seen_.emplace(kKey, kCarried);
        return kCarried;
    }

    result::Result<Operand> operandOf(const Value& value) {
        Operand made;
        made.from = graph::connectionOf(value);
        if (made.from.has_value()) {
            RAWFRAME_TRY_ASSIGN(made.carried, carriedBy(*made.from));
            return made;
        }
        const std::optional<Literal> kLiteral = literalOf(value);
        if (!kLiteral.has_value()) {
            return invalid("a post process's literal is one to four finite numbers");
        }
        made.literal = *kLiteral;
        made.carried = kLiteral->carried;
        return made;
    }

    /// A node's two inputs `first` and `second`, both given.
    result::Result<std::pair<Operand, Operand>>
    pairIn(const graph::Node& node, std::string_view first, std::string_view second) {
        const std::array<std::string_view, 2> kInputs = {first, second};
        RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
        const Value* kFirst = kParts.second->find(first);
        const Value* kSecond = kParts.second->find(second);
        if (kFirst == nullptr || kSecond == nullptr) {
            return invalid("a post process's math and combine4 nodes have both their inputs");
        }
        RAWFRAME_TRY_ASSIGN(Operand made, operandOf(*kFirst));
        RAWFRAME_TRY_ASSIGN(Operand other, operandOf(*kSecond));
        return std::pair{std::move(made), std::move(other)};
    }

    /// A `multiply` or `add` node's out: its inputs' type, or the one not
    /// a float when the other is.
    result::Result<std::optional<Carried>> mathOut(const graph::Node& node) {
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
    result::Status combineIn(const graph::Node& node) {
        RAWFRAME_TRY_ASSIGN(const auto kPair, pairIn(node, "in1", "in2"));
        if ((kPair.first.carried.has_value() && *kPair.first.carried != Carried::Color3) ||
            (kPair.second.carried.has_value() && *kPair.second.carried != Carried::Float)) {
            return invalid("a combine4 node's in1 is a color3 and its in2 a float");
        }
        return {};
    }

    /// A `sample_texture_2d` node's texture and sampler state, and its uv,
    /// if connected, from a vec2.
    result::Result<std::pair<SampledTexture, std::optional<graph::Connection>>> sampleIn(const graph::Node& node) {
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
                return invalid("a sampled texture's uv is connected, or left out for the picture's coordinates");
            }
            RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, carriedBy(*uv));
            if (kCarried.has_value() && *kCarried != Carried::Vec2) {
                return invalid("a sampled texture's uv comes from a vec2 output");
            }
        }
        return std::pair{made, uv};
    }

    /// A node of this domain, checked; another is kept whole.
    result::Status check(const graph::Node& node) {
        if (!known(node)) {
            return {};
        }
        const std::string_view kType = typeOf(node);
        if (kType == kSceneColorType || kType == kSceneDepthType || kType == kScreenUvType) {
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
        if (kType == kMultiplyType || kType == kAddType) {
            RAWFRAME_TRY(mathOut(node));
            return {};
        }
        constexpr std::array<std::string_view, 1> kInputs = {"color"};
        RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
        const Value* kColor = kParts.second->find("color");
        if (kColor == nullptr) {
            return {};
        }
        RAWFRAME_TRY_ASSIGN(const Operand kOperand, operandOf(*kColor));
        if (kOperand.carried.has_value() && *kOperand.carried != Carried::Color4) {
            return invalid("a post process's color is a color4");
        }
        if (!kOperand.from.has_value()) {
            const std::array<double, 4>& kValues = kOperand.literal.values;
            if (!(kValues[3] >= 0 && kValues[3] <= 1) || std::ranges::all_of(kValues, [](double each) {
                    return each == 0;
                })) {
                return invalid("a post process's color literal has an alpha from nought to one, and is left out at "
                               "nought");
            }
        }
        return {};
    }

private:
    result::Result<std::optional<Carried>> find(const graph::Connection& from) {
        const graph::Node* kSource = nodeOf(process_, from.node);
        if (kSource == nullptr || !known(*kSource)) {
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
        if (kType == kScreenUvType && kOutput == "uv") {
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

    const graph::Document& process_;
    std::map<std::pair<graph::NodeId, std::string>, std::optional<Carried>> seen_;
};

const graph::Node* outputNode(const graph::Document& process) {
    const graph::Node* found = nullptr;
    for (const graph::Node& node : process.nodes) {
        const Value* kType = node.record.find("type");
        if (kType != nullptr && kType->text() != nullptr && *kType->text() == kPostProcessOutputType) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &node;
        }
    }
    return found;
}

/// The states section's insertion, checked.
result::Result<Insertion> insertionIn(const graph::Document& process) {
    const auto kFound = std::ranges::find(process.sections, std::string_view{"states"}, [](const auto& section) {
        return std::string_view{section.first};
    });
    if (kFound == process.sections.end()) {
        return Insertion::AfterTonemap;
    }
    const Value& kStates = kFound->second;
    const Value* kInsertion = kStates.kind() == Value::Kind::Object ? kStates.find("insertion") : nullptr;
    const auto kPlace = kInsertion != nullptr && kInsertion->text() != nullptr
                            ? std::ranges::find(kInsertions, *kInsertion->text())
                            : kInsertions.end();
    if (kStates.names().size() != 1 || kPlace == kInsertions.end() ||
        static_cast<Insertion>(kPlace - kInsertions.begin()) == Insertion::AfterTonemap) {
        return invalid("a post process's one state is its insertion, one of the five, left out at after_tonemap");
    }
    return static_cast<Insertion>(kPlace - kInsertions.begin());
}

/// A value folded (D348): per channel, a constant, and what multiplies the
/// picture's color, the texture's, and the two multiplied. The picture's
/// alpha is one, so a fourth channel holds only a constant and the
/// texture's alpha; a float's every channel is its number, but one of the
/// texture's alpha has it in the fourth alone.
struct Form {
    Carried carried = Carried::Float;
    std::array<double, 4> constant{};
    std::array<double, 4> scene{};
    std::array<double, 4> texture{};
    std::array<double, 4> both{};
    bool alphaOnly = false;
};

/// The picture's coordinates scaled and moved (SPEC-0026's
/// `tile_and_offset`, folded).
struct Affine {
    std::array<double, 2> scale{1, 1};
    std::array<double, 2> offset{0, 0};
};

class Folder {
public:
    explicit Folder(const graph::Document& process) noexcept : process_(process), checker_(process) {
    }

    result::Result<Form> formOf(const Value& value) {
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

    result::Result<Form> formOf(const graph::Connection& from) {
        const auto kKey = std::pair{from.node, from.output};
        if (const auto kSeen = seen_.find(kKey); kSeen != seen_.end()) {
            return kSeen->second;
        }
        RAWFRAME_TRY_ASSIGN(const Form kMade, fold(from));
        seen_.emplace(kKey, kMade);
        return kMade;
    }

    [[nodiscard]] const SampledTexture& sampled() const noexcept {
        return sampled_;
    }

private:
    result::Result<Form> fold(const graph::Connection& from) {
        const graph::Node& kNode = *nodeOf(process_, from.node);
        if (!known(kNode)) {
            return unsupported("a node of a type generation 1 does not know waits for the node library");
        }
        const std::string_view kType = typeOf(kNode);
        if (kType == kSceneColorType) {
            return Form{.carried = Carried::Color4, .constant = {0, 0, 0, 1}, .scene = {1, 1, 1, 0}};
        }
        if (kType == kSceneDepthType) {
            return unsupported("the picture's depth waits for the node library");
        }
        if (kType == kScreenUvType) {
            return unsupported("the picture's coordinates feed only a sampled texture in generation 1");
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
                return unsupported("a post process samples one texture in generation 1");
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
    static result::Result<Carried> outOf(const Form& a, const Form& b) {
        const Carried kOut = a.carried == Carried::Float ? b.carried : a.carried;
        if (kOut != Carried::Float && (a.alphaOnly || b.alphaOnly)) {
            return unsupported("a texture's alpha folds only into a post process's alpha in generation 1");
        }
        return kOut;
    }

    static result::Result<Form> sum(const Form& a, const Form& b) {
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

    static result::Result<Form> product(const Form& a, const Form& b) {
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
                return unsupported("a post process multiplies the picture and the texture each at most once in a "
                                   "term in generation 1");
            }
            made.constant.at(channel) = kA1 * kB1;
            made.scene.at(channel) = (kA1 * kBs) + (kAs * kB1);
            made.texture.at(channel) = (kA1 * kBt) + (kAt * kB1);
            made.both.at(channel) = (kA1 * kBst) + (kAst * kB1) + (kAs * kBt) + (kAt * kBs);
        }
        return tidied(made);
    }

    /// A color3's fourth channel is nought.
    static Form tidied(Form made) {
        if (made.carried == Carried::Color3) {
            made.constant[3] = 0;
            made.scene[3] = 0;
            made.texture[3] = 0;
            made.both[3] = 0;
        }
        return made;
    }

    result::Result<Affine> affineOf(const graph::Connection& from) {
        const graph::Node& kNode = *nodeOf(process_, from.node);
        if (!known(kNode)) {
            return unsupported("a node of a type generation 1 does not know waits for the node library");
        }
        if (typeOf(kNode) == kScreenUvType) {
            return Affine{};
        }
        if (typeOf(kNode) != kMultiplyType && typeOf(kNode) != kAddType) {
            return unsupported("a texture is sampled at the picture's coordinates, scaled and moved, in generation 1");
        }
        RAWFRAME_TRY_ASSIGN(const auto kPair, checker_.pairIn(kNode, "a", "b"));
        if (kPair.first.from.has_value() == kPair.second.from.has_value()) {
            return unsupported("the picture's coordinates are scaled and moved by literals in generation 1");
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

    const graph::Document& process_;
    Checker checker_;
    std::map<std::pair<graph::NodeId, std::string>, Form> seen_;
    std::optional<graph::NodeId> sampler_;
    SampledTexture sampled_;
};

void putWord(std::vector<std::byte>& bytes, std::uint32_t word) {
    for (std::size_t each = 0; each < 4; ++each) {
        bytes.push_back(static_cast<std::byte>((word >> (each * 8)) & 0xFFU));
    }
}

void putFloat(std::vector<std::byte>& bytes, float value) {
    putWord(bytes, std::bit_cast<std::uint32_t>(value));
}

constexpr std::size_t kEncodedBytes = 8 + 4 + (16 * 4) + 8 + 4 + (4 * 4);

} // namespace

result::Status validatePostProcess(const graph::Document& process, const graph::Limits& limits) {
    if (auto valid = graph::validate(process, limits); !valid.has_value()) {
        return asMaterial(std::move(valid).error());
    }
    if (process.kind != "post_process" || process.interface.kind() != Value::Kind::Object ||
        !process.interface.names().empty()) {
        return invalid("a post process is of kind post_process, with an empty interface");
    }
    for (const auto& [kName, kValue] : process.sections) {
        if (kName != "states") {
            return invalid("a post process's one section is its states");
        }
    }
    RAWFRAME_TRY(insertionIn(process));
    if (outputNode(process) == nullptr) {
        return invalid("a post process has one post_process node");
    }
    Checker checker{process};
    for (const graph::Node& node : process.nodes) {
        RAWFRAME_TRY(checker.check(node));
    }
    return {};
}

result::Result<std::string> writePostProcess(const graph::Document& process, const graph::Limits& limits) {
    RAWFRAME_TRY(validatePostProcess(process, limits));
    auto written = graph::writeDocument(process, limits);
    if (!written.has_value()) {
        return asMaterial(std::move(written).error());
    }
    return std::move(*written);
}

result::Result<graph::Document> readPostProcess(std::string_view text, const graph::Limits& limits) {
    constexpr std::array<std::string_view, 1> kSections = {"states"};
    auto read = graph::readDocument(text, "post_process", kSections, limits);
    if (!read.has_value()) {
        return asMaterial(std::move(read).error());
    }
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writePostProcess(*read, limits));
    if (kWritten != text) {
        return invalid("a post process is not in its canonical form");
    }
    return std::move(*read);
}

result::Result<PostProcess> compilePostProcess(const graph::Document& process) {
    RAWFRAME_TRY(validatePostProcess(process));
    PostProcess made;
    RAWFRAME_TRY_ASSIGN(made.insertion, insertionIn(process));
    const Value* kColor = outputNode(process)->record.find("inputs")->find("color");
    if (kColor == nullptr) {
        return made;
    }
    Folder folder{process};
    RAWFRAME_TRY_ASSIGN(const Form kForm, folder.formOf(*kColor));
    for (std::size_t channel = 0; channel < 4; ++channel) {
        made.constant.at(channel) = static_cast<float>(kForm.constant.at(channel));
        made.texture.at(channel) = static_cast<float>(kForm.texture.at(channel));
        if (channel < 3) {
            made.scene.at(channel) = static_cast<float>(kForm.scene.at(channel));
            made.both.at(channel) = static_cast<float>(kForm.both.at(channel));
        }
    }
    const auto kAllZero = [](std::span<const float> values) {
        return std::ranges::all_of(values, [](float each) {
            return each == 0;
        });
    };
    if (!kAllZero(made.texture) || !kAllZero(made.both)) {
        made.sampled = folder.sampled();
    }
    for (const std::span<const float> kValues : {std::span<const float>{made.constant},
                                                 std::span<const float>{made.scene},
                                                 std::span<const float>{made.texture},
                                                 std::span<const float>{made.both},
                                                 std::span<const float>{made.sampled.scale},
                                                 std::span<const float>{made.sampled.offset}}) {
        if (!std::ranges::all_of(kValues, [](float each) {
                return std::isfinite(each);
            })) {
            return invalid("a post process folds to finite numbers");
        }
    }
    return made;
}

std::vector<std::byte> encodePostProcess(const PostProcess& made) {
    std::vector<std::byte> bytes;
    bytes.reserve(kEncodedBytes);
    for (const char kLetter : std::string_view{"RFPP"}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 1);
    putWord(bytes, static_cast<std::uint32_t>(made.insertion));
    for (const float kValue : made.constant) {
        putFloat(bytes, kValue);
    }
    for (const float kValue : made.scene) {
        putFloat(bytes, kValue);
    }
    putFloat(bytes, 0);
    for (const float kValue : made.texture) {
        putFloat(bytes, kValue);
    }
    for (const float kValue : made.both) {
        putFloat(bytes, kValue);
    }
    putFloat(bytes, 0);
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

result::Result<PostProcess> decodePostProcess(std::span<const std::byte> bytes) {
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
    if (bytes.size() != kEncodedBytes || std::string_view{reinterpret_cast<const char*>(bytes.data()), 4} != "RFPP" ||
        kWordAt(4) != 1 || kWordAt(8) > static_cast<std::uint32_t>(Insertion::FinalOutput)) {
        return invalid("a cooked post process is RFPP, format 1, its insertion one of the five");
    }
    PostProcess made{.insertion = static_cast<Insertion>(kWordAt(8))};
    std::array<float, 16> numbers{};
    for (std::size_t at = 0; at < numbers.size(); ++at) {
        numbers.at(at) = kFloatAt(12 + (at * 4));
    }
    std::ranges::copy(std::span{numbers}.subspan(0, 4), made.constant.begin());
    std::ranges::copy(std::span{numbers}.subspan(4, 3), made.scene.begin());
    std::ranges::copy(std::span{numbers}.subspan(8, 4), made.texture.begin());
    std::ranges::copy(std::span{numbers}.subspan(12, 3), made.both.begin());
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
    const bool kTextured = std::ranges::any_of(made.texture,
                                               [](float each) {
                                                   return each != 0;
                                               }) ||
                           std::ranges::any_of(made.both, [](float each) {
                               return each != 0;
                           });
    if (!kFinite || numbers[7] != 0 || numbers[15] != 0 || kState > 0x101U || (kState & 0xFEFEU) != 0 ||
        kTextured != (made.sampled.id != 0) || (made.sampled.id == 0 && made.sampled != SampledTexture{})) {
        return invalid("a cooked post process holds finite numbers, and a texture exactly where it samples one");
    }
    return made;
}

std::array<float, kPostProcessBlobFloats> blobOf(const PostProcess& made) noexcept {
    std::array<float, kPostProcessBlobFloats> blob{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        blob.at(channel) = made.constant.at(channel);
        blob.at(8 + channel) = made.texture.at(channel);
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        blob.at(4 + channel) = made.scene.at(channel);
        blob.at(12 + channel) = made.both.at(channel);
    }
    blob[16] = made.sampled.scale[0];
    blob[17] = made.sampled.scale[1];
    blob[18] = made.sampled.offset[0];
    blob[19] = made.sampled.offset[1];
    return blob;
}

} // namespace rawframe::material
