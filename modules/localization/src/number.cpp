#include "rawframe/localization/number.h"

#include "cldr.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <vector>

namespace rawframe::localization {

namespace {

/// CLDR's root: what a locale without number data of its own writes.
constexpr cldr::NumberSymbols kRoot{"und", ".", ",", "-", "0123456789", 3, 3, 1};

const cldr::NumberSymbols& symbolsOf(const Locale& locale) {
    const std::span<const cldr::NumberSymbols> kTable = cldr::numberSymbols();
    const auto kFind = [kTable](std::string_view key) -> const cldr::NumberSymbols* {
        const auto kFound = std::ranges::lower_bound(kTable, key, {}, &cldr::NumberSymbols::locale);
        return kFound != kTable.end() && kFound->locale == key ? &*kFound : nullptr;
    };
    const Locale kScripted{.language = locale.language, .script = locale.script, .region = {}};
    for (const std::string& key : {locale.text(), kScripted.text(), locale.language}) {
        if (const cldr::NumberSymbols* found = kFind(key)) {
            return *found;
        }
    }
    return kRoot;
}

/// The ten digits, each a UTF-8 sequence.
std::array<std::string_view, 10> digitsOf(std::string_view digits) {
    std::array<std::string_view, 10> made{};
    std::size_t at = 0;
    for (std::string_view& digit : made) {
        const auto kLead = static_cast<unsigned char>(digits[at]);
        const std::size_t kLength = kLead < 0x80U ? 1 : kLead < 0xE0U ? 2 : kLead < 0xF0U ? 3 : 4;
        digit = digits.substr(at, kLength);
        at += kLength;
    }
    return made;
}

} // namespace

std::string plainInteger(std::int64_t value) {
    std::array<char, 24> text{};
    const auto kMade = std::to_chars(text.data(), text.data() + text.size(), value);
    return std::string{text.data(), kMade.ptr};
}

std::optional<std::string> plainDecimal(double value, std::uint32_t minimumFraction, std::uint32_t maximumFraction) {
    if (!std::isfinite(value) || minimumFraction > maximumFraction || maximumFraction > kMostFractionDigits) {
        return std::nullopt;
    }
    // The largest double is 309 digits before its point.
    std::array<char, 400> text{};
    const auto kMade = std::to_chars(
        text.data(), text.data() + text.size(), value, std::chars_format::fixed, static_cast<int>(maximumFraction));
    std::string made{text.data(), kMade.ptr};
    const std::size_t kPoint = made.find('.');
    if (kPoint != std::string::npos) {
        std::size_t end = made.size();
        while (end > kPoint + 1 + minimumFraction && made[end - 1] == '0') {
            --end;
        }
        made.resize(end == kPoint + 1 ? kPoint : end);
    }
    if (made.front() == '-' && std::ranges::all_of(made.substr(1), [](char each) {
            return each == '0' || each == '.';
        })) {
        made.erase(0, 1);
    }
    return made;
}

std::string localized(const Locale& locale, std::string_view plain, bool grouping) {
    const cldr::NumberSymbols& symbols = symbolsOf(locale);
    const std::array<std::string_view, 10> kDigits = digitsOf(symbols.digits);
    std::string made;
    if (!plain.empty() && plain.front() == '-') {
        made += symbols.minus;
        plain.remove_prefix(1);
    }
    const std::size_t kPoint = std::min(plain.find('.'), plain.size());
    const std::string_view kWhole = plain.substr(0, kPoint);
    const auto kDigit = [&made, &kDigits](char each) {
        if (each >= '0' && each <= '9') {
            made += kDigits[static_cast<std::size_t>(each - '0')];
        }
    };
    // Where a group separator follows a digit, counted from the right.
    const bool kGrouped = grouping && symbols.primary > 0 && kWhole.size() >= symbols.primary + symbols.minimumGrouping;
    for (std::size_t at = 0; at < kWhole.size(); ++at) {
        kDigit(kWhole[at]);
        const std::size_t kLeft = kWhole.size() - at - 1;
        if (kGrouped && kLeft > 0 &&
            (kLeft == symbols.primary ||
             (kLeft > symbols.primary && (kLeft - symbols.primary) % symbols.secondary == 0))) {
            made += symbols.group;
        }
    }
    if (kPoint < plain.size()) {
        made += symbols.decimal;
        for (const char kEach : plain.substr(kPoint + 1)) {
            kDigit(kEach);
        }
    }
    return made;
}

} // namespace rawframe::localization
