#pragma once

// A new game made from a template (SPEC-0051's new-project command, D780):
// what `rawframe-author new` and Studio's start screen both do, so a game
// begun either way is the same game.

#include "rawframe/result/result.h"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace rawframe::authoring_session {

struct NewGame {
    /// The new game's description, `<directory>/<name>.game`.
    std::filesystem::path description;
    std::size_t files = 0;
    /// The identities made fresh.
    std::size_t identities = 0;
};

/// Whether `name` can name a game: a lowercase letter, then lowercase
/// letters, digits, and `_`, at most 64 in all, as a Kest project's name.
[[nodiscard]] bool isGameName(std::string_view name) noexcept;

/// A new game at `directory`, which must be absent or empty, made from the
/// game in the template directory `from`, which holds one description: its
/// files copied, the description and the Kest project named after the new
/// directory, and every identity the template's sidecars and component
/// lines mint made fresh throughout its text files, so a game, a mod, and
/// another game made from one template never share one in a composition
/// (ADR-0023, SPEC-0008). Nothing is left at `directory` when it fails.
[[nodiscard]] result::Result<NewGame> newGame(const std::filesystem::path& from,
                                              const std::filesystem::path& directory);

} // namespace rawframe::authoring_session
