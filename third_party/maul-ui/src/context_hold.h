// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A context held by a call of another object that reads it, the text
// component's calls given a muiTextHost: host code that call runs (the
// service's allocator, FreeType's memory through it, the clipboard
// writer) may read the context but not change or destroy it, as host code
// the context runs itself may not. Such a change answers mui_errorBusy, a
// destroy does nothing, and each counts one misuse on the context.

#ifndef MAUL_UI_SRC_CONTEXT_HOLD_H
#define MAUL_UI_SRC_CONTEXT_HOLD_H

#include "maul-ui/context.h"

#include <stdbool.h>

// Holds a context, NULL being left alone; what muiReleaseContext is given
// back. A context busy already, as while layout runs muiMeasureText,
// stays busy until its own call ends.
bool muiHoldContext(const muiContext* context);

// Ends what muiHoldContext began.
void muiReleaseContext(const muiContext* context, bool held);

#endif
