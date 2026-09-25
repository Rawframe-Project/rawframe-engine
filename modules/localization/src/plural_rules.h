#pragma once

// The compiled plural rules tools/generate_cldr.py makes (generated/).

#include "rawframe/localization/plural.h"

#include <span>
#include <string_view>

namespace rawframe::localization::cldr {

using PluralRule = PluralCategory (*)(const PluralOperands&) noexcept;

struct RuleSet {
    std::string_view locale;
    PluralRule rule = nullptr;
};

/// Each locale CLDR gives rules for, in tag order.
[[nodiscard]] std::span<const RuleSet> cardinalRules() noexcept;
[[nodiscard]] std::span<const RuleSet> ordinalRules() noexcept;

} // namespace rawframe::localization::cldr
