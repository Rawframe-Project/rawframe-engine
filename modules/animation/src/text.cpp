#include "text.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace rawframe::animation {

using document::Value;

bool hasMembers(const Value& value, std::initializer_list<std::string_view> names) {
    if (value.kind() != Value::Kind::Object || value.names().size() != names.size()) {
        return false;
    }
    return std::ranges::all_of(names, [&value](std::string_view name) {
        return value.find(name) != nullptr;
    });
}

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

std::optional<base::Bits128> bits128Of(const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::String) {
        return std::nullopt;
    }
    const base::Bits128Parse kParsed = base::parseBits128Hex(*value->text());
    if (!kParsed.parsed) {
        return std::nullopt;
    }
    return kParsed.value;
}

std::string hexOf(std::uint64_t value) {
    std::array<char, 16> digits{};
    for (std::size_t at = 0; at < digits.size(); ++at) {
        digits[at] = "0123456789abcdef"[(value >> (4U * (15U - at))) & 0xFU];
    }
    return std::string{digits.data(), digits.size()};
}

std::optional<std::uint64_t> bits64Of(const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::String) {
        return std::nullopt;
    }
    const std::string& text = *value->text();
    std::uint64_t made = 0;
    if (text.size() != 16 || !std::ranges::all_of(text, [](char each) {
            return (each >= '0' && each <= '9') || (each >= 'a' && each <= 'f');
        })) {
        return std::nullopt;
    }
    std::from_chars(text.data(), text.data() + text.size(), made, 16);
    return made;
}

bool machineName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

std::optional<double> numberOf(const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::Number) {
        return std::nullopt;
    }
    const std::optional<double> kNumber = value->real();
    if (!kNumber.has_value() || !std::isfinite(*kNumber)) {
        return std::nullopt;
    }
    return kNumber;
}

Value arrayOf(const std::array<double, 4>& numbers, std::size_t width) {
    Value made = Value::array();
    for (std::size_t at = 0; at < width; ++at) {
        made.push(Value::real(numbers[at]));
    }
    return made;
}

std::optional<std::array<double, 4>> numbersOf(const Value* value, std::size_t width) {
    if (value == nullptr || value->kind() != Value::Kind::Array || value->items().size() != width) {
        return std::nullopt;
    }
    std::array<double, 4> made{};
    for (std::size_t at = 0; at < width; ++at) {
        const std::optional<double> kNumber = numberOf(&value->items()[at]);
        if (!kNumber.has_value()) {
            return std::nullopt;
        }
        made[at] = *kNumber;
    }
    return made;
}

bool finite(std::span<const double> numbers) noexcept {
    return std::ranges::all_of(numbers, [](double each) {
        return std::isfinite(each);
    });
}

bool unit(const std::array<double, 4>& rotation) noexcept {
    const double kLength = (rotation[0] * rotation[0]) + (rotation[1] * rotation[1]) + (rotation[2] * rotation[2]) +
                           (rotation[3] * rotation[3]);
    return finite(rotation) && std::abs(kLength - 1.0) <= 2e-6;
}

} // namespace rawframe::animation
