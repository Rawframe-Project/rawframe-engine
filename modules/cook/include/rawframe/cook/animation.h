#pragma once

// The animation importer (`rawframe.animation`): a skeleton, clip, or graph
// document (the `.rfanim` family) cooked into a resource of its kind, its
// text in the one form it is read in, so a graph names its clips and a
// clip its skeleton by identity.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer animationImporter() noexcept;

} // namespace rawframe::cook
