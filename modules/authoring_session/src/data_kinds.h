#pragma once

// The data documents a session edits (D811), each its extension and the
// reader of its own module: whether a text reads as that kind, as the cook
// would read it.

#include "rawframe/authoring/json_document.h"
#include "rawframe/result/result.h"

#include <array>
#include <span>
#include <string_view>

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
/// reader of a game's line document, its settings (D817).
[[nodiscard]] result::Status readsGameDescription(std::string_view text);

/// Whether `text` reads as a blockout and its solids make a mesh within
/// the format's limits, as the cook makes it (D849): the reader of a
/// blockout's line document.
[[nodiscard]] result::Status readsBlockout(std::string_view text);

} // namespace rawframe::authoring_session
