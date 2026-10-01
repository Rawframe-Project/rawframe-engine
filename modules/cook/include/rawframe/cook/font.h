#pragma once

// The font importer (`rawframe.font`, ADR-0049, ADR-0078, D385): a TrueType
// or OpenType font or collection rebuilt by the OpenType Sanitizer, and
// cooked only once the runtime's own reader takes what was rebuilt. It takes
// no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer fontImporter() noexcept;

} // namespace rawframe::cook
