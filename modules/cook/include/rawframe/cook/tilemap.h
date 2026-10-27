#pragma once

// The tilemap importer (`rawframe.tilemap`): a tilemap document
// cooked into a resource of its own, its text checked and kept as it
// was written, which a client reads to draw the map's tiles. It takes no
// settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer tilemapImporter() noexcept;

} // namespace rawframe::cook
