// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's Xbox gamepads through Windows.Gaming.Input (mwin-0023): every
// Xbox-compatible pad, with no limit of four, its name, USB ids and
// battery, and its motors. The runtime is found through combase.dll
// when a context starts; where it is missing, XInput reads the pads
// (win32_pad.h). The runtime's interfaces are declared in the source
// rather than taken from the SDK's headers, whose C declarations differ
// between SDK versions, and only the methods called are named. The
// runtime says when a pad comes or goes on a thread of its own; the
// pads are read on the main thread, at each pump, by the pad tracker
// (pad_tracker.h), whose table of functions it fills.

#ifndef MAUL_WINDOW_SRC_WIN32_WGI_H
#define MAUL_WINDOW_SRC_WIN32_WGI_H

#include "core.h"
#include "pad_tracker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef struct mwinWgiHandler mwinWgiHandler;

typedef struct mwinWgi
{
    HMODULE combase;
    // The runtime's functions, from combase.dll.
    HRESULT(WINAPI* activate)(void* classId, REFIID iid, void** factory);
    HRESULT(WINAPI* makeString)(const WCHAR* text, UINT32 length, void** string);
    HRESULT(WINAPI* deleteString)(void* string);
    const WCHAR*(WINAPI* stringText)(void* string, UINT32* length);
    // The gamepads' and raw controllers' statics, and the handler told
    // of arrivals and removals with its registrations for each.
    void* gamepads;
    void* controllers;
    mwinWgiHandler* handler;
    int64_t tokens[2];
    bool listening[2];
    // Whether this context initialized COM on its thread.
    bool com;
} mwinWgi;

// Finds the runtime and fills the tracker's table with it; false, with
// nothing held, where there is none.
bool mwinWgiStart(mwinWgi* wgi, mwinPadRuntime* runtime);
void mwinWgiStop(mwinWgi* wgi);

#endif // MAUL_WINDOW_SRC_WIN32_WGI_H
