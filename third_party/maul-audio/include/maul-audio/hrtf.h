// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Head-related transfer function sets, loaded from the bytes of a
// .maudhrtf file (docs/hrtf-format.md) for the binaural renderer. The
// library reads no files: the host hands over the bytes, which the
// loader treats as hostile.

#ifndef MAUL_AUDIO_HRTF_H
#define MAUL_AUDIO_HRTF_H

#include "maul-audio/base.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A loaded HRTF set, at the rate it was loaded for.
    typedef struct maudHrtf maudHrtf;

    // How to load an HRTF set. Build it with maudDefaultHrtfDef.
    typedef struct maudHrtfDef
    {
        uint32_t cookie;
        // The file's bytes; the loader copies what it keeps, so they may
        // go once maudLoadHrtf returns.
        const void* bytes;
        size_t byteCount;
        // The rate the set is used at: the loader resamples the file's
        // responses and delays to it. 0 keeps the file's rate.
        uint32_t sampleRate;
        // Limits: the most directions, and the most taps per response
        // after resampling. A file past one is refused.
        uint32_t maxDirections;
        uint32_t maxTaps;
        maudAllocator allocator;
    } maudHrtfDef;

    // What a loaded set holds. The text is the set's own and lives as long
    // as it does; it is not terminated.
    typedef struct maudHrtfInfo
    {
        uint32_t sampleRate;
        uint32_t taps;
        uint32_t directionCount;
        uint32_t ringCount;
        // The metres from the head's centre at which the set was measured.
        float distance;
        // The dataset and subject, and its license and attribution, UTF-8.
        const char* name;
        size_t nameLength;
        const char* license;
        size_t licenseLength;
    } maudHrtfInfo;

    /// Returns the default HRTF def: no bytes, the file's own rate, at most
    /// 65,536 directions and 1,024 taps.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudHrtfDef maudDefaultHrtfDef(void);

    /// Loads an HRTF set from a .maudhrtf file's bytes, checking every
    /// count, size and the checksum before it allocates or trusts anything.
    ///
    /// @param def      The def, from maudDefaultHrtfDef.
    /// @param hrtfOut  Receives the set; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def
    ///         without its cookie or out of range, or bytes that are not a
    ///         well-formed file (a wrong magic, size, ring layout, text or
    ///         checksum); `maud_errorUnsupported` for a format version this
    ///         library does not read; `maud_errorCapacity` when the set
    ///         passes the def's limits or the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudLoadHrtf(const maudHrtfDef* def, maudHrtf** hrtfOut);

    /// Destroys an HRTF set. NULL is ignored.
    ///
    /// @param hrtf  The set.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MAUD_API void maudDestroyHrtf(maudHrtf* hrtf);

    /// Reports what an HRTF set holds.
    ///
    /// @param hrtf     The set.
    /// @param infoOut  Receives the information.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetHrtfInfo(const maudHrtf* hrtf, maudHrtfInfo* infoOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_HRTF_H
