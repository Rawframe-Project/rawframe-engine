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

// The library version. CMake reads it from here.
#define MUI_VERSION_MAJOR 0
#define MUI_VERSION_MINOR 1
#define MUI_VERSION_PATCH 0

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
        // An argument is invalid: a null pointer where one is required, a
        // value out of range.
        mui_errorInvalid = -1,
        // A caller buffer or a named limit is too small for the result.
        mui_errorCapacity = -2,
        // An id names an object that no longer exists.
        mui_errorStale = -3,
        // Data is damaged, or in a format the call does not take, such as
        // a font that fails validation or a compressed web font.
        mui_errorFormat = -4,
        // The platform refused: a call of its failed, or its state does
        // not allow the call, such as a thread outside the apartment the
        // platform requires.
        mui_errorPlatform = -5,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions.
    typedef struct muiAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } muiAllocator;

    // Ids name what a context owns (family record 0016): a 1-based slot,
    // 0 for the null id, and a generation that tells a live object from
    // the earlier occupants of its slot.
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
    MUI_API const char* muiResultName(muiResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_BASE_H
