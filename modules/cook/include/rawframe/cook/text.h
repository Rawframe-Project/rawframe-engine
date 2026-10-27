#pragma once

// The text importer (`rawframe.text`): a string table or translation
// document cooked into a resource of its kind, its text in the
// one form it is read in, every message checked. What needs more than one
// document (orphans, the asymmetry rule, staleness) is the catalog's, when
// one is built from the cooked resources.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer textImporter() noexcept;

} // namespace rawframe::cook
