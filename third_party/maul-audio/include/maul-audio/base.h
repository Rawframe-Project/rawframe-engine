// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul Audio API: the library version, the export and
// attribute macros, the result codes every fallible function returns,
// and the allocator owner objects take.

#ifndef MAUL_AUDIO_BASE_H
#define MAUL_AUDIO_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here.
#define MAUD_VERSION_MAJOR 0
#define MAUD_VERSION_MINOR 1
#define MAUD_VERSION_PATCH 1

// MAUD_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_audio_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_AUDIO_SHARED) && defined(_WIN32)
#if defined(maul_audio_EXPORTS)
#define MAUD_API __declspec(dllexport) extern
#else
#define MAUD_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_AUDIO_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MAUD_API __attribute__((visibility("default"))) extern
#else
#define MAUD_API extern
#endif

// MAUD_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC reports its C++ dialect in
// _MSVC_LANG, leaving __cplusplus at 199711L unless asked otherwise.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MAUD_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MAUD_NODISCARD [[nodiscard]]
#else
#define MAUD_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t maudResult;

    enum
    {
        // The call did what was asked.
        maud_success = 0,
        // There is nothing to return: a queue is drained.
        maud_empty = 1,
        // An argument is invalid: a null pointer where one is required, a
        // value out of range.
        maud_errorInvalid = -1,
        // A caller buffer, a named limit or the allocator is too small for
        // the result.
        maud_errorCapacity = -2,
        // An id names an object that no longer exists.
        maud_errorStale = -3,
        // This platform, this device or this build cannot do what was
        // asked.
        maud_errorUnsupported = -4,
        // The platform failed a call the library made.
        maud_errorPlatform = -5,
        // The call is not allowed in the object's current state.
        maud_errorState = -6,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions.
    typedef struct maudAllocator
    {
        void* (*alloc)(size_t size, size_t alignment, void* context);
        void (*free)(void* memory, size_t size, size_t alignment, void* context);
        void* context;
    } maudAllocator;

    // A library version: major, minor and patch.
    typedef struct maudVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } maudVersion;

    // A point or direction in the listener's frame, in metres: +x to the
    // right, +y up, -z straight ahead (right-handed; the convention of
    // OpenAL, Steam Audio and Resonance Audio). A host with another
    // convention converts at the call.
    typedef struct maudVector3
    {
        float x;
        float y;
        float z;
    } maudVector3;

    // A rotation as a quaternion, (x, y, z) its vector part and w its
    // scalar part; any nonzero length is normalized where one is taken.
    typedef struct maudQuaternion
    {
        float x;
        float y;
        float z;
        float w;
    } maudQuaternion;

    // A source as a panner hears it, for encoding into an ambisonic bed or
    // panning to speakers: where it is (only the direction counts; a zero
    // vector is straight ahead) and its gain.
    typedef struct maudPanSource
    {
        maudVector3 direction;
        float gain;
    } maudPanSource;

    /// Returns the version of the library that was linked, which may differ
    /// from the MAUD_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudVersion maudGetVersion(void);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "maud_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API const char* maudResultName(maudResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_BASE_H
