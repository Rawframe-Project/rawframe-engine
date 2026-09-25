#pragma once

// The scene importer (`rawframe.scene`, D95): a scene document cooked into
// a resource of its own, its text in the one form it is read in, so a game
// or another scene names it by identity.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer sceneImporter() noexcept;

} // namespace rawframe::cook
