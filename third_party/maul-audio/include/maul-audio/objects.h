// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Object streams: an output stream whose callback places mono objects
// around the listener beside the stream's ordinary output, the bed, for
// a platform renderer to spatialize (Windows' spatial sound objects,
// Apple's spatial mixer). A device's spatializer and spatialObjects say
// what the platform takes there. The host still computes what the path
// does to each source (distance, occlusion, air) and applies it to an
// object's frames; only the binaural or panning step moves to the
// platform.

#ifndef MAUL_AUDIO_OBJECTS_H
#define MAUL_AUDIO_OBJECTS_H

#include "maul-audio/base.h"
#include "maul-audio/stream.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The most objects a stream may have: above every platform's count.
#define MAUD_MAX_STREAM_OBJECTS 256u

    // One object of an object stream, as its callback sees it. The
    // position, gain and activity are kept from one period to the next,
    // so a still object costs no writes.
    struct maudStreamObject
    {
        // The block's frameCount mono frames to fill, cleared to silence
        // before the call.
        float* samples;
        // Where it sounds from, relative to the listener: metres, +x
        // right, +y up, -z ahead. The listener's origin at first.
        float position[3];
        // Its linear gain, from 0; 1 at first.
        float gain;
        // Whether it sounds. Objects start inactive; the platform's object
        // is given back while one is not, and active objects past the
        // block's objectsAvailable are not heard.
        bool active;
    };

    /// Renders the next frames of an offline object stream to the caller,
    /// which stands in for the platform's renderer: the bed into bedOut and
    /// each object's frames into the buffer its samples point to, with the
    /// position, gain and activity the callback last gave it. The block
    /// offers every object (objectsAvailable is the stream's objectCount).
    /// The stream's clock advances by frameCount.
    ///
    /// @param context      The context.
    /// @param stream       A started, running object stream of an offline
    ///                     context.
    /// @param bedOut       Room for frameCount interleaved frames of the
    ///                     stream's layout. May be NULL when frameCount is 0.
    /// @param objectsOut   objectCount records, each samples pointing to
    ///                     room for frameCount frames (or NULL when
    ///                     frameCount is 0); the rest is written.
    /// @param objectCount  The stream's objectCount.
    /// @param frameCount   The number of frames.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer where frames are due, a stream without objects,
    ///         another object count or a total that does not fit in
    ///         memory; `maud_errorUnsupported` on a context that is not
    ///         offline; `maud_errorState` for a stopped or suspended stream
    ///         or one already rendering.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. A stream renders on one
    /// thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRenderObjects(maudContext* context, maudStreamId stream,
                                                         float* bedOut,
                                                         maudStreamObject* objectsOut,
                                                         uint32_t objectCount, uint32_t frameCount);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_OBJECTS_H
