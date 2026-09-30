#pragma once

// The material importer (`rawframe.material`, ADR-0027, D303): a surface
// material's graph document, read as hostile input in its canonical form,
// compiled, and cooked into the runtime's material. It takes no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer materialImporter() noexcept;

} // namespace rawframe::cook
