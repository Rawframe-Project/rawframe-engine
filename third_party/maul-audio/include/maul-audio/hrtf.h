// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Head-related transfer function sets, loaded from the bytes of a
// .maudhrtf file (docs/internals/file-formats.md) for the binaural renderer. The
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

// The most frames one call of a binaural effect or binaural decoder may
// process; a def past it is refused with maud_errorLimit.
#define MAUD_MAX_BINAURAL_FRAMES 16384u

    // A loaded HRTF set, at the rate it was loaded for.
    typedef struct maudHrtf maudHrtf;

    // How to load an HRTF set. Build it with maudDefaultHrtfDef.
    typedef struct maudHrtfDef
    {
        uint32_t cookie;
        // MAUD_ABI_VERSION of the headers the program was built with; the
        // cookie and this stay the first two fields in every version.
        uint32_t version;
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
        uint32_t tapCount;
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

// The cookie of an HRTF def, which its default sets and
// maudLoadHrtf checks.
#define MAUD_HRTF_DEF_COOKIE 0x6D687266u

    /// Returns the default HRTF def: no bytes, the file's own rate, at most
    /// 65,536 directions and 1,024 taps.
    ///
    /// The default is built in the program from the headers it includes,
    /// so the version it stamps is the program's.
    ///
    /// @return The def, with a valid cookie and version.
    /// @par Thread safety
    /// Safe from any thread.
    static inline maudHrtfDef maudDefaultHrtfDef(void)
    {
#ifdef __cplusplus
        maudHrtfDef def = {};
#else
    maudHrtfDef def = {0};
#endif
        def.cookie = MAUD_HRTF_DEF_COOKIE;
        def.version = MAUD_ABI_VERSION;
        def.maxDirections = 65536;
        def.maxTaps = 1024;
        return def;
    }

    /// Loads an HRTF set from a .maudhrtf file's bytes, checking every
    /// count, size and the checksum before it allocates or trusts anything.
    ///
    /// @param def      The def, from maudDefaultHrtfDef.
    /// @param hrtfOut  Receives the set; NULL on failure.
    /// @return `maud_success`; `maud_errorVersion` for a def stamped with
    ///         another binary interface than the library's, checked
    ///         before anything else of it; `maud_errorInvalid` for a NULL pointer, or a def
    ///         without its cookie or out of range; `maud_errorFormat` for
    ///         bytes that are not a well-formed file (a wrong magic, field,
    ///         size, ring layout, text or checksum); `maud_errorVersion` also for another format
    ///         version, or a field only a later version writes;
    ///         `maud_errorCapacity` when the set
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
    /// @param infoOut  Receives the information; all zero on failure.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the set is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudHrtf_GetInfo(const maudHrtf* hrtf,
                                                        maudHrtfInfo* infoOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_HRTF_H
