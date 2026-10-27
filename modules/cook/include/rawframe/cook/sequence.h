#pragma once

// The sequence importer (`rawframe.sequence`): a sequence document
// cooked into a resource of its own, its text checked and kept as it was
// written, which a game reads to play it. It takes no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer sequenceImporter() noexcept;

} // namespace rawframe::cook
