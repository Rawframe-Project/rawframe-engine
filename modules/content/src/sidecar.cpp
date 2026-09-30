#include "rawframe/content/sidecar.h"

#include "rawframe/content/errors.h"
#include "rawframe/document/record.h"

#include <algorithm>
#include <array>
#include <set>

namespace rawframe::content {

namespace {

using document::Value;

constexpr std::array<std::string_view, 5> kFields = {"schema", "resourceId", "importer", "settings", "subassets"};

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kContentDomain, code(ContentError::SidecarInvalid), why);
}

} // namespace

bool isSubassetKey(std::string_view key) noexcept {
    const std::size_t kSlash = key.find('/');
    if (kSlash == 0 || kSlash == std::string_view::npos || kSlash + 1 == key.size()) {
        return false;
    }
    const std::string_view kFamily = key.substr(0, kSlash);
    return std::ranges::all_of(kFamily,
                               [](char letter) {
                                   return letter >= 'a' && letter <= 'z';
                               }) &&
           std::ranges::none_of(key.substr(kSlash + 1), [](char letter) {
               return static_cast<unsigned char>(letter) < 0x20U || letter == 0x7F;
           });
}

result::Result<Sidecar> readSidecar(std::string_view text) {
    RAWFRAME_TRY_ASSIGN(const Value kParsed, document::parseCanonical(text));
    const Value* schema = kParsed.find("schema");
    if (schema == nullptr || schema->integer() != 1) {
        return invalid("a sidecar's schema is 1");
    }
    RAWFRAME_TRY_ASSIGN(const document::Record kRecord, document::Record::of(kParsed, kFields, "$"));
    const auto kId = kRecord.text("resourceId");
    const base::Bits128Parse kParsedId = kId.has_value() ? base::parseBits128Hex(*kId) : base::Bits128Parse{};
    if (!kParsedId.parsed || kParsedId.value == base::Bits128{}) {
        return invalid("a sidecar names a resource identity, not nought");
    }
    RAWFRAME_TRY_ASSIGN(const std::string_view kImporter, kRecord.text("importer"));
    RAWFRAME_TRY_ASSIGN(const Value* kSettings, kRecord.optional("settings", Value::Kind::Object));
    RAWFRAME_TRY_ASSIGN(const Value* kSubassets, kRecord.optional("subassets", Value::Kind::Object));
    Sidecar sidecar{.id = ResourceId{kParsedId.value},
                    .importer = std::string{kImporter},
                    .settings = kSettings != nullptr ? std::optional<Value>{*kSettings} : std::nullopt};
    if (kSubassets != nullptr) {
        std::set<base::Bits128> seen{kParsedId.value};
        for (std::size_t index = 0; index < kSubassets->names().size(); ++index) {
            const std::string& kKey = kSubassets->names()[index];
            if (!isSubassetKey(kKey) || (index > 0 && kSubassets->names()[index - 1] >= kKey)) {
                return invalid("a sidecar's subassets are keyed by family and name, in order");
            }
            const Value& kWritten = kSubassets->items()[index];
            const base::Bits128Parse kSubasset =
                kWritten.kind() == Value::Kind::String ? base::parseBits128Hex(*kWritten.text()) : base::Bits128Parse{};
            if (!kSubasset.parsed || kSubasset.value == base::Bits128{} || !seen.insert(kSubasset.value).second) {
                return invalid("a sidecar's subassets are distinct identities, not nought or its own");
            }
            sidecar.subassets.emplace(kKey, ResourceId{kSubasset.value});
        }
    }
    return sidecar;
}

std::string writeSidecar(const Sidecar& sidecar) {
    const auto kHex = [](base::Bits128 id) {
        std::array<char, base::kBits128HexDigits> digits{};
        base::formatBits128Hex(id, digits);
        return std::string{digits.data(), digits.size()};
    };
    Value written = Value::object();
    written.add("schema", Value::integer(1));
    written.add("resourceId", Value::string(kHex(sidecar.id.value)));
    written.add("importer", Value::string(sidecar.importer));
    if (sidecar.settings.has_value()) {
        written.add("settings", *sidecar.settings);
    }
    if (!sidecar.subassets.empty()) {
        Value subassets = Value::object();
        for (const auto& [kKey, kId] : sidecar.subassets) {
            subassets.add(kKey, Value::string(kHex(kId.value)));
        }
        written.add("subassets", std::move(subassets));
    }
    return document::write(written);
}

} // namespace rawframe::content
