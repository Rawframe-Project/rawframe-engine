#include "rawframe/material/instance.h"

#include "common.h"
#include "fold.h"
#include "surface_graph.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
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
    if (kOverrides.kind() != Value::Kind::Object || kOverrides.names().empty() || kOverrides.names().size() > 16) {
        return invalid("a material instance overrides one to sixteen of its base's inputs and switches");
    }
    return InstanceDocument{.base = *kPath, .overrides = kOverrides};
}

result::Result<InstanceValues> instanceValues(const graph::Document& base, const Value& overrides) {
    RAWFRAME_TRY(validateSurface(base));
    RAWFRAME_TRY_ASSIGN(const std::vector<surface_graph::InterfaceInput> kInputs,
                        surface_graph::interfaceOf(surface_graph::Graph{base}));
    std::array<double, kParameterFloats> values{};
    for (const surface_graph::InterfaceInput& kInput : kInputs) {
        std::ranges::copy(kInput.initial, values.begin() + static_cast<std::ptrdiff_t>(kInput.slot * 4));
    }
    // The texture each input gives where an override changes it.
    std::array<std::uint64_t, 8> textures{};
    RAWFRAME_TRY_ASSIGN(const std::vector<surface_graph::StaticSwitch> kSwitches,
                        surface_graph::switchesOf(surface_graph::Graph{base}));
    for (std::size_t at = 0; at < overrides.names().size(); ++at) {
        // A switch's override is its variant's (`switchedVariant`, D719).
        if (std::ranges::find(kSwitches, std::string_view{overrides.names()[at]}, &surface_graph::StaticSwitch::name) !=
            kSwitches.end()) {
            continue;
        }
        const auto kFound =
            std::ranges::find(kInputs, std::string_view{overrides.names()[at]}, &surface_graph::InterfaceInput::name);
        if (kFound == kInputs.end()) {
            return invalid("a material instance overrides its base's interface inputs and switches alone");
        }
        const Value& kOverride = overrides.items()[at];
        if (kFound->carried == surface_graph::Carried::Texture2d) {
            const std::optional<std::uint64_t> kId =
                kOverride.text() != nullptr ? graph::nodeIdOf(*kOverride.text()) : std::nullopt;
            if (!kId.has_value() || *kId == 0) {
                return invalid("a material instance's texture override names the game's texture by 16 lowercase hex "
                               "digits");
            }
            if (*kId == kFound->texture) {
                return invalid("a material instance's override differs from its input's default, which it would "
                               "repeat");
            }
            textures.at(kFound->slot) = *kId;
            continue;
        }
        if (graph::connectionOf(kOverride).has_value()) {
            return invalid("a material instance's override is of its input's type");
        }
        RAWFRAME_TRY_ASSIGN(const auto kValue, surface_graph::interfaceValueOf(kFound->carried, kOverride));
        if (kValue == kFound->initial) {
            return invalid("a material instance's override differs from its input's default, which it would repeat");
        }
        std::ranges::copy(kValue, values.begin() + static_cast<std::ptrdiff_t>(kFound->slot * 4));
    }
    InstanceValues made;
    std::ranges::transform(values, made.parameters.begin(), [](double each) {
        return static_cast<float>(each);
    });
    // The program's texture slots each input's texture is bound at.
    if (std::ranges::any_of(textures, [](std::uint64_t each) {
            return each != 0;
        })) {
        RAWFRAME_TRY_ASSIGN(const GeneratedSlang kProgram, generateSlang(base));
        for (std::size_t slot = 0; slot < kProgram.textureInputs.size(); ++slot) {
            const std::uint8_t kInput = kProgram.textureInputs.at(slot);
            made.textures.at(slot) = kInput == 0 ? 0 : textures.at(kInput - 1U);
        }
    }
    return made;
}

result::Result<std::optional<graph::Document>> switchedVariant(const graph::Document& base, const Value& overrides) {
    RAWFRAME_TRY(validateSurface(base));
    RAWFRAME_TRY_ASSIGN(const std::vector<surface_graph::StaticSwitch> kSwitches,
                        surface_graph::switchesOf(surface_graph::Graph{base}));
    graph::Document made = base;
    bool switched = false;
    for (std::size_t at = 0; at < overrides.names().size(); ++at) {
        const auto kFound =
            std::ranges::find(kSwitches, std::string_view{overrides.names()[at]}, &surface_graph::StaticSwitch::name);
        if (kFound == kSwitches.end()) {
            continue;
        }
        const std::optional<bool> kOn = overrides.items()[at].truth();
        if (!kOn.has_value()) {
            return invalid("a material instance's switch override is a bool");
        }
        if (*kOn == kFound->on) {
            return invalid("a material instance's override differs from its input's default, which it would repeat");
        }
        // The switch's default the override, in the variant's own interface.
        Value interface = Value::object();
        for (std::size_t member = 0; member < made.interface.names().size(); ++member) {
            const std::string& kMember = made.interface.names()[member];
            if (kMember != "switches") {
                interface.add(kMember, made.interface.items()[member]);
                continue;
            }
            Value switches = Value::object();
            const Value& kOld = made.interface.items()[member];
            for (std::size_t each = 0; each < kOld.names().size(); ++each) {
                Value entry = Value::object();
                entry.add("default",
                          kOld.names()[each] == kFound->name ? Value::boolean(*kOn) : kOld.items()[each].items()[0]);
                switches.add(kOld.names()[each], std::move(entry));
            }
            interface.add("switches", std::move(switches));
        }
        made.interface = std::move(interface);
        switched = true;
    }
    if (!switched) {
        return std::optional<graph::Document>{};
    }
    return std::optional{std::move(made)};
}

std::vector<std::byte> encodeInstance(const MaterialInstance& made) {
    std::vector<std::byte> bytes;
    for (const char kLetter : {'R', 'F', 'M', 'I'}) {
        bytes.push_back(static_cast<std::byte>(kLetter));
    }
    putWord(bytes, 2);
    for (const std::uint64_t kWord : {made.base.high, made.base.low}) {
        putWord(bytes, static_cast<std::uint32_t>(kWord));
        putWord(bytes, static_cast<std::uint32_t>(kWord >> 32U));
    }
    for (const float kNumber : made.parameters) {
        putWord(bytes, std::bit_cast<std::uint32_t>(kNumber));
    }
    for (const std::uint64_t kTexture : made.textures) {
        putWord(bytes, static_cast<std::uint32_t>(kTexture));
        putWord(bytes, static_cast<std::uint32_t>(kTexture >> 32U));
    }
    return bytes;
}

result::Result<MaterialInstance> decodeInstance(std::span<const std::byte> bytes) {
    constexpr std::size_t kSize = 4 + 4 + 16 + (4 * kParameterFloats) + (8 * 4);
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
    if (kWord() != 2) {
        return invalid("a material instance is of format 2");
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
    for (std::uint64_t& texture : made.textures) {
        texture = kWord();
        texture |= static_cast<std::uint64_t>(kWord()) << 32U;
    }
    return made;
}

} // namespace rawframe::material
