// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Maul UI's FreeType options: FreeType's defaults, less what the text
// component refuses or does not need. Without zlib, LZW, bzip2 or Brotli,
// compressed fonts (WOFF, WOFF2, gzip) are refused, and without
// environment properties, nothing outside the build changes how glyphs
// render.
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
// The TrueType bytecode interpreter stays: glyphs load with
// FT_LOAD_NO_HINTING, but FreeType still runs a tricky font's bytecode,
// since those fonts assemble their glyphs in it. ftoption.h has already
// derived TT_USE_BYTECODE_INTERPRETER, which builds it, so this undefine
// leaves out only the cvar table's variations of the values bytecode
// reads: no tricky font is a variable font.
#undef TT_CONFIG_OPTION_BYTECODE_INTERPRETER
#ifndef TT_USE_BYTECODE_INTERPRETER
#error "tricky fonts need FreeType's TrueType bytecode interpreter"
#endif
// The Adobe Glyph List only lets FreeType make a Unicode charmap from
// glyph names. The text component maps characters through HarfBuzz and
// reads no FreeType charmap or glyph name.
#undef FT_CONFIG_OPTION_ADOBE_GLYPH_LIST

#endif // MAUL_UI_FTOPTION_H
