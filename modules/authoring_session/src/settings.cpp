#include "settings.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/base/files.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/cook/importers.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace rawframe::authoring_session {

namespace {

using document::Value;

result::Error failure(authoring::AuthoringError error, result::ErrorClass kind, std::string_view why) {
    return result::fail(kind, authoring::kAuthoringDomain, authoring::code(error), why).error();
}

std::string_view kindName(cook::SettingField::Kind kind) {
    switch (kind) {
    case cook::SettingField::Kind::Truth:
        return "truth";
    case cook::SettingField::Kind::Text:
        return "text";
    case cook::SettingField::Kind::Integer:
        return "integer";
    }
    return "text";
}

/// `value` as its field's text, as a fallback is written; none when it is
/// not of the field's kind.
std::optional<std::string> textOfValue(const cook::SettingField& field, const Value& value) {
    switch (field.kind) {
    case cook::SettingField::Kind::Truth:
        if (value.kind() == Value::Kind::Bool) {
            return value.truth().value_or(false) ? "true" : "false";
        }
        return std::nullopt;
    case cook::SettingField::Kind::Text:
        if (value.kind() == Value::Kind::String) {
            return *value.text();
        }
        return std::nullopt;
    case cook::SettingField::Kind::Integer:
        if (const std::optional<std::int64_t> kNumber = value.integer()) {
            return std::to_string(*kNumber);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

/// A fallback's text as its field's value.
Value valueOfText(const cook::SettingField& field, std::string_view text) {
    switch (field.kind) {
    case cook::SettingField::Kind::Truth:
        return Value::boolean(text == "true");
    case cook::SettingField::Kind::Text:
        return Value::string(std::string{text});
    case cook::SettingField::Kind::Integer:
        return Value::integer(std::stoll(std::string{text}));
    }
    return Value::string(std::string{text});
}

struct Found {
    std::filesystem::path sidecar;
    content::Sidecar read;
    const cook::Importer* importer = nullptr;
};

result::Result<Found>
found(const std::filesystem::path& game, std::string_view asset, std::span<const cook::Importer> importers) {
    // Only a source the session lists: a path out of the game's directory
    // is never one.
    const std::vector<SourceListed> kSources = sourcesBeside(game);
    if (std::ranges::find(kSources, asset, &SourceListed::path) == kSources.end()) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "no source with a sidecar is at that path under the game's directory")};
    }
    Found made;
    made.sidecar = game.parent_path() / (std::string{asset} + std::string{content::kSidecarSuffix});
    const auto kText = readFile(made.sidecar);
    if (!kText.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::TargetNotFound,
                                       result::ErrorClass::NotFound,
                                       "the source's sidecar does not read")};
    }
    RAWFRAME_TRY_ASSIGN(made.read, content::readSidecar(*kText));
    made.importer = cook::importerNamed(importers, made.read.importer);
    return made;
}

Value answerOf(std::string_view asset, const Found& source) {
    Value made = Value::object();
    made.add("kind", Value::string("authoring.import_settings"));
    made.add("asset", Value::string(std::string{asset}));
    made.add("importer", Value::string(source.read.importer));
    made.add("settings", source.read.settings.value_or(Value{}));
    Value fields = Value::array();
    if (source.importer != nullptr) {
        for (const cook::SettingField& kField : source.importer->fields) {
            Value each = Value::object();
            each.add("name", Value::string(std::string{kField.name}));
            each.add("type", Value::string(std::string{kindName(kField.kind)}));
            each.add("fallback", valueOfText(kField, kField.fallback));
            if (!kField.choices.empty()) {
                Value choices = Value::array();
                for (const std::string_view kChoice : kField.choices) {
                    choices.push(Value::string(std::string{kChoice}));
                }
                each.add("choices", std::move(choices));
            }
            fields.push(std::move(each));
        }
    }
    made.add("fields", std::move(fields));
    return made;
}

} // namespace

result::Result<Value>
importSettingsOf(const std::filesystem::path& game, std::string_view asset, std::span<const cook::Importer> importers) {
    RAWFRAME_TRY_ASSIGN(const Found kSource, found(game, asset, importers));
    return answerOf(asset, kSource);
}

result::Result<Value> setImportSettings(const std::filesystem::path& game,
                                        std::string_view asset,
                                        const Value& given,
                                        std::span<const cook::Importer> importers) {
    RAWFRAME_TRY_ASSIGN(Found source, found(game, asset, importers));
    if (source.importer == nullptr || source.importer->fields.empty()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "this source's importer offers no settings")};
    }
    // The settings as they are, in name order, a member no field names (a
    // texture's environment) kept.
    std::map<std::string, Value, std::less<>> settings;
    if (source.read.settings.has_value() && source.read.settings->kind() == Value::Kind::Object) {
        for (std::size_t at = 0; at < source.read.settings->names().size(); ++at) {
            settings.emplace(source.read.settings->names()[at], source.read.settings->items()[at]);
        }
    }
    for (std::size_t at = 0; at < given.names().size(); ++at) {
        const std::string& kName = given.names()[at];
        const auto kField = std::ranges::find(source.importer->fields, kName, &cook::SettingField::name);
        if (kField == source.importer->fields.end()) {
            return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                           result::ErrorClass::InvalidArgument,
                                           "a setting the source's importer does not offer")
                                       .withContext("setting", kName)};
        }
        const std::optional<std::string> kText = textOfValue(*kField, given.items()[at]);
        if (!kText.has_value()) {
            return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                           result::ErrorClass::InvalidArgument,
                                           "a setting's value is not of its kind")
                                       .withContext("setting", kName)};
        }
        if (*kText == kField->fallback) {
            settings.erase(kName);
        } else {
            settings.insert_or_assign(kName, given.items()[at]);
        }
    }
    std::optional<Value> written;
    if (!settings.empty()) {
        written = Value::object();
        for (auto& [kName, value] : settings) {
            written->add(kName, std::move(value));
        }
    }
    // The importer's to accept, as the cook would.
    if (const auto kNormalized = source.importer->normalize(written.has_value() ? &*written : nullptr);
        !kNormalized.has_value()) {
        return std::unexpected{failure(authoring::AuthoringError::ValidationFailed,
                                       result::ErrorClass::InvalidArgument,
                                       "the source's importer refused the settings")
                                   .withContext("why", std::string{kNormalized.error().description()})};
    }
    source.read.settings = std::move(written);
    const std::string kText = content::writeSidecar(source.read);
    if (!base::replaceFile(source.sidecar, std::as_bytes(std::span{kText}))) {
        return std::unexpected{failure(authoring::AuthoringError::Internal,
                                       result::ErrorClass::Unavailable,
                                       "the source's sidecar could not be written")};
    }
    return answerOf(asset, source);
}

} // namespace rawframe::authoring_session
