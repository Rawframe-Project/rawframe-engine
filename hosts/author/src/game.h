#pragma once

// What every command of the authoring tool reads of a game and its scenes:
// files, digests, the component catalog from the game's program, and the
// scenes beside the description by their sidecars' identities.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game_files.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::author {

/// A file's bytes, if it reads.
std::optional<std::string> readFile(const std::filesystem::path& path);

/// A document's generation between processes: `sha256:` and its digest.
std::string digestOf(std::string_view text);

/// The components the game's program declares, each field typed from its
/// layout, an `entity` field a reference.
result::Result<authoring::ComponentCatalog> catalogOf(const world_kest::GameFiles& files);

/// A slot of an outcome: its deltas, or the one error record.
document::Value slotValue(const result::Result<authoring::Committed>& outcome);

/// The identity a scene's sidecar gives it, if it has one that reads.
std::optional<base::Bits128> sidecarIdentity(const std::filesystem::path& source);

/// Every scene under the game description's directory, by the identity its
/// sidecar gives it.
std::vector<std::pair<base::Bits128, std::filesystem::path>> scenesBeside(const std::filesystem::path& game);

} // namespace rawframe::author
