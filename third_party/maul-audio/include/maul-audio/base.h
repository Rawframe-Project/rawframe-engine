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

// The library version. CMake reads it from here, and release-please
// writes it at each release.
// x-release-please-start-major
#define MAUD_VERSION_MAJOR 0
// x-release-please-end
// x-release-please-start-minor
#define MAUD_VERSION_MINOR 4
// x-release-please-end
// x-release-please-start-patch
#define MAUD_VERSION_PATCH 0
// x-release-please-end

// The binary interface a program is built for. Each root def (one whose
// creation takes no other library object) carries it, stamped by the def's
// default from the headers the program was built with, and its creation
// refuses another with maud_errorVersion. The number names the releases
// that share an interface: from 1.0 on one major and minor version; before
// 1.0, when any release may break it, the minor and the patch too. The
// top bit is always set, so a zeroed or small hand-written value never
// matches.
#define MAUD_ABI_VERSION                                                                           \
    (UINT32_C(0x80000000) | ((uint32_t)MAUD_VERSION_MAJOR << 16) |                                 \
     (MAUD_VERSION_MAJOR == 0                                                                      \
          ? (((uint32_t)MAUD_VERSION_MINOR << 8) | (uint32_t)MAUD_VERSION_PATCH)                   \
          : (uint32_t)MAUD_VERSION_MINOR))
#if MAUD_VERSION_MAJOR > 0x7FFF || MAUD_VERSION_MINOR > 0xFFFF
#error "MAUD_VERSION_MAJOR or MAUD_VERSION_MINOR does not fit MAUD_ABI_VERSION"
#endif
#if MAUD_VERSION_MAJOR == 0 && (MAUD_VERSION_MINOR > 0xFF || MAUD_VERSION_PATCH > 0xFF)
#error "before 1.0, MAUD_VERSION_MINOR and MAUD_VERSION_PATCH must fit 8 bits each"
#endif

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
        // value out of range that no status below names.
        maud_errorInvalid = -1,
        // A caller buffer, a named limit or the allocator is too small for
        // the result.
        maud_errorCapacity = -2,
        // An id names an object that no longer exists.
        maud_errorStale = -3,
        // This platform, this device or this build cannot do what was
        // asked, or the object was made without the feature the call needs
        // (a spatializer without reflections): no later call can add it.
        maud_errorUnsupported = -4,
        // The platform failed a call the library made.
        maud_errorPlatform = -5,
        // The call is not allowed in the object's current state, which only
        // another call of the program changes.
        maud_errorState = -6,
        // A root def was built from headers of another binary interface
        // than the library's (MAUD_ABI_VERSION), or a file is of another
        // version of its format.
        maud_errorVersion = -7,
        // Stored bytes are malformed: an HRTF set or a bake that fails a
        // check of its format.
        maud_errorFormat = -8,
        // An argument passes a bound published as a named constant
        // (MAUD_MAX_PERIOD_FRAMES, for one). More room would not help.
        maud_errorLimit = -9,
        // The object is running host code (a stream's callback, an
        // allocator, a hook) or another thread is rendering the stream:
        // the same call succeeds once that work ends.
        maud_errorBusy = -10,
    };

    // The allocator an owner object takes in its def and keeps for its
    // lifetime. Alignment is a power of two. A zeroed allocator means the C
    // library's allocation functions. It calls no function of the object
    // it serves: the context and the spatializer refuse such a call with
    // maud_errorBusy and count it as misuse, and every other object calls
    // its allocator only while it is made or destroyed, when no handle to
    // it is valid.
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
    MAUD_API const char* maudGetResultName(maudResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_BASE_H
