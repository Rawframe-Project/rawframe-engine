// Pseudo-localization (SPEC-0033): only a message's text is changed, found
// by the message reader itself.

#include "rawframe/localization/pseudo.h"

#include "message_parts.h"
#include "rawframe/localization/errors.h"

#include <array>
#include <cmath>

namespace rawframe::localization {

namespace {

constexpr std::array<std::string_view, 26> kUpper{"Á", "Ɓ", "Ç", "Ď", "É", "Ƒ", "Ĝ", "Ĥ", "Í", "Ĵ", "Ķ", "Ĺ", "Ṁ",
                                                  "Ñ", "Ó", "Ṗ", "Ǫ", "Ŕ", "Š", "Ť", "Ú", "Ṽ", "Ŵ", "Ẋ", "Ý", "Ž"};
constexpr std::array<std::string_view, 26> kLower{"á", "ƀ", "ç", "ď", "é", "ƒ", "ĝ", "ĥ", "í", "ĵ", "ķ", "ĺ", "ṁ",
                                                  "ñ", "ó", "ṗ", "ʠ", "ŕ", "š", "ť", "ú", "ṽ", "ŵ", "ẋ", "ý", "ž"};

/// Right-to-left override, and the pop that ends it.
constexpr std::string_view kMirrorBegin = "\xE2\x80\xAE";
constexpr std::string_view kMirrorEnd = "\xE2\x80\xAC";

/// A run of text as the options change it, adding how many characters it
/// holds to `characters`. Escapes stay as written and count as one.
void transform(std::string_view run, bool accents, std::string& made, std::size_t& characters) {
    for (std::size_t at = 0; at < run.size(); ++at) {
        const char kEach = run[at];
        if (kEach == '\\') {
            made += run.substr(at, 2);
            ++at;
            ++characters;
            continue;
        }
        const auto kByte = static_cast<unsigned char>(kEach);
        if ((kByte & 0xC0U) != 0x80U) {
            ++characters;
        }
        if (accents && kEach >= 'A' && kEach <= 'Z') {
            made += kUpper[static_cast<std::size_t>(kEach - 'A')];
        } else if (accents && kEach >= 'a' && kEach <= 'z') {
            made += kLower[static_cast<std::size_t>(kEach - 'a')];
        } else {
            made += kEach;
        }
    }
}

} // namespace

double expansionFor(std::size_t characters) noexcept {
    // RESEARCH-0058's IBM table, each band's lower bound; the table skips
    // 51 to 70, which takes the band past it (D145).
    if (characters <= 10) {
        return 2.0;
    }
    if (characters <= 20) {
        return 1.8;
    }
    if (characters <= 30) {
        return 1.6;
    }
    if (characters <= 50) {
        return 1.4;
    }
    return 1.3;
}

result::Result<std::string>
pseudoLocalize(std::string_view message, const PseudoOptions& options, const MessageLimits& limits) {
    if (options.expansion.has_value() && !(*options.expansion >= 1.0 && *options.expansion <= kMostExpansion)) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kLocalizationDomain,
                            code(LocalizationError::OverLimit),
                            "a pseudo-localization's expansion is from 1 to its limit");
    }
    std::vector<PatternSpan> spans;
    RAWFRAME_TRY(parseMessage(message, limits, &spans));
    std::string made;
    std::size_t at = 0;
    for (const PatternSpan& span : spans) {
        made += message.substr(at, span.begin - at);
        if (options.markers) {
            made += '[';
        }
        std::size_t characters = 0;
        std::size_t cursor = span.begin;
        for (const TextRun& run : span.runs) {
            made += message.substr(cursor, run.begin - cursor);
            if (options.mirror) {
                made += kMirrorBegin;
            }
            transform(message.substr(run.begin, run.end - run.begin), options.accents, made, characters);
            if (options.mirror) {
                made += kMirrorEnd;
            }
            cursor = run.end;
        }
        made += message.substr(cursor, span.end - cursor);
        if (options.padding) {
            const double kRatio = options.expansion.value_or(expansionFor(characters));
            const auto kGoal = static_cast<std::size_t>(std::ceil(static_cast<double>(characters) * kRatio));
            made.append(kGoal - characters, '~');
        }
        if (options.markers) {
            made += ']';
        }
        at = span.end;
    }
    made += message.substr(at);
    // What comes out is a message within the same limits.
    RAWFRAME_TRY(parseMessage(made, limits));
    return made;
}

result::Result<Translations> pseudoTranslations(base::Bits128 table,
                                                const StringTable& source,
                                                const Locale& locale,
                                                const PseudoOptions& options,
                                                const MessageLimits& limits) {
    Translations made{.table = table, .locale = locale, .entries = {}};
    for (const auto& [key, entry] : source.entries) {
        auto message = pseudoLocalize(entry.message, options, limits);
        if (!message.has_value()) {
            return std::unexpected<result::Error>{std::move(message).error().withContext("key", key)};
        }
        made.entries.emplace(
            key, TranslatedEntry{.message = std::move(*message), .sourceHash = sourceHashOf(entry.message)});
    }
    return made;
}

} // namespace rawframe::localization
