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

// The library version. CMake reads it from here.
#define MWIN_VERSION_MAJOR 0
#define MWIN_VERSION_MINOR 11
#define MWIN_VERSION_PATCH 0

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
        // An argument is invalid: a null pointer where one is required, a
        // value out of range, text that is not UTF-8.
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
        // The call is not allowed now: before the context exists, or from
        // inside a function the library itself is running.
        mwin_errorState = -6,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions.
    typedef struct mwinAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } mwinAllocator;

    // Ids name what a context owns (family record 0016): a 1-based slot,
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

    // A request in flight (family record 0018).
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
