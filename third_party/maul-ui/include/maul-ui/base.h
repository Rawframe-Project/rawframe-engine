// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul UI API: the library version, the export and
// attribute macros, and the result codes every fallible function
// returns.

#ifndef MAUL_UI_BASE_H
#define MAUL_UI_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here, and release-please
// moves it between the markers.
// x-release-please-start-major
#define MUI_VERSION_MAJOR 0
// x-release-please-end
// x-release-please-start-minor
#define MUI_VERSION_MINOR 4
// x-release-please-end
// x-release-please-start-patch
#define MUI_VERSION_PATCH 0
// x-release-please-end

// The binary interface a program is built for: the whole version before
// 1.0, when any release may break it, and the major and minor version from
// 1.0 on. The top bit is always set, so a zeroed def never matches. The
// root defs carry it: the context's, the text service's, the access
// tree's, the AT-SPI application's, and the ARIA, Android,
// NSAccessibility, UI Automation and UIKit adapters'. Their creations
// refuse a def built for another with mui_errorVersion before they read
// the rest, whose layout that version may not share; every other def is
// used under an object one of them made.
#define MUI_ABI_VERSION                                                                            \
    (UINT32_C(0x80000000) | ((uint32_t)MUI_VERSION_MAJOR << 16) |                                  \
     (MUI_VERSION_MAJOR == 0 ? (((uint32_t)MUI_VERSION_MINOR << 8) | (uint32_t)MUI_VERSION_PATCH)  \
                             : (uint32_t)MUI_VERSION_MINOR))
#if MUI_VERSION_MAJOR > 0x7FFF || MUI_VERSION_MINOR > 0xFFFF
#error "MUI_VERSION_MAJOR or MUI_VERSION_MINOR does not fit MUI_ABI_VERSION"
#endif
#if MUI_VERSION_MAJOR == 0 && (MUI_VERSION_MINOR > 0xFF || MUI_VERSION_PATCH > 0xFF)
#error "before 1.0, MUI_VERSION_MINOR and MUI_VERSION_PATCH must fit 8 bits each"
#endif

// MUI_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_ui_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_UI_SHARED) && defined(_WIN32)
#if defined(maul_ui_EXPORTS)
#define MUI_API __declspec(dllexport) extern
#else
#define MUI_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_UI_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MUI_API __attribute__((visibility("default"))) extern
#else
#define MUI_API extern
#endif

// MUI_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC keeps __cplusplus at
// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MUI_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MUI_NODISCARD [[nodiscard]]
#else
#define MUI_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t muiResult;

    enum
    {
        // The call did what was asked.
        mui_success = 0,
        // A queue had nothing to take.
        mui_empty = 1,
        // An argument is invalid: a null pointer where one is required, an
        // object of the wrong kind, a value outside the range its parameter
        // documents. A correct program never passes one, so a live owner
        // object counts it as misuse, but for a query that takes the object
        // as const.
        mui_errorInvalid = -1,
        // A caller buffer, a named limit of a def or the allocator gives too
        // little room for the result; more room would succeed.
        mui_errorCapacity = -2,
        // An id names an object that no longer exists.
        mui_errorStale = -3,
        // The object was created without what the call needs, such as a
        // glyph atlas of another format, or the library does not take the
        // data, such as a compressed web font. No later call adds it.
        mui_errorUnsupported = -4,
        // The platform refused: a call of its failed, or its state does
        // not allow the call, such as a thread outside the apartment the
        // platform requires.
        mui_errorPlatform = -5,
        // The call is not allowed in the object's state, which only another
        // call of the program changes, such as an editor call on a text
        // block not opted into editing. It counts no misuse.
        mui_errorState = -6,
        // A root def was built against headers of a release whose binary
        // interface differs from the library's (MUI_ABI_VERSION).
        mui_errorVersion = -7,
        // Data is damaged, such as a font that fails validation, or a
        // request another program sent does not fit what it names, such as
        // a number that is not finite. Another program controls such data,
        // so it counts no misuse.
        mui_errorFormat = -8,
        // An argument, or its content, passes a fixed bound of the library
        // that a MUI_MAX_ or MUI_MIN_ constant names, such as
        // MUI_MAX_TEXT_SPANS; more room would not help. It counts as misuse,
        // as an invalid argument does.
        mui_errorLimit = -9,
        // The object is busy: host code it is running (a measure, paint or
        // access text function, the allocator, an action function, the
        // clipboard writer) called one of its functions that changes it,
        // or an event function called one it may not (muiEventFunction).
        // The same call succeeds once that code returns.
        mui_errorBusy = -10,
    };

    // The longest text the library holds, in bytes: a text block's text, an
    // accessibility text. Offsets, one past the end included, fit 32 bits.
    enum
    {
        MUI_MAX_TEXT_BYTES = 0x7FFFFFFF
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions. Its functions run inside the object's
    // calls and call no function that changes it: one answers
    // mui_errorBusy and counts one misuse.
    typedef struct muiAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } muiAllocator;

    // Ids name what a context owns: a 1-based slot, 0 for the null id, and
    // a generation that tells a live object from the earlier occupants of
    // its slot.
    typedef struct muiNodeId
    {
        uint32_t index1;
        uint32_t generation;
    } muiNodeId;

    // A library version: major, minor and patch.
    typedef struct muiVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } muiVersion;

    /// Returns the version of the library that was linked, which may differ
    /// from the MUI_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiVersion muiGetVersion(void);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "mui_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API const char* muiGetResultName(muiResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_BASE_H
