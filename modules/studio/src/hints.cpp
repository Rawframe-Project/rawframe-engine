#include "hints.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>

namespace rawframe::studio {

namespace {

using document::Value;

std::optional<std::int64_t> wholeOf(std::string_view text) {
    std::int64_t value = 0;
    const auto kRead = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || kRead.ec != std::errc{} || kRead.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> realOf(std::string_view text) {
    double value = 0;
    const auto kRead = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || kRead.ec != std::errc{} || kRead.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// A number as a hint shows it: whole without a fraction.
std::string numberText(double value) {
    std::array<char, 32> written{};
    const auto kEnd = std::to_chars(written.data(), written.data() + written.size(), value);
    return {written.data(), kEnd.ptr};
}

} // namespace

void readHints(const Value& field, Catalog::Field& into) {
    if (const Value* cases = field.find("cases"); cases != nullptr && cases->kind() == Value::Kind::Array) {
        for (const Value& each : cases->items()) {
            if (each.text() != nullptr) {
                into.cases.push_back(*each.text());
            }
        }
    }
    if (const Value* least = field.find("least"); least != nullptr) {
        into.least = least->real();
    }
    if (const Value* most = field.find("most"); most != nullptr) {
        into.most = most->real();
    }
    if (const Value* unit = field.find("unit"); unit != nullptr && unit->kind() == Value::Kind::String) {
        into.unit = *unit->text();
    }
    if (const Value* asset = field.find("asset"); asset != nullptr && asset->kind() == Value::Kind::String) {
        into.asset = *asset->text();
    }
    if (const Value* color = field.find("color"); color != nullptr) {
        into.color = color->truth().value_or(false);
    }
    if (const Value* labels = field.find("labels"); labels != nullptr && labels->kind() == Value::Kind::Array) {
        for (const Value& each : labels->items()) {
            const Value* value = each.find("value");
            const Value* name = each.find("name");
            if (value != nullptr && value->integer().has_value() && name != nullptr && name->text() != nullptr) {
                into.labels.emplace_back(*value->integer(), *name->text());
            }
        }
    }
}

std::string shownAs(const Catalog::Field& type, std::string_view text) {
    if (const auto kWhole = wholeOf(text); kWhole.has_value()) {
        const auto kLabel = std::ranges::find(type.labels, *kWhole, &std::pair<std::int64_t, std::string>::first);
        if (kLabel != type.labels.end()) {
            return kLabel->second;
        }
        if (type.color && *kWhole >= 0 && *kWhole <= 0xFFFFFFFFLL) {
            std::array<char, 16> written{};
            std::snprintf(written.data(), written.size(), "#%08llX", static_cast<unsigned long long>(*kWhole));
            return written.data();
        }
    }
    return std::string{text};
}

std::string typedAs(const Catalog::Field& type, std::string_view typed) {
    const auto kLabel = std::ranges::find(type.labels, typed, &std::pair<std::int64_t, std::string>::second);
    if (kLabel != type.labels.end()) {
        return std::to_string(kLabel->first);
    }
    if (!type.asset.empty() && typed == "none") {
        return "0";
    }
    if (type.color) {
        if (const std::optional<std::uint32_t> kColor = colorOf(typed); kColor.has_value()) {
            return std::to_string(*kColor);
        }
    }
    return std::string{typed};
}

std::string hintText(const Catalog::Field& type) {
    std::string said = type.unit;
    if (type.least.has_value() && type.most.has_value()) {
        said += (said.empty() ? "" : ", ") + numberText(*type.least) + " to " + numberText(*type.most);
    } else if (type.least.has_value()) {
        said += (said.empty() ? "" : ", ") + std::string{"from "} + numberText(*type.least);
    } else if (type.most.has_value()) {
        said += (said.empty() ? "" : ", ") + std::string{"to "} + numberText(*type.most);
    }
    return said;
}

bool pastRange(const Catalog::Field& type, std::string_view text) {
    const std::optional<double> kValue = realOf(text);
    return kValue.has_value() &&
           ((type.least.has_value() && *kValue < *type.least) || (type.most.has_value() && *kValue > *type.most));
}

std::vector<std::string> choicesOf(const Catalog::Field& type) {
    if (!type.cases.empty()) {
        return type.cases;
    }
    if (!type.labels.empty()) {
        std::vector<std::string> names;
        for (const auto& [kValue, kName] : type.labels) {
            names.push_back(kName);
        }
        return names;
    }
    if (type.kind == "truth") {
        return {"true", "false"};
    }
    return {};
}

std::vector<std::string> assetChoices(const Catalog::Field& type, std::span<const Asset> assets) {
    if (type.asset.empty()) {
        return {};
    }
    std::vector<std::string> names{"none"};
    for (const Asset& each : assets) {
        if (each.kind == type.asset) {
            names.push_back(each.name);
        }
    }
    return names;
}

std::optional<std::uint32_t> colorOf(std::string_view text) {
    if (!text.starts_with('#') || (text.size() != 7 && text.size() != 9)) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto kRead = std::from_chars(text.data() + 1, text.data() + text.size(), value, 16);
    if (kRead.ec != std::errc{} || kRead.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    // Six digits are opaque.
    return text.size() == 7 ? (value << 8U) | 0xFFU : value;
}

document::Value documentedValue(const std::string& text) {
    if (text.empty()) {
        return document::Value{};
    }
    auto read = document::parse(text);
    return read.has_value() ? std::move(*read) : document::Value::string(text);
}

} // namespace rawframe::studio
