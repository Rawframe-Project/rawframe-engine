#pragma once

// CLDR's examples of each plural category, made by tools/generate_cldr.py
// into generated/plural_samples.cpp: the compiled rules' conformance oracle.

#include "rawframe/localization/plural.h"

#include <span>
#include <string_view>

namespace rawframe::localization::oracle {

struct PluralSample {
    std::string_view locale;
    bool ordinal = false;
    std::string_view number;
    PluralCategory category = PluralCategory::Other;
};

[[nodiscard]] std::span<const PluralSample> pluralSamples() noexcept;

} // namespace rawframe::localization::oracle
