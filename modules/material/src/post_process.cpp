#include "rawframe/material/post_process.h"

#include "common.h"
#include "fold.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace rawframe::material {

namespace {

using document::Value;

constexpr std::array<std::string_view, 5> kInsertions = {
    "after_temporal", "before_tonemap", "after_tonemap", "scene_output", "final_output"};

constexpr FoldDomain kDomain{
    .called = "a post process", .output = kPostProcessOutputType, .coordinates = kScreenUvType, .picture = true};

/// The output's `color`, checked: a color4, a literal of it with an alpha
/// from nought to one, left out at nought.
result::Status checkColor(const graph::Document& process, Checker& checker) {
    constexpr std::array<std::string_view, 1> kInputs = {"color"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(*outputOf(process, kDomain), {}, kInputs));
    const Value* kColor = kParts.second->find("color");
    if (kColor == nullptr) {
        return {};
    }
    RAWFRAME_TRY_ASSIGN(const Operand kOperand, checker.operandOf(*kColor));
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
    if (outputOf(process, kDomain) == nullptr) {
        return invalid("a post process has one post_process node");
    }
    Checker checker{process, kDomain};
    for (const graph::Node& node : process.nodes) {
        RAWFRAME_TRY(checker.check(node));
    }
    return checkColor(process, checker);
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
    const Value* kColor = outputOf(process, kDomain)->record.find("inputs")->find("color");
    if (kColor == nullptr) {
        return made;
    }
    Folder folder{process, kDomain};
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
