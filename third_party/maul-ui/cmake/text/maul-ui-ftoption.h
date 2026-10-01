// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Maul UI's FreeType options (record mui-0006): FreeType's defaults, less
// what the text component refuses or does not need. Without zlib, LZW,
// bzip2 or Brotli, compressed fonts (WOFF, WOFF2, gzip) are refused, and
// without environment properties, nothing outside the build changes how
// glyphs render.
#ifndef MAUL_UI_FTOPTION_H
#define MAUL_UI_FTOPTION_H

#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
#undef FT_CONFIG_OPTION_USE_LZW
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_MAC_FONTS
#undef FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK
#undef FT_CONFIG_OPTION_INCREMENTAL
#undef FT_CONFIG_OPTION_SVG
#undef TT_CONFIG_OPTION_BDF

#endif // MAUL_UI_FTOPTION_H
