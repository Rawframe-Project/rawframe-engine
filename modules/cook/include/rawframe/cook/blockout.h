#pragma once

// The blockout importer (`rawframe.blockout`): a blockout document
// cooked into the runtime's mesh, its solids made one closed mesh,
// so a level blocked out in text is placed as any mesh is. It takes no
// settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer blockoutImporter() noexcept;

} // namespace rawframe::cook
