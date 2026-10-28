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

// The library version. CMake reads it from here, and release-please
// moves it between the markers.
// x-release-please-start-major
#define MNAV_VERSION_MAJOR 0
// x-release-please-end
// x-release-please-start-minor
#define MNAV_VERSION_MINOR 2
// x-release-please-end
// x-release-please-start-patch
#define MNAV_VERSION_PATCH 0
// x-release-please-end

// The binary interface a program is built for: the whole version before
// 1.0, when any release may break it, and the major and minor version from
// 1.0 on. The top bit is always set, so a zeroed def never matches. Maul Nav
// has no single root object, so every def of a creation that takes no other
// library object carries it, stamped by its default from the headers the
// program includes, and that creation refuses a def built for another
// version with mnav_errorVersion, after its cookie and before anything
// else. The query filter and the link generation def are only ever used
// with an object such a creation made, and carry only their cookie.
#define MNAV_ABI_VERSION                                                                           \
    (UINT32_C(0x80000000) | ((uint32_t)MNAV_VERSION_MAJOR << 16) |                                 \
     (MNAV_VERSION_MAJOR == 0                                                                      \
          ? (((uint32_t)MNAV_VERSION_MINOR << 8) | (uint32_t)MNAV_VERSION_PATCH)                   \
          : (uint32_t)MNAV_VERSION_MINOR))
#if MNAV_VERSION_MAJOR > 0x7FFF || MNAV_VERSION_MINOR > 0xFFFF
#error "MNAV_VERSION_MAJOR or MNAV_VERSION_MINOR does not fit MNAV_ABI_VERSION"
#endif
#if MNAV_VERSION_MAJOR == 0 && (MNAV_VERSION_MINOR > 0xFF || MNAV_VERSION_PATCH > 0xFF)
#error "before 1.0, MNAV_VERSION_MINOR and MNAV_VERSION_PATCH must fit 8 bits each"
#endif

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

    // The errors the Maul family shares keep the family's values
    // (docs/internals/maul-conventions.md); the library's own errors are
    // numbered from -32 down.
    enum
    {
        // The call did what was asked.
        mnav_success = 0,
        // An argument a correct program never passes: a null pointer where
        // one is required, a NaN, a value (an infinity included) outside a
        // range its documentation gives with no named constant, a
        // combination the call does not take. One misuse is counted on the
        // first owner object the call is given, unless a bake's report names
        // the input element refused.
        mnav_errorInvalid = -1,
        // Room is too small for the result: a caller's buffer, the
        // allocator, or a limit the caller set (a def's limits and memory,
        // a query's nodes). More room would succeed.
        mnav_errorCapacity = -2,
        // An id names a tile, polygon or link that has been replaced or
        // removed since it was handed out.
        mnav_errorStale = -3,
        // The navmesh was made without the feature the call needs: its
        // runtime tier, chosen when it was made, does not allow the change.
        // No later call adds it.
        mnav_errorUnsupported = -4,
        // The call is not allowed in the object's current state, which only
        // another call of the program changes: work begun and not ended,
        // a link not yet committed, or an object built on a navmesh or a
        // flight volume that has been committed since.
        mnav_errorState = -6,
        // A def was built against headers of another library version
        // (MNAV_ABI_VERSION), or stored bytes are of a format version this
        // library does not read.
        mnav_errorVersion = -7,
        // Stored bytes (a tile, a flight tile) are malformed: every check a
        // reader makes on them fails with this, or with mnav_errorVersion.
        mnav_errorFormat = -8,
        // An argument, or its content, passes a fixed bound the library
        // publishes as a named constant (MNAV_MAX_* or MNAV_MIN_*), an
        // infinity included: a coordinate past the extent, a count past its
        // most. More room would not help.
        mnav_errorLimit = -9,
        // The object is in use by host code the library is inside (its
        // allocator, a link generation's clearance test), and the call came
        // back in from there; one misuse is counted. The same call succeeds
        // once that code returns.
        mnav_errorBusy = -10,
        // The place asked about has no tile loaded: not a wall, nothing
        // is known there.
        mnav_errorNotLoaded = -32,
        // The place asked about is outside what the object covers: a point
        // off the polygon given, a cell or polygon outside a flow field's
        // region, a point below or above a flight volume. A correct program
        // meets it; it counts no misuse.
        mnav_errorOutside = -33,
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
    // Its calls run in the host's own floating-point environment. They may call
    // nothing of the object they serve, or of another object the running call
    // reads or writes: a call that would change the object they serve is
    // refused with mnav_errorBusy, one misuse counted.
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
