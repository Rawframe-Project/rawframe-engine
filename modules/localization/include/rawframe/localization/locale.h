#pragma once

// Locales (ADR-0050, SPEC-0033): a BCP 47 subset, `language[-Script][-REGION]`,
// a language of two or three lowercase letters, a script of four in title
// case, and a region of two uppercase letters or three digits (`es-419`).
// Variants, extensions, and private use are refused. Documents write tags
// in that canonical form; a tag from a player or the platform comes in by
// `intake`, which makes the case canonical and replaces CLDR's aliases.
//
// A message is looked up locale by locale along a fallback chain:
//
//   1. the requested tag, maximized by CLDR's likely subtags (`zh-TW` is
//      `zh-Hant-TW`);
//   2. then its parents: CLDR's parent override where one exists (`pt-MO`
//      to `pt-PT`, `en-AU` to `en-001`, `zh-Hant` to the root), else the
//      tag less its last subtag, ending before the root;
//   3. then the project's default locale, then the table's source locale.
//
// Documents name locales as authors write them, `pt-BR` more often than
// `pt-Latn-BR`, so each maximal step also offers its shorter forms that
// maximize to it (D142): `pt-Latn-BR` offers `pt-BR`, and `pt-Latn` offers
// `pt`, since `pt` is written in Latin, while `sr-Latn` never offers `sr`,
// which is Cyrillic. The chain holds each tag once, first where it came.
//
// Nothing here reads the platform's locale data: the tables are CLDR's,
// pinned and compiled ahead of time.

#include "rawframe/result/result.h"

#include <compare>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::localization {

struct Locale {
    std::string language;
    /// Empty for none.
    std::string script;
    std::string region;

    /// The tag, its subtags joined by `-`.
    [[nodiscard]] std::string text() const;

    friend auto operator<=>(const Locale&, const Locale&) = default;
};

/// SPEC-0033's named limit for fallback chains.
struct ChainLimits {
    std::size_t maximumLocales = 16;
};

/// A tag as a document writes it: SPEC-0033's grammar in canonical case.
/// Refuses (`LocaleInvalid`) anything else, aliases included as written.
[[nodiscard]] result::Result<Locale> parseLocale(std::string_view tag);

/// A tag as a player or the platform gives it: `_` read as `-`, case made
/// canonical, and CLDR's language, script, and region aliases replaced
/// (`iw` is `he`, `sh` is `sr-Latn`). Refuses (`LocaleInvalid`) a tag out
/// of the grammar and (`LocaleUnknown`) one naming a subtag CLDR does not
/// know; the caller falls back to its default locale.
[[nodiscard]] result::Result<Locale> intake(std::string_view tag);

/// The tag with CLDR's likely script and region filled in; as it was when
/// CLDR has nothing for it.
[[nodiscard]] Locale maximize(const Locale& locale);

/// The locales to look a key up in, in order, as above. Refuses
/// (`OverLimit`) a chain longer than the limit.
[[nodiscard]] result::Result<std::vector<Locale>> fallbackChain(const Locale& requested,
                                                                const Locale& projectDefault,
                                                                const Locale& source,
                                                                const ChainLimits& limits = {});

} // namespace rawframe::localization
