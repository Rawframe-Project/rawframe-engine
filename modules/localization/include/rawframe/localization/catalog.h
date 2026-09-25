#pragma once

// Catalogs (ADR-0050, SPEC-0033): string tables and their translations
// checked together, then asked for messages key by key.
//
// Building one checks what needs more than one document: each
// translation's table is in the catalog, each translated key is in it
// (else `Orphaned`), each translated message reads no argument its source
// does not (the asymmetry rule), no two translations share a table and a
// locale, and none is into its table's source locale. A translation whose
// source message changed since (its `sourceHash` differs) is stale: a
// report, not a failure. A catalog is immutable; a new generation is a new
// catalog.
//
// A key resolves along the fallback chain from the requested locale, the
// project's default, and the table's source locale (locale.h): the first
// locale with a message for it serves it, and the source locale always
// has one. The message is formatted in the locale that served it, so a
// fallback's numbers match its words.

#include "rawframe/base/bits128.h"
#include "rawframe/localization/locale.h"
#include "rawframe/localization/message.h"
#include "rawframe/localization/table.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::localization {

/// A table with its resource identity.
struct TableDocument {
    base::Bits128 id;
    StringTable table;
};

/// A translated entry whose source changed since it was translated.
struct StaleEntry {
    base::Bits128 table;
    Locale locale;
    std::string key;

    friend auto operator<=>(const StaleEntry&, const StaleEntry&) = default;
};

/// SPEC-0033's named limits for catalogs.
struct CatalogLimits {
    std::size_t maximumTables = 256;
    std::size_t maximumLocales = 64;
    TableLimits table;
    ChainLimits chain;
};

class Catalog {
public:
    /// Refuses documents out of their form as reading them would, and
    /// (`Orphaned`, `CatalogInvalid`, `OverLimit`) documents that do not fit
    /// together, with the table, locale, and key as context.
    [[nodiscard]] static result::Result<Catalog> build(std::span<const TableDocument> tables,
                                                       std::span<const Translations> translations,
                                                       const CatalogLimits& limits = {});

    /// Stale entries, in table, locale, and key order.
    [[nodiscard]] std::span<const StaleEntry> stale() const noexcept {
        return stale_;
    }

    /// The locale that serves a key. Refuses (`KeyUnknown`) a key or table
    /// the catalog does not have, and (`OverLimit`) a chain past its limit.
    [[nodiscard]] result::Result<Locale>
    resolve(base::Bits128 table, std::string_view key, const Locale& requested, const Locale& projectDefault) const;

    /// The key's message formatted in the locale that serves it; refuses as
    /// `resolve` does and as `format` (message.h) does.
    [[nodiscard]] result::Result<std::string> format(base::Bits128 table,
                                                     std::string_view key,
                                                     const Locale& requested,
                                                     const Locale& projectDefault,
                                                     std::span<const Argument> arguments) const;

private:
    struct Table {
        Locale sourceLocale;
        /// Each locale's messages, the source locale's among them.
        std::map<Locale, std::map<std::string, Message, std::less<>>> messages;
    };

    [[nodiscard]] result::Result<std::pair<const Locale*, const Message*>>
    find(base::Bits128 table, std::string_view key, const Locale& requested, const Locale& projectDefault) const;

    std::map<base::Bits128, Table> tables_;
    std::vector<StaleEntry> stale_;
    CatalogLimits limits_;
};

} // namespace rawframe::localization
