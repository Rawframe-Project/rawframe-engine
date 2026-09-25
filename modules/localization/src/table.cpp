// The `text.strings` and `text.translations` documents (SPEC-0033).

#include "rawframe/localization/table.h"

#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/localization/errors.h"
#include "table_parts.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace rawframe::localization {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::DocumentInvalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kLocalizationDomain, code(LocalizationError::OverLimit), why);
}

constexpr std::string_view kHexDigits = "0123456789abcdef";

bool hasMembers(const Value& value, std::initializer_list<std::string_view> names) {
    if (value.kind() != Value::Kind::Object || value.names().size() != names.size()) {
        return false;
    }
    return std::ranges::all_of(names, [&value](std::string_view name) {
        return value.find(name) != nullptr;
    });
}

const std::string* stringOf(const Value* value) {
    return value != nullptr && value->kind() == Value::Kind::String ? value->text() : nullptr;
}

/// A locale in canonical form, as a document writes it: in case, and
/// already what intake makes of it, so no alias and no subtag CLDR does
/// not know (SPEC-0033's alias validity).
bool canonicalLocale(const Locale& locale) {
    const auto kParsed = parseLocale(locale.text());
    const auto kTaken = intake(locale.text());
    return kParsed.has_value() && *kParsed == locale && kTaken.has_value() && *kTaken == locale;
}

bool sourceHash(std::string_view text) {
    return text.size() == 16 && std::ranges::all_of(text, [](char each) {
               return kHexDigits.contains(each);
           });
}

/// What every entry's key and message must be, the map's size included.
template <typename Entries> result::Status entriesInForm(const Entries& entries, const TableLimits& limits) {
    if (entries.size() > limits.maximumKeys) {
        return overLimit("a document has more keys than its limit");
    }
    for (const auto& [key, entry] : entries) {
        if (!validKey(key, limits)) {
            return invalid("a key is dot-separated machine segments within their limits");
        }
        auto message = parseMessage(entry.message, limits.message);
        if (!message.has_value()) {
            return std::unexpected<result::Error>{std::move(message).error().withContext("key", key)};
        }
    }
    return {};
}

result::Status tableInForm(const StringTable& table, const TableLimits& limits) {
    if (!canonicalLocale(table.sourceLocale)) {
        return invalid("a table's source locale is a canonical tag");
    }
    return entriesInForm(table.entries, limits);
}

/// The document's members past its format and kind, if it is one of that
/// kind and has exactly these.
result::Result<Value> documentOf(std::string_view text,
                                 std::string_view kind,
                                 std::initializer_list<std::string_view> members,
                                 const TableLimits& limits) {
    if (text.size() > limits.maximumDocumentBytes) {
        return overLimit("a document is larger than its limit");
    }
    auto parsed = document::parse(text, {.maximumBytes = limits.maximumDocumentBytes, .maximumDepth = 8});
    if (!parsed.has_value()) {
        return invalid("a document is strict JSON");
    }
    const Value* kKind = parsed->find("kind");
    const Value* kVersion = parsed->find("formatVersion");
    const Value* kEntries = parsed->find("entries");
    if (!hasMembers(*parsed, members) || stringOf(kKind) == nullptr || *stringOf(kKind) != kind ||
        kVersion->kind() != Value::Kind::Number || kVersion->integer() != 1 ||
        kEntries->kind() != Value::Kind::Object) {
        return invalid("a document is format version 1 of its kind, with exactly its members");
    }
    if (kEntries->names().size() > limits.maximumKeys) {
        return overLimit("a document has more keys than its limit");
    }
    return std::move(*parsed);
}

/// What the writer makes of it is the text, byte for byte, or the text was
/// not in the one form.
result::Status canonical(std::string_view text, result::Result<std::string> written) {
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, std::move(written));
    if (kWritten != text) {
        return invalid("a document is not in its canonical form");
    }
    return {};
}

} // namespace

bool privateUseLocale(const Locale& locale) {
    const auto kParsed = parseLocale(locale.text());
    return kParsed.has_value() && *kParsed == locale && locale.region.size() == 2 && locale.region[0] == 'X';
}

result::Status translationsInForm(const Translations& translations, const TableLimits& limits, bool pseudo) {
    if (translations.table == base::Bits128{} ||
        !(pseudo ? privateUseLocale(translations.locale) : canonicalLocale(translations.locale))) {
        return invalid("a translation document names its table and a canonical locale tag");
    }
    for (const auto& [key, entry] : translations.entries) {
        if (!sourceHash(entry.sourceHash)) {
            return invalid("a source hash is 16 lowercase hex digits");
        }
    }
    return entriesInForm(translations.entries, limits);
}

bool validKey(std::string_view key, const TableLimits& limits) {
    if (key.empty() || key.size() > limits.maximumKeyBytes) {
        return false;
    }
    std::size_t segments = 1;
    bool segmentStart = true;
    for (const char kEach : key) {
        if (kEach == '.') {
            if (segmentStart) {
                return false;
            }
            ++segments;
            segmentStart = true;
            continue;
        }
        const bool kLetter = kEach >= 'a' && kEach <= 'z';
        if (!kLetter && (segmentStart || !((kEach >= '0' && kEach <= '9') || kEach == '_'))) {
            return false;
        }
        segmentStart = false;
    }
    return !segmentStart && segments <= limits.maximumKeySegments;
}

std::string sourceHashOf(std::string_view message) {
    const base::Sha256Digest kDigest = base::sha256(message);
    std::string made;
    for (std::size_t at = 0; at < 8; ++at) {
        const auto kByte = std::to_integer<unsigned>(kDigest[at]);
        made += kHexDigits[kByte >> 4U];
        made += kHexDigits[kByte & 0xFU];
    }
    return made;
}

result::Result<std::string> writeStrings(const StringTable& table, const TableLimits& limits) {
    RAWFRAME_TRY(tableInForm(table, limits));
    Value entries = Value::object();
    for (const auto& [key, entry] : table.entries) {
        Value made = Value::object();
        made.add("message", Value::string(entry.message));
        if (!entry.description.empty()) {
            made.add("description", Value::string(entry.description));
        }
        entries.add(key, std::move(made));
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("text.strings"));
    made.add("sourceLocale", Value::string(table.sourceLocale.text()));
    made.add("entries", std::move(entries));
    return document::write(made);
}

result::Result<StringTable> readStrings(std::string_view text, const TableLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const Value kDocument,
                        documentOf(text, "text.strings", {"formatVersion", "kind", "sourceLocale", "entries"}, limits));
    const std::string* sourceLocale = stringOf(kDocument.find("sourceLocale"));
    if (sourceLocale == nullptr) {
        return invalid("a table's source locale is a tag");
    }
    const auto kLocale = parseLocale(*sourceLocale);
    if (!kLocale.has_value()) {
        return invalid("a table's source locale is a canonical tag");
    }
    StringTable table{.sourceLocale = *kLocale, .entries = {}};
    const Value& entries = *kDocument.find("entries");
    for (std::size_t at = 0; at < entries.names().size(); ++at) {
        const Value& each = entries.items()[at];
        const bool kDescribed = each.find("description") != nullptr;
        const std::string* message = stringOf(each.find("message"));
        const std::string* description = stringOf(each.find("description"));
        if (!(kDescribed ? hasMembers(each, {"message", "description"}) : hasMembers(each, {"message"})) ||
            message == nullptr || (kDescribed && (description == nullptr || description->empty()))) {
            return invalid("an entry is a message and an optional description that says something");
        }
        table.entries.emplace(entries.names()[at],
                              SourceEntry{.message = *message, .description = kDescribed ? *description : ""});
    }
    RAWFRAME_TRY(canonical(text, writeStrings(table, limits)));
    return table;
}

result::Result<std::string> writeTranslations(const Translations& translations, const TableLimits& limits) {
    RAWFRAME_TRY(translationsInForm(translations, limits, false));
    Value entries = Value::object();
    for (const auto& [key, entry] : translations.entries) {
        Value made = Value::object();
        made.add("message", Value::string(entry.message));
        made.add("sourceHash", Value::string(entry.sourceHash));
        entries.add(key, std::move(made));
    }
    std::array<char, base::kBits128HexDigits> table{};
    base::formatBits128Hex(translations.table, table);
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("text.translations"));
    made.add("table", Value::string(std::string{table.data(), table.size()}));
    made.add("locale", Value::string(translations.locale.text()));
    made.add("entries", std::move(entries));
    return document::write(made);
}

result::Result<Translations> readTranslations(std::string_view text, const TableLimits& limits) {
    RAWFRAME_TRY_ASSIGN(
        const Value kDocument,
        documentOf(text, "text.translations", {"formatVersion", "kind", "table", "locale", "entries"}, limits));
    const std::string* table = stringOf(kDocument.find("table"));
    const std::string* locale = stringOf(kDocument.find("locale"));
    const base::Bits128Parse kTable = base::parseBits128Hex(table != nullptr ? *table : "");
    const auto kLocale = parseLocale(locale != nullptr ? *locale : "");
    if (!kTable.parsed || !kLocale.has_value()) {
        return invalid("a translation document names its table by 32 hex digits and a canonical locale tag");
    }
    Translations translations{.table = kTable.value, .locale = *kLocale, .entries = {}};
    const Value& entries = *kDocument.find("entries");
    for (std::size_t at = 0; at < entries.names().size(); ++at) {
        const Value& each = entries.items()[at];
        const std::string* message = stringOf(each.find("message"));
        const std::string* hash = stringOf(each.find("sourceHash"));
        if (!hasMembers(each, {"message", "sourceHash"}) || message == nullptr || hash == nullptr) {
            return invalid("a translated entry is a message and its source hash");
        }
        translations.entries.emplace(entries.names()[at], TranslatedEntry{.message = *message, .sourceHash = *hash});
    }
    RAWFRAME_TRY(canonical(text, writeTranslations(translations, limits)));
    return translations;
}

} // namespace rawframe::localization
