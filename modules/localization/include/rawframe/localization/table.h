#pragma once

// String tables and their translations (ADR-0050, SPEC-0033): the two
// authored document kinds, each read in its one canonical form.
//
//   {
//     "formatVersion": 1,
//     "kind": "text.strings",
//     "sourceLocale": "en",
//     "entries": {
//       "menu.play": {
//         "message": "Play",
//         "description": "The main menu's button that starts a match."
//       }
//     }
//   }
//
//   {
//     "formatVersion": 1,
//     "kind": "text.translations",
//     "table": "749e2ba7d0067a4db2348b183fc4d55f",
//     "locale": "tr",
//     "entries": {
//       "menu.play": {
//         "message": "Oyna",
//         "sourceHash": "3f1c0a2b9d8e7f60"
//       }
//     }
//   }
//
// A table's resource identity is its sidecar's, as every resource's is
// (D69); the document does not write it again (D144). A translation names
// its table by that identity, 32 lowercase hex digits. Keys are
// dot-separated machine segments, `^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*$`,
// in byte order; a description is present only when it says something.
// `sourceHash` is the first 16 lowercase hex digits of the SHA-256 of the
// source message's bytes when it was translated. Every message is read
// and checked as a message (message.h) when its document is.
//
// What needs both documents (a translation's keys and arguments against
// its table's, staleness, the locales differing) is the catalog's.

#include "rawframe/base/bits128.h"
#include "rawframe/localization/locale.h"
#include "rawframe/localization/message.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace rawframe::localization {

/// The resource types of the two kinds (D144), and their one
/// representation each: the document's text in its one form.
inline constexpr base::Bits128 kStringsType = base::parseBits128Hex("749e2ba7d0067a4db2348b183fc4d55f").value;
inline constexpr std::string_view kStringsRepresentation = "rawframe.text.strings";
inline constexpr base::Bits128 kTranslationsType = base::parseBits128Hex("4530b8b3c0311816d2a65a512c4f7070").value;
inline constexpr std::string_view kTranslationsRepresentation = "rawframe.text.translations";

/// SPEC-0033's named limits for tables and their documents.
struct TableLimits {
    std::size_t maximumKeys = 16384;
    std::size_t maximumKeyBytes = 128;
    std::size_t maximumKeySegments = 8;
    std::size_t maximumDocumentBytes = std::size_t{4} * 1024 * 1024;
    MessageLimits message;
};

struct SourceEntry {
    std::string message;
    /// Context for translators; empty for none. Never formatted.
    std::string description;

    friend bool operator==(const SourceEntry&, const SourceEntry&) = default;
};

/// A `text.strings` document: a key set in its source locale.
struct StringTable {
    Locale sourceLocale;
    std::map<std::string, SourceEntry, std::less<>> entries;

    friend bool operator==(const StringTable&, const StringTable&) = default;
};

struct TranslatedEntry {
    std::string message;
    /// The source message's hash when this was translated.
    std::string sourceHash;

    friend bool operator==(const TranslatedEntry&, const TranslatedEntry&) = default;
};

/// A `text.translations` document: one locale's messages for one table.
struct Translations {
    base::Bits128 table;
    Locale locale;
    std::map<std::string, TranslatedEntry, std::less<>> entries;

    friend bool operator==(const Translations&, const Translations&) = default;
};

/// A key in the grammar above and within its limits.
[[nodiscard]] bool validKey(std::string_view key, const TableLimits& limits = {});

/// The first 16 lowercase hex digits of the SHA-256 of a message's bytes.
[[nodiscard]] std::string sourceHashOf(std::string_view message);

/// Each refuses (`DocumentInvalid`) a document out of its form, key
/// grammar, or canonical bytes, (`MessageInvalid`, with the key as its
/// `key` context) an entry whose message is out of the subset, and
/// (`OverLimit`) one past a limit.
[[nodiscard]] result::Result<std::string> writeStrings(const StringTable& table, const TableLimits& limits = {});
[[nodiscard]] result::Result<StringTable> readStrings(std::string_view text, const TableLimits& limits = {});
[[nodiscard]] result::Result<std::string> writeTranslations(const Translations& translations,
                                                            const TableLimits& limits = {});
[[nodiscard]] result::Result<Translations> readTranslations(std::string_view text, const TableLimits& limits = {});

} // namespace rawframe::localization
