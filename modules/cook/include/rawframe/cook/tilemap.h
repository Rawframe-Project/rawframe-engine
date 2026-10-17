#pragma once

// The tilemap importer (`rawframe.tilemap`, D858): a tilemap document
// (D857) cooked into a resource of its own, its text checked and kept as it
// was written, which a client reads to draw the map's tiles. It takes no
// settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer tilemapImporter() noexcept;

} // namespace rawframe::cook
