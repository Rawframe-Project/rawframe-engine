#pragma once

// `rawframe-author session` (D407): an authoring session on standard input
// and output, as main.cpp describes it.

#include <filesystem>

namespace rawframe::author {

/// Holds a session on the game described at `game` over the scenes under
/// `root` until `end` or the input ends; 0 when every record succeeded.
int session(const std::filesystem::path& game, const std::filesystem::path& root);

} // namespace rawframe::author
