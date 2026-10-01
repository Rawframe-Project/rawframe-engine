// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// FreeType's entry header as Maul UI builds it (record mui-0006): found
// before FreeType's own, as FreeType's documentation of ftoption.h
// describes, it names Maul UI's options and module list for FreeType's
// sources and the text component alike.
#ifndef FT2BUILD_H_
#define FT2BUILD_H_

#define FT_CONFIG_OPTIONS_H <maul-ui-ftoption.h>
#define FT_CONFIG_MODULES_H <maul-ui-ftmodule.h>

#include <freetype/config/ftheader.h>

#endif // FT2BUILD_H_
