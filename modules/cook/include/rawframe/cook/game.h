#pragma once

// The game importer (`rawframe.game`, D88): a game description cooked with
// everything it names, so a process reads the game from content alone. The
// documents it names (scenes, actions, mixer, sounds) are read, checked as their
// owners read them, and held in the cooked description; each program it
// names is an entry among the Kest sources of the `kest.project` beside it,
// named by that project's sidecar, and must compile with the engine's
// library.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer gameImporter() noexcept;

} // namespace rawframe::cook
