#pragma once

// The texture importer (`rawframe.texture`, ADR-0058, D253): a PNG, JPEG,
// BMP, or TGA source cooked into the runtime's texture. Its settings say
// whether the image is `linear` data rather than sRGB color, whether it
// stays `exact` rather than BC7, and whether it has no `levels` below the
// image; each is omitted at its default.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer textureImporter() noexcept;

} // namespace rawframe::cook
