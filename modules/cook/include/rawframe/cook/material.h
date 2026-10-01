#pragma once

// The material importer (`rawframe.material`, ADR-0027, D303): a surface
// material's graph document, read as hostile input in its canonical form,
// compiled, and cooked into the runtime's material. It takes no settings.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer materialImporter() noexcept;
/// The post-process importer (`rawframe.postprocess`, D348): a post
/// process's graph document, read and folded alike. It takes no settings.
[[nodiscard]] Importer postProcessImporter() noexcept;
/// The canvas material importer (`rawframe.canvasmaterial`, D355): a
/// canvas material's graph document, read and folded alike. It takes no
/// settings.
[[nodiscard]] Importer canvasImporter() noexcept;

} // namespace rawframe::cook
