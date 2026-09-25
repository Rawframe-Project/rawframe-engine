#pragma once

// What the catalog needs of the documents' own checks.

#include "rawframe/localization/table.h"
#include "rawframe/result/result.h"

namespace rawframe::localization {

/// A canonical tag whose region is ISO 3166's private use `XA` to `XZ`:
/// a pseudo-locale's (pseudo.h), which no authored document may name.
[[nodiscard]] bool privateUseLocale(const Locale& locale);

/// A translation document's own checks; with `pseudo`, its locale is a
/// private-use one instead of one intake knows.
[[nodiscard]] result::Status
translationsInForm(const Translations& translations, const TableLimits& limits, bool pseudo);

} // namespace rawframe::localization
