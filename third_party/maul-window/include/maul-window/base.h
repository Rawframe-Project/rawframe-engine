// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul Window API: the library version, the export and
// attribute macros, and the result codes every fallible function
// returns.

#ifndef MAUL_WINDOW_BASE_H
#define MAUL_WINDOW_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here, and release-please
// moves it between the markers.
// x-release-please-start-major
#define MWIN_VERSION_MAJOR 0
// x-release-please-end
// x-release-please-start-minor
#define MWIN_VERSION_MINOR 6
// x-release-please-end
// x-release-please-start-patch
#define MWIN_VERSION_PATCH 0
// x-release-please-end

// The binary interface a program is built for: the whole version before
// 1.0, when any release may break it, and the major and minor version from
// 1.0 on. The top bit is always set, so a zeroed def never matches. The
// root defs carry it, those of the calls a program can make first: the
// application def (mwinDefaultAppDef) and the message box def
// (mwinDefaultMessageBoxDef). mwinRun and mwinShowMessageBox refuse a def
// built for another; every other def is used inside a run.
#define MWIN_ABI_VERSION                                                                           \
    (UINT32_C(0x80000000) | ((uint32_t)MWIN_VERSION_MAJOR << 16) |                                 \
     (MWIN_VERSION_MAJOR == 0                                                                      \
          ? (((uint32_t)MWIN_VERSION_MINOR << 8) | (uint32_t)MWIN_VERSION_PATCH)                   \
          : (uint32_t)MWIN_VERSION_MINOR))
#if MWIN_VERSION_MAJOR > 0x7FFF || MWIN_VERSION_MINOR > 0xFFFF
#error "MWIN_VERSION_MAJOR or MWIN_VERSION_MINOR does not fit MWIN_ABI_VERSION"
#endif
#if MWIN_VERSION_MAJOR == 0 && (MWIN_VERSION_MINOR > 0xFF || MWIN_VERSION_PATCH > 0xFF)
#error "before 1.0, MWIN_VERSION_MINOR and MWIN_VERSION_PATCH must fit 8 bits each"
#endif

// MWIN_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_window_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_WINDOW_SHARED) && defined(_WIN32)
#if defined(maul_window_EXPORTS)
#define MWIN_API __declspec(dllexport) extern
#else
#define MWIN_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_WINDOW_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MWIN_API __attribute__((visibility("default"))) extern
#else
#define MWIN_API extern
#endif

// MWIN_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC keeps __cplusplus at
// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MWIN_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MWIN_NODISCARD [[nodiscard]]
#else
#define MWIN_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t mwinResult;

    enum
    {
        // The call did what was asked.
        mwin_success = 0,
        // There is nothing to return: the event stream is drained.
        mwin_empty = 1,
        // An argument a correct program never passes: a null pointer where
        // one is required, a value outside its parameter's documented
        // range, text that is not UTF-8. A bound with a named constant is
        // mwin_errorLimit.
        mwin_errorInvalid = -1,
        // A caller buffer, a named limit or the allocator is too small for
        // the result.
        mwin_errorCapacity = -2,
        // An id names an object that no longer exists.
        mwin_errorStale = -3,
        // This platform or this build cannot do what was asked.
        mwin_errorUnsupported = -4,
        // The platform failed a call the library made.
        mwin_errorPlatform = -5,
        // The call is not allowed in the object's current state, which
        // the program sees change only through its own calls, as through
        // the events it reads.
        mwin_errorState = -6,
        // A def was built from headers of another binary interface than
        // the library's (MWIN_ABI_VERSION).
        mwin_errorVersion = -7,
        // An argument, or its content, passes a fixed bound of the
        // library, published as a named constant (MWIN_*): more room in
        // mwinLimits would not help.
        mwin_errorLimit = -9,
        // The context is inside the program's allocator, which calls no
        // function that changes it; the same call succeeds once the
        // allocator returns.
        mwin_errorBusy = -10,
    };

    // A yes or no the platform may not know.
    typedef uint8_t mwinTristate;

    enum
    {
        mwin_unknown = 0,
        mwin_no = 1,
        mwin_yes = 2,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions. It calls no function that changes
    // the object it serves: such a call returns mwin_errorBusy.
    typedef struct mwinAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } mwinAllocator;

    // Ids name what a context owns: a 1-based slot,
    // 0 for the null id, and a generation that tells a live object from
    // the earlier occupants of its slot. One type per kind, so a window id
    // cannot be passed where a request id belongs.
    typedef struct mwinWindowId
    {
        uint32_t index1;
        uint32_t generation;
    } mwinWindowId;

    // A monitor, stable while it stays connected; a monitor connected again
    // gets a new id.
    typedef struct mwinMonitorId
    {
        uint32_t index1;
        uint32_t generation;
    } mwinMonitorId;

    // A request in flight.
    typedef struct mwinRequestId
    {
        uint32_t index1;
        uint32_t generation;
    } mwinRequestId;

    // A library version: major, minor and patch.
    typedef struct mwinVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } mwinVersion;

    /// Returns the version of the library that was linked, which may differ
    /// from the MWIN_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinVersion mwinGetVersion(void);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "mwin_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API const char* mwinResultName(mwinResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_BASE_H
