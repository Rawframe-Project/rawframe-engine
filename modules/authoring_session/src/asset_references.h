#pragma once

// What a source uses and what uses it (D803), as the session's last cook
// recorded it: each source's reads in the cook's receipt, a file it read
// to cook (a glTF's pictures, a game description's declared files, a
// project's Kest), turned from paths under the source's directory into the
// sources they are.

#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

/// `authoring.asset_references`'s answer for the source at `asset` under `game`'s
/// directory, from the cook into `cooked`: `uses`, the files it read (a
/// glTF's pictures need no sidecar of their own), and `usedBy`, the sources
/// that read it and the scenes `naming` says hold it in a field (D892),
/// each a path under the game's directory, in path order. Refused for a
/// path that is no source and for a game this session has not cooked
/// (`target_not_found`).
[[nodiscard]] result::Result<document::Value> assetReferencesOf(const std::filesystem::path& game,
                                                                std::string_view asset,
                                                                const std::optional<std::filesystem::path>& cooked,
                                                                std::span<const std::string> naming);

} // namespace rawframe::authoring_session
