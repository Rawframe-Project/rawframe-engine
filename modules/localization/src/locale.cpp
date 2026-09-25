#include "rawframe/localization/locale.h"

#include "cldr.h"
#include "rawframe/localization/errors.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace rawframe::localization {

namespace {

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::LocaleInvalid), why);
}

bool lower(char each) {
    return each >= 'a' && each <= 'z';
}

bool upper(char each) {
    return each >= 'A' && each <= 'Z';
}

bool digit(char each) {
    return each >= '0' && each <= '9';
}

/// The subtags of a tag in SPEC-0033's grammar and canonical case; none
/// for any other text.
std::optional<Locale> split(std::string_view tag) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0; start <= tag.size();) {
        const std::size_t kEnd = std::min(tag.find('-', start), tag.size());
        parts.push_back(tag.substr(start, kEnd - start));
        start = kEnd + 1;
    }
    if (parts.empty() || parts.size() > 3) {
        return std::nullopt;
    }
    Locale made;
    const std::string_view kLanguage = parts[0];
    if (kLanguage.size() < 2 || kLanguage.size() > 3 || !std::ranges::all_of(kLanguage, lower)) {
        return std::nullopt;
    }
    made.language = kLanguage;
    std::size_t at = 1;
    if (at < parts.size() && parts[at].size() == 4 && upper(parts[at][0]) &&
        std::ranges::all_of(parts[at].substr(1), lower)) {
        made.script = parts[at++];
    }
    if (at < parts.size() && ((parts[at].size() == 2 && std::ranges::all_of(parts[at], upper)) ||
                              (parts[at].size() == 3 && std::ranges::all_of(parts[at], digit)))) {
        made.region = parts[at++];
    }
    if (at != parts.size()) {
        return std::nullopt;
    }
    return made;
}

bool known(std::span<const std::string_view> table, std::string_view subtag) {
    return std::ranges::binary_search(table, subtag);
}

/// ASCII case alone: no platform locale decides a tag's case.
char upperOf(char each) {
    return lower(each) ? static_cast<char>(each - 'a' + 'A') : each;
}

char lowerOf(char each) {
    return upper(each) ? static_cast<char>(each - 'A' + 'a') : each;
}

} // namespace

std::string Locale::text() const {
    std::string made = language;
    for (const std::string& part : {script, region}) {
        if (!part.empty()) {
            made += '-';
            made += part;
        }
    }
    return made;
}

result::Result<Locale> parseLocale(std::string_view tag) {
    std::optional<Locale> made = split(tag);
    if (!made.has_value()) {
        return invalid("a locale is language[-Script][-REGION] in canonical case");
    }
    return std::move(*made);
}

result::Result<Locale> intake(std::string_view tag) {
    // Case made canonical subtag by subtag: a language lower, a script
    // title, a region upper.
    std::string canonical;
    std::size_t subtag = 0;
    std::size_t length = 0;
    const auto kFinish = [&canonical, &subtag, &length] {
        if (subtag > 0 && length == 4) {
            // A script: its first letter up, the rest down.
            const std::size_t kStart = canonical.size() - 4;
            canonical[kStart] = upperOf(canonical[kStart]);
        } else if (subtag > 0 && length == 2) {
            for (std::size_t at = canonical.size() - 2; at < canonical.size(); ++at) {
                canonical[at] = upperOf(canonical[at]);
            }
        }
    };
    for (const char kEach : tag) {
        if (kEach == '-' || kEach == '_') {
            kFinish();
            canonical += '-';
            ++subtag;
            length = 0;
            continue;
        }
        canonical += lowerOf(kEach);
        ++length;
    }
    kFinish();
    std::optional<Locale> made = split(canonical);
    if (!made.has_value()) {
        return invalid("a locale is language[-Script][-REGION]");
    }
    if (const std::string_view* replacement = cldr::find(cldr::languageAliases(), made->language)) {
        const std::optional<Locale> kWith = split(*replacement);
        if (kWith.has_value()) {
            made->language = kWith->language;
            made->script = made->script.empty() ? kWith->script : made->script;
            made->region = made->region.empty() ? kWith->region : made->region;
        }
    }
    if (const std::string_view* replacement = cldr::find(cldr::scriptAliases(), made->script)) {
        made->script = *replacement;
    }
    if (const std::string_view* replacement = cldr::find(cldr::regionAliases(), made->region)) {
        made->region = *replacement;
    }
    if (!known(cldr::languages(), made->language) || (!made->script.empty() && !known(cldr::scripts(), made->script)) ||
        (!made->region.empty() && !known(cldr::regions(), made->region))) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kLocalizationDomain,
                            code(LocalizationError::LocaleUnknown),
                            "a locale names a subtag CLDR does not know");
    }
    return std::move(*made);
}

Locale maximize(const Locale& locale) {
    // CLDR's lookup order: language, script, and region; language and
    // region; language and script; language.
    const std::string& language = locale.language;
    std::vector<std::string> tries;
    if (!locale.script.empty() && !locale.region.empty()) {
        tries.push_back(language + '-' + locale.script + '-' + locale.region);
    }
    if (!locale.region.empty()) {
        tries.push_back(language + '-' + locale.region);
    }
    if (!locale.script.empty()) {
        tries.push_back(language + '-' + locale.script);
    }
    tries.push_back(language);
    for (const std::string& each : tries) {
        const std::string_view* found = cldr::find(cldr::likelySubtags(), each);
        const std::optional<Locale> kFull = found != nullptr ? split(*found) : std::nullopt;
        if (kFull.has_value()) {
            return Locale{.language = language == "und" ? kFull->language : language,
                          .script = locale.script.empty() ? kFull->script : locale.script,
                          .region = locale.region.empty() ? kFull->region : locale.region};
        }
    }
    return locale;
}

result::Result<std::vector<Locale>>
fallbackChain(const Locale& requested, const Locale& projectDefault, const Locale& source, const ChainLimits& limits) {
    std::vector<Locale> chain;
    const auto kAdd = [&chain](const Locale& locale) {
        if (!std::ranges::contains(chain, locale)) {
            chain.push_back(locale);
        }
    };
    // Its forms: the tag, and each shorter tag that maximizes to it.
    const auto kForms = [](const Locale& locale) {
        std::vector<Locale> made{locale};
        if (!locale.script.empty() && !locale.region.empty()) {
            const Locale kShort{.language = locale.language, .script = {}, .region = locale.region};
            if (maximize(kShort).script == locale.script) {
                made.push_back(kShort);
            }
        } else if (!locale.script.empty()) {
            const Locale kShort{.language = locale.language, .script = {}, .region = {}};
            if (maximize(kShort).script == locale.script) {
                made.push_back(kShort);
            }
        }
        return made;
    };
    std::optional<Locale> current = maximize(requested);
    // A step at most for each subtag and each override, each only once:
    // the walk ends however CLDR's data were made.
    for (std::size_t step = 0; current.has_value() && step < limits.maximumLocales * 2; ++step) {
        const std::vector<Locale> kHere = kForms(*current);
        for (const Locale& each : kHere) {
            kAdd(each);
        }
        std::optional<std::string_view> parent;
        for (const Locale& each : kHere) {
            if (const std::string_view* found = cldr::find(cldr::parentLocales(), each.text())) {
                parent = *found;
                break;
            }
        }
        if (parent.has_value()) {
            const std::optional<Locale> kParent = *parent == "und" ? std::nullopt : split(*parent);
            current = kParent.has_value() ? std::optional{maximize(*kParent)} : std::nullopt;
            continue;
        }
        if (!current->region.empty()) {
            current->region.clear();
        } else if (!current->script.empty()) {
            current->script.clear();
        } else {
            current.reset();
        }
    }
    kAdd(projectDefault);
    kAdd(source);
    if (chain.size() > limits.maximumLocales) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kLocalizationDomain,
                            code(LocalizationError::OverLimit),
                            "a fallback chain is longer than its limit");
    }
    return chain;
}

} // namespace rawframe::localization
