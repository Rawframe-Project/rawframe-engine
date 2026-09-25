#pragma once

// The mod importer (`rawframe.mod`, D178): a mod's description cooked with
// every scene it contributes, each the scene resource its sidecar names, so
// a process reads the mod from its Build alone. The description must parse;
// what its contributions mean is checked against the game when a
// Composition opens it (D177).

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer modImporter() noexcept;

} // namespace rawframe::cook
