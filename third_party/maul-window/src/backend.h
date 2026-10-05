// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a backend does for the core: reach the window system, drive the
// program's loop, and make the platform's windows. A backend answers
// every request it is given through mwinComplete, now or at a later
// pump, and reports what the platform did through mwinPost.

#ifndef MAUL_WINDOW_SRC_BACKEND_H
#define MAUL_WINDOW_SRC_BACKEND_H

#include "maul-window/input.h"
#include "maul-window/native.h"

typedef struct mwinBackendOps
{
    // Reaches the window system.
    mwinResult (*start)(mwinContext* context);
    void (*stop)(mwinContext* context);
    // Calls the program's init, frames and quit (see mwinRunLoop).
    mwinResult (*run)(mwinContext* context);
    // Makes the platform window of a new slot, from its def.
    void (*createWindow)(mwinContext* context, uint32_t slot);
    // Destroys the platform window of a slot, which the core already
    // considers gone.
    void (*destroyWindow)(mwinContext* context, uint32_t slot);
    // Takes a new request of the window in a slot.
    void (*submit)(mwinContext* context, uint32_t slot, uint32_t request);
    // Nanoseconds on the clock the platform stamps its events with.
    uint64_t (*now)(const mwinContext* context);
    // What a physical key means under the current layout.
    mwinKey (*mapKeyCode)(const mwinContext* context, mwinKeyCode code);
    // The current layout's name, as mwinGetKeyboardLayout reports it.
    mwinResult (*keyboardLayout)(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut);
    // The handles of the surface of the window in a slot, which has one.
    void (*nativeHandles)(const mwinContext* context, uint32_t slot, mwinNativeHandles* out);
    // Runs the motors of the gamepad in a slot, which has mwin_padRumble.
    mwinResult (*rumble)(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs);
} mwinBackendOps;

// The loop of a backend that pumps: init, then pump and frame until a
// frame stops or init fails, then quit. Returns init's status.
mwinResult mwinRunLoop(mwinContext* context, void (*pump)(mwinContext* context));

// The same loop in steps, for a platform that owns the loop and calls
// the backend once a frame. Start calls init: true when frames follow.
// Step pumps and runs a frame: false once the program stops; a step
// taken while init, a frame or quit runs does nothing. End calls quit
// and returns init's status.
bool mwinStartProgram(mwinContext* context);
bool mwinStepProgram(mwinContext* context, void (*pump)(mwinContext* context));
mwinResult mwinEndProgram(mwinContext* context);

// Runs a program as mwinRun does, for a platform that begins the run
// itself, with what it handed over for the backend's start (the
// context's launch).
mwinResult mwinRunLaunched(const mwinAppDef* def, void* launch);

// Stops the backend and frees the context, after mwinEndProgram, where
// mwinRun could not wait for the program to end.
void mwinFinishRun(mwinContext* context);

// The test backend, in builds with MAUL_WINDOW_TEST_BACKEND.
extern const mwinBackendOps mwinTestBackend;

// The Wayland, X11 and Win32 backends, in builds with
// MAUL_WINDOW_WAYLAND, MAUL_WINDOW_X11 and MAUL_WINDOW_WIN32.
extern const mwinBackendOps mwinWaylandBackend;
extern const mwinBackendOps mwinX11Backend;
extern const mwinBackendOps mwinWin32Backend;

// The macOS backend, in builds with MAUL_WINDOW_MACOS.
extern const mwinBackendOps mwinMacBackend;

// The iOS backend, in builds with MAUL_WINDOW_IOS.
extern const mwinBackendOps mwinIOSBackend;

// The Android backend, in builds with MAUL_WINDOW_ANDROID.
extern const mwinBackendOps mwinAndroidBackend;

// The web backend, in builds with MAUL_WINDOW_WEB.
extern const mwinBackendOps mwinWebBackend;

#endif // MAUL_WINDOW_SRC_BACKEND_H
