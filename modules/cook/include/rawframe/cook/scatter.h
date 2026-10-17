#pragma once

// The scatter importer (`rawframe.scatter`, D863): a scatter document
// (D862) cooked into its placements, the terrain it stands on read as one
// of its inputs, so a game read from its content places each copy exactly
// where its sources do without reading the terrain. It takes no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer scatterImporter() noexcept;

} // namespace rawframe::cook
