#include "rawframe/material/instance.h"

#include "common.h"
#include "fold.h"
#include "surface_graph.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace rawframe::material {

using document::Value;

bool namesBase(std::string_view text) {
    const auto kRead = document::parse(text);
    return kRead.has_value() && kRead->kind() == Value::Kind::Object && kRead->find("base") != nullptr;
}

result::Result<InstanceDocument> readInstance(std::string_view text) {
    auto read = document::parseCanonical(text);
    if (!read.has_value()) {
        return invalid("a material instance is canonical JSON");
    }
    const Value& kRead = *read;
    constexpr std::array<std::string_view, 4> kMembers = {"formatVersion", "kind", "base", "overrides"};
    if (kRead.kind() != Value::Kind::Object || !std::ranges::equal(kRead.names(), kMembers)) {
        return invalid("a material instance is its formatVersion, kind, base, and overrides, in that order");
    }
    const Value& kVersion = kRead.items()[0];
    const Value& kKind = kRead.items()[1];
    const Value& kBase = kRead.items()[2];
    const Value& kOverrides = kRead.items()[3];
    if (kVersion.kind() != Value::Kind::Number || kVersion.integer() != 1 || kKind.text() == nullptr ||
        *kKind.text() != "surface") {
        return invalid("a material instance is of format 1 and kind surface");
    }
    // A path within the sources: names a slash apart, none empty or `..`.
    const std::string* kPath = kBase.text();
    bool named = kPath != nullptr && !kPath->empty() && kPath->size() <= 256 && kPath->front() != '/';
    for (std::size_t from = 0; named && from <= kPath->size();) {
        const std::size_t kTo = std::min(kPath->find('/', from), kPath->size());
        const std::string_view kName = std::string_view{*kPath}.substr(from, kTo - from);
        named = !kName.empty() && kName != "." && kName != ".." && kName.find('\\') == std::string_view::npos;
        from = kTo + 1;
    }
    if (!named) {
        return invalid("a material instance's base is a path within the sources, its names a slash apart");
    }
    if (kOverrides.kind() != Value::Kind::Object || kOverrides.names().empty() || kOverrides.names().size() > 8) {
        return invalid("a material instance overrides one to eight of its base's inputs");
    }
    return InstanceDocument{.base = *kPath, .overrides = kOverrides};
}

result::Result<std::array<float, kParameterFloats>> instanceParameters(const graph::Document& base,
                                                                       const Value& overrides) {
    RAWFRAME_TRY(validateSurface(base));
    RAWFRAME_TRY_ASSIGN(const std::vector<surface_graph::InterfaceInput> kInputs, surface_graph::interfaceOf(base));
    std::array<double, kParameterFloats> values{};
    for (const surface_graph::InterfaceInput& kInput : kInputs) {
        std::ranges::copy(kInput.initial, values.begin() + static_cast<std::ptrdiff_t>(kInput.slot * 4));
    }
    const graph::Document kNone;
    for (std::size_t at = 0; at < overrides.names().size(); ++at) {
        const auto kFound =
            std::ranges::find(kInputs, std::string_view{overrides.names()[at]}, &surface_graph::InterfaceInput::name);
        if (kFound == kInputs.end()) {
            return invalid("a material instance overrides its base's interface inputs alone");
        }
        RAWFRAME_TRY_ASSIGN(const surface_graph::Operand kValue,
                            surface_graph::operandOf(kNone, overrides.items()[at]));
        const surface_graph::Carried kRead =
            kFound->carried == surface_graph::Carried::Vec3   ? surface_graph::Carried::Color3
            : kFound->carried == surface_graph::Carried::Vec4 ? surface_graph::Carried::Color4
                                                              : kFound->carried;
        if (kValue.carried != kRead || kValue.from.has_value()) {
            return invalid("a material instance's override is of its input's type");
        }
        if (kValue.literal == kFound->initial) {
            return invalid("a material instance's override differs from its input's default, which it would repeat");
        }
        std::ranges::copy(kValue.literal, values.begin() + static_cast<std::ptrdiff_t>(kFound->slot * 4));
    }
    std::array<float, kParameterFloats> made{};
    std::ranges::transform(values, made.begin(), [](double each) {
        return static_cast<float>(each);
    });
    return made;
}

std::vector<std::byte> encodeInstance(const MaterialInstance& made) {
    std::vector<std::byte> bytes;
    for (const char kLetter : {'R', 'F', 'M', 'I'}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 1);
    for (const std::uint64_t kWord : {made.base.high, made.base.low}) {
        putWord(bytes, static_cast<std::uint32_t>(kWord));
        putWord(bytes, static_cast<std::uint32_t>(kWord >> 32U));
    }
    for (const float kNumber : made.parameters) {
        putWord(bytes, std::bit_cast<std::uint32_t>(kNumber));
    }
    return bytes;
}

result::Result<MaterialInstance> decodeInstance(std::span<const std::byte> bytes) {
    constexpr std::size_t kSize = 4 + 4 + 16 + (4 * kParameterFloats);
    if (bytes.size() != kSize || bytes[0] != std::byte{'R'} || bytes[1] != std::byte{'F'} ||
        bytes[2] != std::byte{'M'} || bytes[3] != std::byte{'I'}) {
        return invalid("a material instance is RFMI, its base and its parameters");
    }
    std::size_t at = 4;
    const auto kWord = [&bytes, &at]() {
        std::uint32_t word = 0;
        for (std::size_t each = 0; each < 4; ++each) {
            word |= std::to_integer<std::uint32_t>(bytes[at + each]) << (each * 8);
        }
        at += 4;
        return word;
    };
    if (kWord() != 1) {
        return invalid("a material instance is of format 1");
    }
    MaterialInstance made;
    for (std::uint64_t* word : {&made.base.high, &made.base.low}) {
        *word = kWord();
        *word |= static_cast<std::uint64_t>(kWord()) << 32U;
    }
    if (made.base == base::Bits128{}) {
        return invalid("a material instance names its base");
    }
    for (float& number : made.parameters) {
        number = std::bit_cast<float>(kWord());
        if (!std::isfinite(number)) {
            return invalid("a material instance's parameters are finite");
        }
    }
    return made;
}

} // namespace rawframe::material
