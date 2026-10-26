#pragma once

// The data documents a session edits, each its extension and the
// reader of its own module: whether a text reads as that kind, as the cook
// would read it.

#include "rawframe/authoring/json_document.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/result/result.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

struct DataKind {
    std::string_view extension;
    /// What it is called in what is said of it.
    std::string_view called;
    authoring::DocumentReader reads = nullptr;
};

/// Action maps, mixers, string tables, their translations, and animation
/// graphs and clips.
[[nodiscard]] std::span<const DataKind> dataKinds() noexcept;

/// Whether `text` reads as a game description, as world_kest reads it: the
/// reader of a game's line document, its settings.
[[nodiscard]] result::Status readsGameDescription(std::string_view text);

/// Whether `text` reads as a blockout and its solids make a mesh within
/// the format's limits, as the cook makes it: the reader of a
/// blockout's line document.
[[nodiscard]] result::Status readsBlockout(std::string_view text);

/// Whether `text` reads as a terrain whose mesh is made, and the
/// lines a brush's stroke leaves of one: the terrain module's brush, its
/// document written again.
[[nodiscard]] result::Status readsTerrain(std::string_view text);
[[nodiscard]] result::Result<std::vector<std::string>> rewritesTerrain(std::span<const std::string> lines,
                                                                       const authoring::Operation& operation);

/// Whether `text` reads as a scatter: the reader of a scatter's
/// line document.
[[nodiscard]] result::Status readsScatter(std::string_view text);

/// Whether `text` reads as a sequence: the reader of a sequence's
/// line document.
[[nodiscard]] result::Status readsSequence(std::string_view text);

/// Whether `text` reads as a tilemap, and the lines tiles painted
/// leave of one: the tilemap module's paint, its document written again.
[[nodiscard]] result::Status readsTilemap(std::string_view text);
[[nodiscard]] result::Result<std::vector<std::string>> rewritesTilemap(std::span<const std::string> lines,
                                                                       const authoring::Operation& operation);

} // namespace rawframe::authoring_session
