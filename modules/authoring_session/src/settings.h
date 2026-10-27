#pragma once

// A source's import settings in a session: read from its sidecar
// with the settings its importer offers, and set through that importer,
// which accepts them or says why not, before the sidecar is written.

#include "rawframe/cook/cook.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace rawframe::authoring_session {

/// `authoring.import_settings`' answer for the source at `asset` under
/// `game`'s directory: its importer, its settings (null for none), and the
/// fields its importer offers, each with its kind, fallback, and choices.
/// Refused (`not_found`) for a path the session does not list as a source.
[[nodiscard]] result::Result<document::Value>
importSettingsOf(const std::filesystem::path& game, std::string_view asset, std::span<const cook::Importer> importers);

/// Sets `given`'s fields on the source's settings: each a field its
/// importer offers, of that field's kind; one at its fallback is left out,
/// as the sidecar has it. The settings are checked by the importer, whose
/// refusal is passed on, then written to the sidecar, its identity and
/// subassets kept. Answers as `importSettingsOf` does, after.
[[nodiscard]] result::Result<document::Value> setImportSettings(const std::filesystem::path& game,
                                                                std::string_view asset,
                                                                const document::Value& given,
                                                                std::span<const cook::Importer> importers);

} // namespace rawframe::authoring_session
