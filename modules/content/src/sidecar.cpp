#include "rawframe/content/sidecar.h"

#include "rawframe/content/errors.h"
#include "rawframe/document/record.h"

#include <array>

namespace rawframe::content {

namespace {

using document::Value;

constexpr std::array<std::string_view, 4> kFields = {"schema", "resourceId", "importer", "settings"};

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kContentDomain, code(ContentError::SidecarInvalid), why);
}

} // namespace

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
    return Sidecar{.id = ResourceId{kParsedId.value},
                   .importer = std::string{kImporter},
                   .settings = kSettings != nullptr ? std::optional<Value>{*kSettings} : std::nullopt};
}

} // namespace rawframe::content
