#pragma once

// Pseudo-localization (ADR-0050, SPEC-0033): a message's text made to look
// translated, so untranslated strings, clipped layouts, and mirrored text
// show up before any translator is asked. Development only: a shipping
// build refuses to compile anything that includes this.
//
// The message is read as a message and only its text changes: placeholders,
// declarations, selectors, variant keys, literals, and escapes stay byte for
// byte. Each pattern's text is, as its options ask,
//
//   - accented: each ASCII letter an accented one (`Play` is `Ṗĺáý`);
//   - padded: `~` added until the text is as long as a translation would
//     be, by IBM's expansion ratios for its length (D145), or by a
//     declared ratio;
//   - marked: `[` before and `]` after, so a clipped end shows;
//   - mirrored: each run of text between U+202E and U+202C, so right to
//     left layout shows.

#if RAWFRAME_SHIPPING
#error "pseudo-localization is development only (SPEC-0033)"
#endif

#include "rawframe/localization/message.h"
#include "rawframe/localization/table.h"
#include "rawframe/result/result.h"

#include <optional>
#include <string>
#include <string_view>

namespace rawframe::localization {

/// SPEC-0033's named limit on expansion: a declared ratio is at least 1
/// and at most this.
inline constexpr double kMostExpansion = 3.0;

struct PseudoOptions {
    bool accents = true;
    bool padding = true;
    /// The ratio text is padded to; none for IBM's by length.
    std::optional<double> expansion;
    bool markers = true;
    bool mirror = false;
};

/// IBM's expansion ratio for text of that many characters: 2 up to 10,
/// 1.8 to 20, 1.6 to 30, 1.4 to 50, and 1.3 past.
[[nodiscard]] double expansionFor(std::size_t characters) noexcept;

/// The message pseudo-localized. Refuses a message as `parseMessage` does,
/// and (`OverLimit`) a declared ratio out of its bounds.
[[nodiscard]] result::Result<std::string>
pseudoLocalize(std::string_view message, const PseudoOptions& options = {}, const MessageLimits& limits = {});

/// A translation of every entry of a table into `locale`, pseudo-localized
/// and hashed against its source, so a catalog serves it as any other.
[[nodiscard]] result::Result<Translations> pseudoTranslations(base::Bits128 table,
                                                              const StringTable& source,
                                                              const Locale& locale,
                                                              const PseudoOptions& options = {},
                                                              const MessageLimits& limits = {});

} // namespace rawframe::localization
