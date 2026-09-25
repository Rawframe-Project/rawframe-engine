// Catalogs (SPEC-0033): tables and translations checked together, and keys
// resolved along the fallback chain.

#include "rawframe/localization/catalog.h"

#include "rawframe/localization/errors.h"
#include "table_parts.h"

#include <algorithm>
#include <array>
#include <set>

namespace rawframe::localization {

namespace {

std::string hexOf(base::Bits128 value) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(value, digits);
    return std::string{digits.data(), digits.size()};
}

result::Error failure(LocalizationError error, std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kLocalizationDomain, code(error), why).error();
}

/// A failure naming the table, and the locale and key where there are
/// ones.
std::unexpected<result::Error> refused(
    LocalizationError error, std::string_view why, base::Bits128 table, const Locale* locale, std::string_view key) {
    result::Error made = failure(error, why).withContext("table", hexOf(table));
    if (locale != nullptr) {
        made = std::move(made).withContext("locale", locale->text());
    }
    if (!key.empty()) {
        made = std::move(made).withContext("key", key);
    }
    return std::unexpected<result::Error>{std::move(made)};
}

} // namespace

result::Result<Catalog> Catalog::build(std::span<const TableDocument> tables,
                                       std::span<const Translations> translations,
                                       const CatalogLimits& limits) {
    return assemble(tables, translations, {}, limits);
}

#if !RAWFRAME_SHIPPING
result::Result<Catalog> Catalog::buildWithPseudo(std::span<const TableDocument> tables,
                                                 std::span<const Translations> translations,
                                                 std::span<const Translations> pseudo,
                                                 const CatalogLimits& limits) {
    return assemble(tables, translations, pseudo, limits);
}
#endif

result::Result<Catalog> Catalog::assemble(std::span<const TableDocument> tables,
                                          std::span<const Translations> translations,
                                          std::span<const Translations> pseudo,
                                          const CatalogLimits& limits) {
    if (tables.size() > limits.maximumTables) {
        return std::unexpected<result::Error>{
            failure(LocalizationError::OverLimit, "a catalog has more tables than its limit")};
    }
    Catalog made;
    made.limits_ = limits;
    std::set<Locale> locales;
    for (const TableDocument& document : tables) {
        if (made.tables_.contains(document.id) || document.id == base::Bits128{}) {
            return refused(LocalizationError::CatalogInvalid,
                           "a catalog has each table once, by its identity",
                           document.id,
                           nullptr,
                           {});
        }
        RAWFRAME_TRY(writeStrings(document.table, limits.table));
        Table& table = made.tables_[document.id];
        table.sourceLocale = document.table.sourceLocale;
        auto& messages = table.messages[table.sourceLocale];
        for (const auto& [key, entry] : document.table.entries) {
            RAWFRAME_TRY_ASSIGN(Message message, parseMessage(entry.message, limits.table.message));
            messages.emplace(key, std::move(message));
        }
        locales.insert(table.sourceLocale);
    }
    for (const Translations& translation : translations) {
        RAWFRAME_TRY(made.add(tables, translation, false, locales));
    }
    for (const Translations& translation : pseudo) {
        RAWFRAME_TRY(made.add(tables, translation, true, locales));
    }
    if (locales.size() > limits.maximumLocales) {
        return std::unexpected<result::Error>{
            failure(LocalizationError::OverLimit, "a catalog has more locales than its limit")};
    }
    std::ranges::sort(made.stale_);
    return made;
}

result::Status Catalog::add(std::span<const TableDocument> tables,
                            const Translations& translation,
                            bool pseudo,
                            std::set<Locale>& locales) {
    const auto kFound = tables_.find(translation.table);
    if (kFound == tables_.end()) {
        return refused(LocalizationError::CatalogInvalid,
                       "a translation's table is in the catalog",
                       translation.table,
                       &translation.locale,
                       {});
    }
    Table& table = kFound->second;
    if (translation.locale == table.sourceLocale || table.messages.contains(translation.locale)) {
        return refused(LocalizationError::CatalogInvalid,
                       "a table has one translation a locale, none into its source locale",
                       translation.table,
                       &translation.locale,
                       {});
    }
    RAWFRAME_TRY(translationsInForm(translation, limits_.table, pseudo));
    const auto& source = table.messages.at(table.sourceLocale);
    const auto kDocument = std::ranges::find(tables, translation.table, &TableDocument::id);
    std::map<std::string, Message, std::less<>> messages;
    for (const auto& [key, entry] : translation.entries) {
        const auto kSource = source.find(key);
        if (kSource == source.end()) {
            return refused(LocalizationError::Orphaned,
                           "a translated key is in its table",
                           translation.table,
                           &translation.locale,
                           key);
        }
        RAWFRAME_TRY_ASSIGN(Message message, parseMessage(entry.message, limits_.table.message));
        const std::vector<std::string> kAllowed = argumentsOf(kSource->second);
        for (const std::string& argument : argumentsOf(message)) {
            if (!std::ranges::binary_search(kAllowed, argument)) {
                return refused(LocalizationError::CatalogInvalid,
                               "a translated message reads only arguments its source reads",
                               translation.table,
                               &translation.locale,
                               key);
            }
        }
        if (entry.sourceHash != sourceHashOf(kDocument->table.entries.find(key)->second.message)) {
            stale_.push_back(StaleEntry{.table = translation.table, .locale = translation.locale, .key = key});
        }
        messages.emplace(key, std::move(message));
    }
    table.messages.emplace(translation.locale, std::move(messages));
    locales.insert(translation.locale);
    return {};
}

result::Result<std::pair<const Locale*, const Message*>>
Catalog::find(base::Bits128 table, std::string_view key, const Locale& requested, const Locale& projectDefault) const {
    const auto kTable = tables_.find(table);
    if (kTable == tables_.end() || !kTable->second.messages.at(kTable->second.sourceLocale).contains(key)) {
        return refused(LocalizationError::KeyUnknown, "a key asked for is in its table", table, nullptr, key);
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<Locale> kChain,
                        fallbackChain(requested, projectDefault, kTable->second.sourceLocale, limits_.chain));
    for (const Locale& locale : kChain) {
        const auto kLocale = kTable->second.messages.find(locale);
        if (kLocale == kTable->second.messages.end()) {
            continue;
        }
        const auto kMessage = kLocale->second.find(key);
        if (kMessage != kLocale->second.end()) {
            return std::pair{&kLocale->first, &kMessage->second};
        }
    }
    // The chain ends in the source locale, which has every key.
    const auto& source = *kTable->second.messages.find(kTable->second.sourceLocale);
    return std::pair{&source.first, &source.second.find(key)->second};
}

result::Result<Locale> Catalog::resolve(base::Bits128 table,
                                        std::string_view key,
                                        const Locale& requested,
                                        const Locale& projectDefault) const {
    RAWFRAME_TRY_ASSIGN(const auto kFound, find(table, key, requested, projectDefault));
    return *kFound.first;
}

result::Result<std::string> Catalog::format(base::Bits128 table,
                                            std::string_view key,
                                            const Locale& requested,
                                            const Locale& projectDefault,
                                            std::span<const Argument> arguments) const {
    RAWFRAME_TRY_ASSIGN(const auto kFound, find(table, key, requested, projectDefault));
    return localization::format(*kFound.second, *kFound.first, arguments, limits_.table.message);
}

} // namespace rawframe::localization
