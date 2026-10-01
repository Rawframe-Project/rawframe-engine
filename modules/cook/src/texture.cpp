#include "rawframe/cook/texture.h"

#include "rawframe/cook/errors.h"
#include "rawframe/document/record.h"
#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>

namespace rawframe::cook {

namespace {

using document::Record;
using document::Value;

constexpr std::array<std::string_view, 5> kSettingsFields = {"color", "exact", "levels", "environment", "grading"};
constexpr std::array<std::string_view, 3> kEnvironmentFields = {"side", "levels", "samples"};

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kCookDomain, code(CookError::BadSidecar), why).error()};
}

/// `environment;side=<n>;levels=<n>;samples=<n>`, each within 32 bits;
/// the importer refuses what is outside its ranges.
result::Result<std::string> normalizeEnvironment(const Record& settings) {
    RAWFRAME_TRY_ASSIGN(const Value* const kValue, settings.required("environment", Value::Kind::Object));
    RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*kValue, kEnvironmentFields, settings.pathOf("environment")));
    RAWFRAME_TRY_ASSIGN(const std::optional<std::string_view> kColor, settings.optionalText("color"));
    RAWFRAME_TRY_ASSIGN(const Value* const kExact, settings.optional("exact", Value::Kind::Bool));
    RAWFRAME_TRY_ASSIGN(const Value* const kLevels, settings.optional("levels", Value::Kind::Bool));
    if (kColor.has_value() || kExact != nullptr || kLevels != nullptr) {
        return refuse("an environment is linear half floats whose levels are roughnesses: no color, exact, or levels");
    }
    const texture_import::EnvironmentSettings kDefaults;
    std::string normalized{"environment"};
    for (const auto& [kField, kDefault] : {std::pair{kEnvironmentFields[0], kDefaults.side},
                                           std::pair{kEnvironmentFields[1], kDefaults.levels},
                                           std::pair{kEnvironmentFields[2], kDefaults.samples}}) {
        RAWFRAME_TRY_ASSIGN(const std::int64_t kNumber, kRecord.integer(kField, kDefault));
        if (kNumber < 0 || kNumber > std::numeric_limits<std::uint32_t>::max()) {
            return refuse("an environment's settings are whole numbers within 32 bits");
        }
        normalized += ";" + std::string{kField} + "=" + std::to_string(kNumber);
    }
    return normalized;
}

/// A normalized environment setting's number.
std::uint32_t numberOf(std::string_view settings, std::string_view field) noexcept {
    const std::size_t kAt = settings.find(";" + std::string{field} + "=");
    std::uint32_t number = 0;
    if (kAt != std::string_view::npos) {
        const std::string_view kRest = settings.substr(kAt + field.size() + 2);
        static_cast<void>(std::from_chars(kRest.data(), kRest.data() + kRest.size(), number));
    }
    return number;
}

bool radiance(std::span<const std::byte> source) noexcept {
    const std::string_view kStart{reinterpret_cast<const char*>(source.data()),
                                  std::min<std::size_t>(source.size(), 6)};
    return kStart.starts_with("#?");
}

/// `color=srgb|linear;exact=0|1;levels=0|1`, an environment's, or
/// `grading`: a grading table (D344), which takes no other setting.
result::Result<std::string> normalize(const Value* settings) {
    texture_import::CookSettings chosen;
    if (settings != nullptr) {
        RAWFRAME_TRY_ASSIGN(const Record kRecord, Record::of(*settings, kSettingsFields, "$.settings"));
        RAWFRAME_TRY_ASSIGN(const Value* const kGrading, kRecord.optional("grading", Value::Kind::Bool));
        if (kGrading != nullptr) {
            RAWFRAME_TRY_ASSIGN(const bool kTable, kRecord.truth("grading", false));
            if (!kTable) {
                return document::notCanonical(kRecord.pathOf("grading"), "a field at its default is omitted");
            }
            if (settings->names().size() != 1) {
                return refuse("a grading table is linear half floats as its file has them: no other setting");
            }
            return std::string{"grading"};
        }
        RAWFRAME_TRY_ASSIGN(const Value* const kEnvironment, kRecord.optional("environment", Value::Kind::Object));
        if (kEnvironment != nullptr) {
            return normalizeEnvironment(kRecord);
        }
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
    texture::Texture cooked;
    if (settings == "grading") {
        RAWFRAME_TRY_ASSIGN(cooked, texture_import::decodeGrading(source));
    } else if (settings.starts_with("environment")) {
        if (!radiance(source)) {
            return refuse("an environment is a Radiance picture");
        }
        RAWFRAME_TRY_ASSIGN(const texture_import::LightImage kImage, texture_import::decodeRadiance(source));
        RAWFRAME_TRY_ASSIGN(cooked,
                            texture_import::cookEnvironment(kImage,
                                                            {.side = numberOf(settings, "side"),
                                                             .levels = numberOf(settings, "levels"),
                                                             .samples = numberOf(settings, "samples")}));
    } else {
        if (radiance(source)) {
            return refuse("a Radiance picture is cooked as an environment");
        }
        const texture_import::CookSettings kSettings{.srgb = settings.contains("color=srgb"),
                                                     .exact = settings.contains("exact=1"),
                                                     .levels = settings.contains("levels=1")};
        RAWFRAME_TRY_ASSIGN(const texture_import::Image kImage, texture_import::decodeImage(source));
        RAWFRAME_TRY_ASSIGN(cooked, texture_import::cookTexture(kImage, kSettings));
    }
    RAWFRAME_TRY_ASSIGN(std::vector<std::byte> bytes, texture::encode(cooked));
    return Artifact{.type = content::ResourceTypeId{texture::kTextureType},
                    .representation = *content::RepresentationId::parse(texture::kTextureRepresentation),
                    .bytes = std::move(bytes)};
}

} // namespace

Importer textureImporter() noexcept {
    return Importer{.identity = "rawframe.texture", .normalize = &normalize, .cook = &cookTexture};
}

} // namespace rawframe::cook
