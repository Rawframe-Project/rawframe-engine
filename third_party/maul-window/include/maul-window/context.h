// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and the run function. A program describes itself in an
// application def (three functions and the context's def) and hands it
// to mwinRun, which creates the context, calls init, calls frame once
// per frame, calls quit and destroys the context. On Win32, X11 and
// Wayland mwinRun pumps the platform itself; on the web and on mobile
// platforms the platform's loop drives it. The program cannot tell the
// two apart.
//
// Every limit on memory and work is named in mwinLimits, with a default
// a program may change. Everything the context needs is allocated when
// it is created, through the def's allocator.

#ifndef MAUL_WINDOW_CONTEXT_H
#define MAUL_WINDOW_CONTEXT_H

#include "maul-window/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The owner of every window, queue and request: created by mwinRun
    // and passed to the application's functions.
    typedef struct mwinContext mwinContext;

    // Which backend a context talks to.
    typedef uint8_t mwinBackendKind;

    enum
    {
        // The platform's own window system.
        mwin_backendNative = 0,
        // A headless backend for tests: it applies requests at the next
        // pump and takes platform records from mwinTest functions. It
        // exists only in builds with MAUL_WINDOW_TEST_BACKEND.
        mwin_backendTest = 1,
    };

    // The named limits of a context. A request past one is refused with
    // mwin_errorCapacity.
    typedef struct mwinLimits
    {
        // Windows that exist at once.
        uint16_t windows;
        // Requests in flight per window.
        uint16_t requestsPerWindow;
        // Notifications waiting per window; for the context's own ones
        // (monitors, lifecycle, system facts), waiting in all.
        uint16_t notificationsPerWindow;
        // Bytes of a window title.
        uint16_t titleBytes;
        // Input records waiting per window and class (discrete, motion,
        // raw deltas, wheel).
        uint16_t inputPerWindow;
        // Bytes of text waiting per window.
        uint16_t textBytesPerWindow;
        // Monitors connected at once.
        uint16_t monitors;
        // Bytes of the preferred locales list.
        uint16_t localeBytes;
        // Gamepads connected at once; 0 for none.
        uint16_t gamepads;
        // Bytes of clipboard text, written or read.
        uint32_t clipboardBytes;
        // Bytes of a drop's paths, and of its text.
        uint32_t dropBytes;
        // Files a drop delivers.
        uint16_t droppedFiles;
        // Files a dialog chooses, and the bytes of their paths.
        uint16_t dialogFiles;
        uint32_t dialogBytes;
    } mwinLimits;

    // How a context is made. Build it with mwinDefaultContextDef.
    typedef struct mwinContextDef
    {
        uint32_t cookie;
        mwinAllocator allocator;
        mwinLimits limits;
        mwinBackendKind backend;
    } mwinContextDef;

    // What a frame asks of the loop.
    typedef uint8_t mwinFrameResult;

    enum
    {
        mwin_frameContinue = 0,
        mwin_frameStop = 1,
    };

    // A program as mwinRun sees it. Build it with mwinDefaultAppDef.
    typedef struct mwinAppDef
    {
        uint32_t cookie;
        mwinContextDef context;
        // Called once, when the platform allows windows. A status other
        // than mwin_success skips the frames and goes to quit.
        mwinResult (*init)(mwinContext* context, void* user);
        // Called once per frame. It drains the event stream.
        mwinFrameResult (*frame)(mwinContext* context, void* user);
        // Called once at the end, with init's failure or mwin_success;
        // the context and its windows still exist. May be NULL.
        void (*quit)(mwinContext* context, mwinResult status, void* user);
        void* user;
    } mwinAppDef;

    /// Returns the default context def: the default limits (8 windows; per
    /// window 32 requests, 256 notifications, 256 input records per class
    /// and 4,096 bytes of text; 1,024 title bytes; 16 monitors; 256 bytes
    /// of locales; 8 gamepads; 1 MiB of clipboard text; 256 files and
    /// 1 MiB of paths or text per drop), the C library's allocator and the
    /// native backend.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinContextDef mwinDefaultContextDef(void);

    /// Returns the default application def: the default context def and no
    /// functions.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinAppDef mwinDefaultAppDef(void);

    /// Runs a program: creates the context, calls init, then frame until a
    /// frame returns mwin_frameStop, then quit, and destroys the context.
    /// Where the platform owns the loop (the web, iOS) this function may
    /// never return, or, on the web without Emscripten, return
    /// `mwin_success` once init has succeeded while the page's frames run
    /// the program on; either way put cleanup in quit. On iOS init runs
    /// when the application's first scene connects, and a program that
    /// stops or fails there ends with quit while the application runs on.
    /// On Android the platform begins the run, not the program: see
    /// mwinAndroidMain; there this function returns
    /// `mwin_errorUnsupported`. It is never called from inside a running
    /// program's functions.
    ///
    /// @param def  The program: a valid cookie, init and frame set.
    /// @return init's status when it failed; `mwin_success` after a stop;
    ///         `mwin_errorCapacity` when the allocator cannot give the
    ///         context its memory; `mwin_errorUnsupported` for a backend
    ///         this build lacks; `mwin_errorPlatform` when the window
    ///         system cannot be reached; `mwin_errorInvalid` for a missing
    ///         function, a bad cookie or a zero limit.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRun(const mwinAppDef* def);

#ifdef __ANDROID__
    /// Defined by the program on Android, in place of the `main` that calls
    /// mwinRun elsewhere: returns the program, which the library then runs
    /// as mwinRun would. The library's activity (`maul.window.Activity`,
    /// named in the manifest with the program's library as
    /// `android.app.lib_name`) calls it when it is created and no program
    /// runs; init runs at once, and frames follow on the main thread while
    /// the activity is started. The program outlives its activity: one the
    /// system creates anew (a configuration change, the user coming back)
    /// joins the running program, the window's surface going with the old
    /// one and coming with the new. A program that stops or fails ends
    /// with quit and finishes its activity; a later activity calls this
    /// function anew, in the same process, so the program keeps nothing
    /// it needs fresh in statics. A shared build of this library finds the
    /// function in the program's library, which must export it.
    ///
    /// @return The program, as mwinRun takes it.
    /// @par Thread safety
    /// Called on the main thread.
    mwinAppDef mwinAndroidMain(void);
#endif

    /// Returns how many calls the context has refused as invalid input
    /// (`mwin_errorInvalid`): a count release builds can watch to catch a
    /// program's bugs. Stale ids are not misuse.
    ///
    /// @param context  The context.
    /// @return The count; 0 for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_API uint64_t mwinGetContextMisuse(const mwinContext* context);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_CONTEXT_H
