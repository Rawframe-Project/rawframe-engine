#pragma once

// The compressed decoders import tooling uses, behind a C boundary: the
// translation unit that holds them is C, as miniaudio and stb_vorbis are.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rawframe_compressed {
    rawframe_compressed_vorbis = 1,
    rawframe_compressed_mp3 = 2,
};

enum rawframe_decode_result {
    rawframe_decode_ok = 0,
    /// Not a stream of the named form, or broken within.
    rawframe_decode_unreadable = 1,
    /// More frames than the limit.
    rawframe_decode_too_long = 2,
    rawframe_decode_out_of_memory = 3,
};

/// Interleaved float samples at the stream's own rate and channel count.
struct rawframe_decoded {
    float* samples;
    uint64_t frames;
    uint32_t channels;
    uint32_t rate;
};

/// Decodes all of `bytes` as `form`, at most `maximum_frames` frames. On
/// success `out` holds samples the caller releases.
enum rawframe_decode_result rawframe_decode_compressed(const void* bytes,
                                                       size_t size,
                                                       enum rawframe_compressed form,
                                                       uint64_t maximum_frames,
                                                       struct rawframe_decoded* out);

void rawframe_release_decoded(struct rawframe_decoded* decoded);

#ifdef __cplusplus
}
#endif
