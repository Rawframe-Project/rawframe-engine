// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul Nav API: the library version, the export and
// attribute macros, the result codes every fallible function returns,
// the allocator and the vector types.
//
// Coordinates are meters in a right-handed frame with +Y up and -Z
// forward. World positions are doubles; positions within a frame the
// library names (a bake's origin, a tile) are binary32.

#ifndef MAUL_NAV_BASE_H
#define MAUL_NAV_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here.
#define MNAV_VERSION_MAJOR 1
#define MNAV_VERSION_MINOR 0
#define MNAV_VERSION_PATCH 1

// The version every def and the query filter carry, stamped by their
// defaults from the headers a program was built with: the ABI is promised
// within a minor version only, and every function that takes a def or a
// filter refuses one stamped for another major or minor with
// mnav_errorVersion before it reads anything else of it. The cookie and
// this stay the first two fields of every def in every version.
#define MNAV_ABI_VERSION (((uint32_t)MNAV_VERSION_MAJOR << 16) | (uint32_t)MNAV_VERSION_MINOR)

// MNAV_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_nav_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_NAV_SHARED) && defined(_WIN32)
#if defined(maul_nav_EXPORTS)
#define MNAV_API __declspec(dllexport) extern
#else
#define MNAV_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_NAV_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MNAV_API __attribute__((visibility("default"))) extern
#else
#define MNAV_API extern
#endif

// MNAV_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects; MSVC states its C++ dialect in
// _MSVC_LANG, not __cplusplus.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MNAV_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MNAV_NODISCARD [[nodiscard]]
#else
#define MNAV_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t mnavResult;

    enum
    {
        // The call did what was asked.
        mnav_success = 0,
        // An argument is invalid: a null pointer where one is required, a
        // value out of range, data that is malformed.
        mnav_errorInvalid = -1,
        // A caller buffer is too small for the result, or the allocator
        // failed.
        mnav_errorCapacity = -2,
        // A named limit the caller set was reached: the work it bounds was
        // not done.
        mnav_errorLimit = -3,
        // A coordinate is finite but lies past what the library can
        // represent in the frame it belongs to.
        mnav_errorRange = -4,
        // Data was written in a format version this library does not
        // read, or a def was built against headers of another major or
        // minor version (MNAV_ABI_VERSION).
        mnav_errorVersion = -5,
        // The place asked about has no tile loaded: not a wall, nothing
        // is known there.
        mnav_errorNotLoaded = -6,
        // An id names a tile or polygon that has been replaced or
        // removed since it was handed out (record 0016).
        mnav_errorStale = -7,
        // The navmesh's runtime tier does not allow the change.
        mnav_errorTier = -8,
    };

    // A library version: major, minor and patch.
    typedef struct mnavVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } mnavVersion;

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's functions, which serve alignments up to that of max_align_t.
    typedef struct mnavAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } mnavAllocator;

    // A vector or a position within a frame, in meters, binary32.
    typedef struct mnavVec3
    {
        float x;
        float y;
        float z;
    } mnavVec3;

    // A world position in meters, in doubles, so that precision does not
    // depend on the distance from the world origin.
    typedef struct mnavPos3
    {
        double x;
        double y;
        double z;
    } mnavPos3;

    /// Returns the version of the library that was linked, which may differ
    /// from the MNAV_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API mnavVersion mnavGetVersion(void);

    // The seed of a hash that starts from nothing.
#define MNAV_HASH_INIT 0xCBF29CE484222325ull

    /// Hashes bytes into a 64-bit value, the hash every determinism check
    /// and navmesh fingerprint is built on: eight bytes a round, read in the
    /// host's byte order, xored in, multiplied by an odd constant and
    /// folded, the leftover bytes one at a time. Its constants are frozen.
    ///
    /// @param seed       MNAV_HASH_INIT, or a hash to continue.
    /// @param data       The bytes. May be NULL when byteCount is 0.
    /// @param byteCount  The number of bytes, at least 0.
    /// @return The hash.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API uint64_t mnavHash64(uint64_t seed, const void* data, int32_t byteCount);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "mnav_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API const char* mnavResultName(mnavResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_BASE_H
