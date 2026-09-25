#include "rawframe/cook/text.h"

#include "rawframe/cook/errors.h"
#include "rawframe/document/json.h"
#include "rawframe/localization/table.h"

namespace rawframe::cook {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why);
}

/// Takes no settings.
result::Result<std::string> normalize(const document::Value* settings) {
    if (settings != nullptr) {
        return refuse("a text document takes no settings");
    }
    return std::string{};
}

result::Result<Artifact> cookText(std::span<const std::byte> source, std::string_view, Reads&) {
    const std::string_view kText{reinterpret_cast<const char*>(source.data()), source.size()};
    // The kind first, then the document read whole as that kind.
    const auto kParsed = document::parse(kText);
    const document::Value* kind = kParsed.has_value() ? kParsed->find("kind") : nullptr;
    const std::string* name = kind != nullptr && kind->kind() == document::Value::Kind::String ? kind->text() : nullptr;
    base::Bits128 type;
    std::string_view representation;
    if (name != nullptr && *name == "text.strings") {
        RAWFRAME_TRY(localization::readStrings(kText));
        type = localization::kStringsType;
        representation = localization::kStringsRepresentation;
    } else if (name != nullptr && *name == "text.translations") {
        RAWFRAME_TRY(localization::readTranslations(kText));
        type = localization::kTranslationsType;
        representation = localization::kTranslationsRepresentation;
    } else {
        return refuse("a text document is a string table or a translation document");
    }
    return Artifact{.type = content::ResourceTypeId{type},
                    .representation = *content::RepresentationId::parse(representation),
                    .bytes = {source.begin(), source.end()}};
}

} // namespace

Importer textImporter() noexcept {
    return Importer{.identity = "rawframe.text", .normalize = &normalize, .cook = &cookText};
}

} // namespace rawframe::cook
