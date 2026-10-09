// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ambisonic bed: sources encoded into a sound field of order 1 to 3,
// the field rotated, in planar channels the host owns, and decoded for
// both ears. Channels
// follow AmbiX: ACN order with SN3D normalization, the field's axes x
// ahead, y left and z up; directions and rotations are given in the
// listener's frame (maudVector3) and converted. Encoding, rotating and
// decoding allocate nothing and do work in proportion to their frames.

#ifndef MAUL_AUDIO_AMBISONICS_H
#define MAUL_AUDIO_AMBISONICS_H

#include "maul-audio/base.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/speakers.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The highest order, and the channels it takes: (order + 1) squared.
#define MAUD_MAX_AMBISONIC_ORDER    3
#define MAUD_MAX_AMBISONIC_CHANNELS 16

    /// Returns the channels a bed of an order takes.
    ///
    /// @param order  1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @return (order + 1) squared, or 0 for an order out of range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API uint32_t maudGetAmbisonicChannelCount(uint32_t order);

    /// Computes the gains that encode a direction: one per channel of the
    /// order, in ACN order with SN3D normalization.
    ///
    /// @param order      1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param direction  The direction, in the listener's frame.
    /// @param gainsOut   Receives the order's channel count of gains.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer or a direction that is not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetAmbisonicGains(uint32_t order, maudVector3 direction,
                                                             float* gainsOut);

    /// Adds a mono source into a bed, its direction and gain moving
    /// linearly from `from` to `to` across the call: frame n is encoded
    /// with the gains (n + 1) / frames of the way.
    ///
    /// @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param from    The source at the previous call's end.
    /// @param to      The source at this call's end.
    /// @param in      frames samples.
    /// @param bed     The order's channel count of channels, frames each,
    ///                added to.
    /// @param frames  The frames.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer or a value that is not finite;
    ///         nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the bed is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudEncodeAmbisonic(uint32_t order,
                                                           const maudPanSource* from,
                                                           const maudPanSource* to, const float* in,
                                                           float* const* bed, uint32_t frames);

    /// Rotates a bed in place, the rotation moving linearly from `from`
    /// to `to` across the call (a crossfade of the two rotated fields; a
    /// large change within one call blends rather than turns). A rotation
    /// turns the field in the listener's frame: a sound from direction d
    /// comes out from the rotated d.
    ///
    /// @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param from    The rotation at the previous call's end.
    /// @param to      The rotation at this call's end.
    /// @param bed     The order's channel count of channels, frames each.
    /// @param frames  The frames.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer, or a quaternion of zero length or
    ///         not finite; nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the bed is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRotateAmbisonic(uint32_t order,
                                                           const maudQuaternion* from,
                                                           const maudQuaternion* to,
                                                           float* const* bed, uint32_t frames);

    // A binaural decoder: a bed's channels to both ears.
    typedef struct maudBinauralDecoder maudBinauralDecoder;

    // How to create a binaural decoder. Build it with
    // maudDefaultBinauralDecoderDef.
    typedef struct maudBinauralDecoderDef
    {
        uint32_t cookie;
        // The set the decoder is fitted to, at the rate it runs at. The
        // decoder keeps its own filters: the set may go once it exists.
        const maudHrtf* hrtf;
        // The order decoded, 1 to MAUD_MAX_AMBISONIC_ORDER; a bed's
        // higher channels are ignored.
        uint32_t order;
        // The most frames one call decodes, 1 to 16,384.
        uint32_t maxFrames;
        maudAllocator allocator;
    } maudBinauralDecoderDef;

    /// Returns the default binaural decoder def: no set, order 3, at most
    /// 1,024 frames per call.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudBinauralDecoderDef maudDefaultBinauralDecoderDef(void);

    /// Creates a binaural decoder: fits filters to the set by magnitude
    /// least squares (least squares below 1.5 kHz, magnitudes above), each
    /// 2 ms long and delaying the field by 0.67 ms. The fit is the heavy
    /// part, done here and never while decoding; its working memory comes
    /// from the def's allocator and goes back before this returns.
    ///
    /// @param def         The def, from maudDefaultBinauralDecoderDef,
    ///                    with a set.
    /// @param decoderOut  Receives the decoder; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, without a set or out of range, or a
    ///         set whose directions cannot carry the order;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateBinauralDecoder(const maudBinauralDecoderDef* def,
                                                                 maudBinauralDecoder** decoderOut);

    /// Destroys a binaural decoder. NULL is ignored.
    ///
    /// @param decoder  The decoder.
    /// @par Thread safety
    /// Safe from any thread; the decoder is used by one thread at a time.
    MAUD_API void maudDestroyBinauralDecoder(maudBinauralDecoder* decoder);

    /// Decodes frames of a bed for both ears, written to out[0] (left)
    /// and out[1] (right).
    ///
    /// @param decoder  The decoder.
    /// @param bed      The decoder's order's channel count of channels,
    ///                 frames each.
    /// @param out      Two channels of frames samples each.
    /// @param frames   0 to the def's maxFrames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer
    ///         or too many frames; nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the decoder is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDecodeBinaural(maudBinauralDecoder* decoder,
                                                          const float* const* bed,
                                                          float* const out[2], uint32_t frames);

    /// Forgets the bed a decoder has heard.
    ///
    /// @param decoder  The decoder.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the decoder is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudResetBinauralDecoder(maudBinauralDecoder* decoder);

    // A speaker decoder: a bed's channels to a speaker layout's.
    typedef struct maudSpeakerDecoder maudSpeakerDecoder;

    // How to create a speaker decoder. Build it with
    // maudDefaultSpeakerDecoderDef.
    typedef struct maudSpeakerDecoderDef
    {
        uint32_t cookie;
        // The layout decoded to.
        maudChannelLayout layout;
        // The order decoded, 1 to MAUD_MAX_AMBISONIC_ORDER; a bed's
        // higher channels are ignored.
        uint32_t order;
        maudAllocator allocator;
    } maudSpeakerDecoderDef;

    /// Returns the default speaker decoder def: stereo, order 3.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpeakerDecoderDef maudDefaultSpeakerDecoderDef(void);

    /// Creates a speaker decoder by all-round ambisonic decoding: the bed
    /// decoded to 240 near-uniform virtual speakers with max-rE weights,
    /// each panned to the layout as a speaker panner pans, folded into one
    /// matrix. A source encoded into the bed reaches the speakers with
    /// unit energy on average over the sphere, as one panned to them
    /// does; the low-frequency channel gets nothing.
    ///
    /// @param def         The def, from maudDefaultSpeakerDecoderDef.
    /// @param decoderOut  Receives the decoder; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, an order out of range or a layout
    ///         the library does not have; `maud_errorCapacity` when the
    ///         allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSpeakerDecoder(const maudSpeakerDecoderDef* def,
                                                                maudSpeakerDecoder** decoderOut);

    /// Destroys a speaker decoder. NULL is ignored.
    ///
    /// @param decoder  The decoder.
    /// @par Thread safety
    /// Safe from any thread; the decoder is used by one thread at a time.
    MAUD_API void maudDestroySpeakerDecoder(maudSpeakerDecoder* decoder);

    /// Copies the decoder's matrix: the layout's channel count of rows,
    /// each the order's channel count of weights, row-major. Speaker s
    /// plays the sum over c of matrix[s][c] times bed channel c.
    ///
    /// @param decoder    The decoder.
    /// @param matrixOut  Receives the matrix.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult
    maudGetSpeakerDecoderMatrix(const maudSpeakerDecoder* decoder, float* matrixOut);

    /// Decodes frames of a bed to the layout's channels, written over
    /// out. The decoder holds no state between calls.
    ///
    /// @param decoder  The decoder.
    /// @param bed      The decoder's order's channel count of channels,
    ///                 frames each.
    /// @param out      The layout's channel count of channels, frames each.
    /// @param frames   The frames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer;
    ///         nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the output is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDecodeToSpeakers(const maudSpeakerDecoder* decoder,
                                                            const float* const* bed,
                                                            float* const* out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_AMBISONICS_H
