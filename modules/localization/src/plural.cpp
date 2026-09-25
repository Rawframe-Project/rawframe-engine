#include "rawframe/localization/plural.h"

#include "plural_rules.h"

#include <algorithm>
#include <array>

namespace rawframe::localization {

namespace {

constexpr std::array<std::string_view, 6> kNames{"zero", "one", "two", "few", "many", "other"};

/// Digits at most: past this a u64 could not hold them.
constexpr std::size_t kMostDigits = 18;

/// The rules for a locale: its tag's, else its language's; none for a
/// language CLDR gives none, which is `other` alone.
cldr::PluralRule ruleFor(std::span<const cldr::RuleSet> rules, const Locale& locale) noexcept {
    const auto kFind = [rules](std::string_view key) -> cldr::PluralRule {
        const auto kFound = std::ranges::lower_bound(rules, key, {}, &cldr::RuleSet::locale);
        return kFound != rules.end() && kFound->locale == key ? kFound->rule : nullptr;
    };
    if (!locale.script.empty() || !locale.region.empty()) {
        if (const cldr::PluralRule kRule = kFind(locale.text())) {
            return kRule;
        }
    }
    return kFind(locale.language);
}

} // namespace

std::string_view nameOf(PluralCategory category) noexcept {
    return kNames[static_cast<std::size_t>(category)];
}

std::optional<PluralCategory> pluralCategory(std::string_view name) noexcept {
    for (std::size_t at = 0; at < kNames.size(); ++at) {
        if (kNames[at] == name) {
            return static_cast<PluralCategory>(at);
        }
    }
    return std::nullopt;
}

std::optional<PluralOperands> operandsOf(std::string_view number) noexcept {
    if (!number.empty() && number.front() == '-') {
        number.remove_prefix(1);
    }
    const std::size_t kPoint = number.find('.');
    const std::string_view kWhole = number.substr(0, kPoint);
    const std::string_view kFraction =
        kPoint == std::string_view::npos ? std::string_view{} : number.substr(kPoint + 1);
    const auto kDigits = [](std::string_view digits) {
        return std::ranges::all_of(digits, [](char each) {
            return each >= '0' && each <= '9';
        });
    };
    if (kWhole.empty() || kWhole.size() > kMostDigits || kFraction.size() > kMostDigits || !kDigits(kWhole) ||
        !kDigits(kFraction) || (kPoint != std::string_view::npos && kFraction.empty())) {
        return std::nullopt;
    }
    PluralOperands made;
    for (const char kDigit : kWhole) {
        made.i = (made.i * 10) + static_cast<std::uint64_t>(kDigit - '0');
    }
    made.v = static_cast<std::uint32_t>(kFraction.size());
    std::string_view trimmed = kFraction;
    while (!trimmed.empty() && trimmed.back() == '0') {
        trimmed.remove_suffix(1);
    }
    made.w = static_cast<std::uint32_t>(trimmed.size());
    for (const char kDigit : kFraction) {
        made.f = (made.f * 10) + static_cast<std::uint64_t>(kDigit - '0');
    }
    for (const char kDigit : trimmed) {
        made.t = (made.t * 10) + static_cast<std::uint64_t>(kDigit - '0');
    }
    return made;
}

PluralCategory cardinal(const Locale& locale, const PluralOperands& operands) noexcept {
    const cldr::PluralRule kRule = ruleFor(cldr::cardinalRules(), locale);
    return kRule != nullptr ? kRule(operands) : PluralCategory::Other;
}

PluralCategory ordinal(const Locale& locale, const PluralOperands& operands) noexcept {
    const cldr::PluralRule kRule = ruleFor(cldr::ordinalRules(), locale);
    return kRule != nullptr ? kRule(operands) : PluralCategory::Other;
}

} // namespace rawframe::localization
