// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The style pass: the first stage of the pipeline, which resolves the
// values of every node below root whose style was requested.

#ifndef MAUL_UI_SRC_RESTYLE_H
#define MAUL_UI_SRC_RESTYLE_H

#include "context.h"
#include "theme_store.h"

#include <stdint.h>

// Resolves the requested nodes below root, starts the transitions their
// changes take at nowNs, marks layout on those whose values changed, and
// clears the style flags of every node it reaches.
void muiRestyle(muiContext* context, uint32_t root, uint64_t nowNs);

// The themes a node reads tokens through, nearest first, from the scopes
// the last style pass found; a theme gone since is passed by.
muiThemeScope muiScopeOf(const muiContext* context, uint32_t slot);

#endif // MAUL_UI_SRC_RESTYLE_H
