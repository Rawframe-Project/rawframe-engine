#pragma once

// The mod importer (`rawframe.mod`): a mod's description cooked with
// every scene it contributes, each the scene resource its sidecar names, so
// a process reads the mod from its Build alone, and the program its handlers
// are in, which must compile from the Kest sources of the `kest.project`
// beside it. The description must parse;
// what its contributions mean is checked against the game when a
// Composition opens it.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer modImporter() noexcept;

} // namespace rawframe::cook
