#pragma once

// The texture importer (`rawframe.texture`, ADR-0058, D253): a PNG, JPEG,
// BMP, or TGA source cooked into the runtime's texture. Its settings say
// whether the image is `linear` data rather than sRGB color, whether it
// stays `exact` rather than BC7, and whether it has no `levels` below the
// image; each is omitted at its default. A Radiance source is an
// `environment` (D321): a cube of its light, prefiltered by roughness, whose
// `side`, `levels`, and `samples` are those settings, each omitted at its
// default; it takes none of the others.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer textureImporter() noexcept;

} // namespace rawframe::cook
