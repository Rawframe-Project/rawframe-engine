// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 accessibility: a UI Automation client asking a window for its
// root (WM_GETOBJECT with UiaRootObjectId) gets the program's provider,
// through uiautomationcore.dll, loaded at the first need. It stays loaded:
// UI Automation's own objects may outlive the context. A window that
// answered tells UI Automation, as it goes, to let go of what it kept.

#ifndef MAUL_WINDOW_SRC_WIN32_ACCESSIBILITY_H
#define MAUL_WINDOW_SRC_WIN32_ACCESSIBILITY_H

#include "win32.h"

// Answers WM_GETOBJECT: true with the result in *result when the root
// was handed over.
bool mwinWin32AnswerObject(mwinWin32Window* window, WPARAM wParam, LPARAM lParam, LRESULT* result);

// Lets UI Automation go of what it kept of a window that answered.
void mwinWin32ForgetObject(const mwinWin32Window* window);

#endif // MAUL_WINDOW_SRC_WIN32_ACCESSIBILITY_H
