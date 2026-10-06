// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Finding font families by key and matching their faces (record
// mui-0006).

#ifndef MAUL_UI_SRC_FONT_FAMILY_H
#define MAUL_UI_SRC_FONT_FAMILY_H

#include "family_store.h"
#include "font_store.h"
#include "text_service.h"

#include "maul-ui/text_style.h"

// The family a key names; NULL for a key that names none, as a font's
// does.
const muiFontFamily* muiFindFamily(const muiTextService* service, uint64_t key);

// The face of a family a weight and slant choose, and its id; NULL when
// every face is gone.
const muiFont* muiMatchFamily(const muiTextService* service, const muiFontFamily* family,
                              float weight, muiFontSlant slant, muiFontId* faceOut);

#endif // MAUL_UI_SRC_FONT_FAMILY_H
