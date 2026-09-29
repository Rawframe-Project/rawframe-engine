#include "rawframe/cook/texture.h"

#include "rawframe/cook/errors.h"
#include "rawframe/document/record.h"
#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <array>

namespace rawframe::cook {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 3> kSettingsFields = {"color", "exact", "levels"};

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why).error()};
}

/// `color=srgb|linear;exact=0|1;levels=0|1`.
result::Result<std::string> normalize(const Value* settings) {
    texture_import::CookSettings chosen;
    if (settings != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*settings, kSettingsFields, "$.settings"));
        RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kColor, kRecord.optionalText("color"));
        if (kColor == "srgb") {
            return document::notCanonical(kRecord.pathOf("color"), "a field at its default is omitted");
        }
        if (kColor.has_value() && *kColor != "linear") {
            return refuse("a texture's color is srgb or linear");
        }
        chosen.srgb = !kColor.has_value();
        RAWFRAME_TRY_ASSIGN(chosen.exact, kRecord.truth("exact", false));
        RAWFRAME_TRY_ASSIGN(chosen.levels, kRecord.truth("levels", true));
    }
    return std::string{"color="} + (chosen.srgb ? "srgb" : "linear") + ";exact=" + (chosen.exact ? "1" : "0") +
           ";levels=" + (chosen.levels ? "1" : "0");
}

result::Result<Artifact> cookTexture(std::span<const std::byte> source, std::string_view settings, Reads& /*reads*/) {
    const texture_import::CookSettings kSettings{.srgb = settings.contains("color=srgb"),
                                                 .exact = settings.contains("exact=1"),
                                                 .levels = settings.contains("levels=1")};
    RAWFRAME_TRY_ASSIGN(const texture_import::Image kImage, texture_import::decodeImage(source));
    RAWFRAME_TRY_ASSIGN(const texture::Texture kTexture, texture_import::cookTexture(kImage, kSettings));
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, texture::encode(kTexture));
    return Artifact{.type = content::ResourceTypeId{texture::kTextureType},
                    .representation = *content::RepresentationId::parse(texture::kTextureRepresentation),
                    .bytes = std::move(bytes)};
}

} // namespace

Importer textureImporter() noexcept {
    return Importer{.identity = "rawframe.texture", .normalize = &normalize, .cook = &cookTexture};
}

} // namespace rawframe::cook
