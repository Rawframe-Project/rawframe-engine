// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul Tween API: the library version, the export and
// attribute macros, the result codes every fallible function returns,
// the allocator a context takes, and the four-channel values and their
// kinds that targets and tracks hold.

#ifndef MAUL_TWEEN_BASE_H
#define MAUL_TWEEN_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here, and release-please
// moves it between the markers.
// x-release-please-start-major
#define MTWN_VERSION_MAJOR 0
// x-release-please-end
// x-release-please-start-minor
#define MTWN_VERSION_MINOR 12
// x-release-please-end
// x-release-please-start-patch
#define MTWN_VERSION_PATCH 0
// x-release-please-end

// The binary interface the headers describe, which the context def carries:
// the whole version before 1.0, where any release may break it, and the major
// and minor version from 1.0 on. The top bit is always set, so a zeroed value,
// or a small one written by hand, never matches.
#define MTWN_ABI_VERSION                                                                           \
    (UINT32_C(0x80000000) | ((uint32_t)MTWN_VERSION_MAJOR << 16) |                                 \
     (MTWN_VERSION_MAJOR == 0                                                                      \
          ? (((uint32_t)MTWN_VERSION_MINOR << 8) | (uint32_t)MTWN_VERSION_PATCH)                   \
          : (uint32_t)MTWN_VERSION_MINOR))
#if MTWN_VERSION_MAJOR > 0x7FFF || MTWN_VERSION_MINOR > 0xFFFF
#error "MTWN_VERSION_MAJOR or MTWN_VERSION_MINOR does not fit MTWN_ABI_VERSION"
#endif
#if MTWN_VERSION_MAJOR == 0 && (MTWN_VERSION_MINOR > 0xFF || MTWN_VERSION_PATCH > 0xFF)
#error "before 1.0, MTWN_VERSION_MINOR and MTWN_VERSION_PATCH must fit 8 bits each"
#endif

// MTWN_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_tween_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_TWEEN_SHARED) && defined(_WIN32)
#if defined(maul_tween_EXPORTS)
#define MTWN_API __declspec(dllexport) extern
#else
#define MTWN_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_TWEEN_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MTWN_API __attribute__((visibility("default"))) extern
#else
#define MTWN_API extern
#endif

// MTWN_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC keeps __cplusplus at
// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MTWN_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MTWN_NODISCARD [[nodiscard]]
#else
#define MTWN_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t mtwnResult;
    enum
    {
        // The call did what was asked.
        mtwn_success = 0,
        // An argument a correct program never passes: a null pointer where
        // one is required, a value outside its documented range, a def not
        // made by its default function.
        mtwn_errorInvalid = -1,
        // Storage is too small for the result: a pool or a room the def
        // sized, a caller's buffer, or the allocator. More room would
        // succeed.
        mtwn_errorCapacity = -2,
        // An id names an object that has been destroyed since it was
        // handed out.
        mtwn_errorStale = -3,
        // The call is not allowed in the object's current state, which only
        // another call changes: a clip not sealed or already sealed, a
        // player at a rate or a time the control cannot start from.
        mtwn_errorState = -6,
        // A def was built against headers of another release before 1.0,
        // or another major or minor version from 1.0 on.
        mtwn_errorVersion = -7,
        // An argument passes a bound published as a named constant
        // (MTWN_MAX_* or MTWN_*_LIMIT). More room would not help.
        mtwn_errorLimit = -9,
        // The object is in use by work under way, and the same call
        // succeeds once that work ends: a context inside the host's code it
        // runs (a call back in), a clip a player still holds.
        mtwn_errorBusy = -10,
    };

    // A version: major, minor and patch.
    typedef struct mtwnVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } mtwnVersion;

    // The allocator a context takes in its def and keeps for its lifetime.
    // Alignment is a power of two. A zeroed allocator means the C library's
    // functions, which serve alignments up to that of max_align_t. Its
    // functions call no function of the context they serve.
    typedef struct mtwnAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } mtwnAllocator;

    // A value of up to four channels, binary32: what a target holds. Its
    // kind says which channels it uses and what they mean.
    typedef struct mtwnVec4
    {
        float x;
        float y;
        float z;
        float w;
    } mtwnVec4;

    // What a value is, a target's or a track's: the channels it uses and how
    // it is interpolated and composed.
    typedef uint8_t mtwnValueKind;
    enum
    {
        // One number, in x.
        mtwn_valueScalar = 0,
        // Two numbers, x and y.
        mtwn_valueVec2 = 1,
        // Three numbers, x to z.
        mtwn_valueVec3 = 2,
        // Four numbers.
        mtwn_valueVec4 = 3,
        // A color: red, green, blue and alpha in x to w, sRGB as CSS gives
        // it, interpolated in the space a transition or track names.
        mtwn_valueColor = 4,
        // An angle in radians, in x.
        mtwn_valueAngle = 5,
        // A rotation as a quaternion: x, y, z, then w.
        mtwn_valueRotation = 6,
        // An integer, in x: interpolations round to nearest.
        mtwn_valueInteger = 7,
        // A discrete value the host indexes, in x: it steps at progress 1/2.
        mtwn_valueDiscrete = 8,
        // A scale, in x, above 0: moved in its logarithm, so equal steps
        // multiply it by equal ratios.
        mtwn_valueScale = 9,
    };

    /// Returns the version of the library that was linked, which may differ
    /// from the MTWN_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_API mtwnVersion mtwnGetVersion(void);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "mtwn_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MTWN_API const char* mtwnGetResultName(mtwnResult result);

#ifdef __cplusplus
}
#endif

#endif
