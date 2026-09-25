// miniaudio's MP3 decoder and stb_vorbis in one translation unit, with every
// miniaudio function internal to it so it never meets the runtime's device
// layer. Only this file's two functions and stb_vorbis's own leave it
// (ADR-0058: source decoders live in import tooling only).

#include "decoders.h"

#include <stdlib.h>

// stb_vorbis sizes some allocations from a stream's headers, which a
// damaged file can make absurd, and on some damaged streams leaves what it
// allocated behind. Within its implementation every allocation past a bound
// fails as if memory were out, which it handles, and every allocation is
// held until the decode ends, when what is left is freed. (stb_vorbis's
// whole-file helpers, which reallocate, are not used here.)
enum {
    largest_vorbis_allocation = 64 * 1024 * 1024
};

static _Thread_local void** held = NULL;
static _Thread_local size_t held_count = 0;
static _Thread_local size_t held_capacity = 0;

static void* held_malloc(size_t size) {
    if (size > largest_vorbis_allocation) {
        return NULL;
    }
    if (held_count == held_capacity) {
        const size_t grown = held_capacity == 0 ? 64 : held_capacity * 2;
        void** const larger = realloc(held, grown * sizeof(void*));
        if (larger == NULL) {
            return NULL;
        }
        held = larger;
        held_capacity = grown;
    }
    void* const made = malloc(size);
    if (made != NULL) {
        held[held_count++] = made;
    }
    return made;
}

static void held_free(void* pointer) {
    for (size_t index = 0; index < held_count; ++index) {
        if (held[index] == pointer) {
            held[index] = held[--held_count];
            break;
        }
    }
    free(pointer);
}

static void release_held(void) {
    for (size_t index = 0; index < held_count; ++index) {
        free(held[index]);
    }
    free(held);
    held = NULL;
    held_count = 0;
    held_capacity = 0;
}

#define MA_API static
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
// WAVE is read by the audio module's own decoder; FLAC is deferred by
// SPEC-0036.
#define MA_NO_WAV
#define MA_NO_FLAC

// The order is stb_vorbis's and miniaudio's: stb_vorbis's declarations, then
// miniaudio, which finds them, then stb_vorbis's implementation.
// clang-format off
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#include "miniaudio.c"
#undef STB_VORBIS_HEADER_ONLY
#define malloc(size) held_malloc(size)
#define free(pointer) held_free(pointer)
#include "extras/stb_vorbis.c"
#undef malloc
#undef free
// clang-format on

enum {
    chunk_frames = 4096
};

enum rawframe_decode_result rawframe_decode_compressed(const void* bytes,
                                                       size_t size,
                                                       enum rawframe_compressed form,
                                                       uint64_t maximum_frames,
                                                       struct rawframe_decoded* out) {
    out->samples = NULL;
    out->frames = 0;
    // Float at the stream's own channels and rate: nothing is converted or
    // resampled here (ADR-0038: resampling is the mixer's alone).
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.encodingFormat = form == rawframe_compressed_vorbis ? ma_encoding_format_vorbis : ma_encoding_format_mp3;
    ma_decoder decoder;
    if (ma_decoder_init_memory(bytes, size, &config, &decoder) != MA_SUCCESS) {
        release_held();
        return rawframe_decode_unreadable;
    }
    out->channels = decoder.outputChannels;
    out->rate = decoder.outputSampleRate;
    if (out->channels == 0) {
        ma_decoder_uninit(&decoder);
        release_held();
        return rawframe_decode_unreadable;
    }
    enum rawframe_decode_result result = rawframe_decode_ok;
    uint64_t capacity = 0;
    for (;;) {
        if (out->frames + chunk_frames > capacity) {
            const uint64_t grown = capacity == 0 ? chunk_frames * 16 : capacity * 2;
            float* const larger = realloc(out->samples, (size_t)(grown * out->channels * sizeof(float)));
            if (larger == NULL) {
                result = rawframe_decode_out_of_memory;
                break;
            }
            out->samples = larger;
            capacity = grown;
        }
        ma_uint64 read = 0;
        const ma_result status =
            ma_decoder_read_pcm_frames(&decoder, out->samples + (out->frames * out->channels), chunk_frames, &read);
        out->frames += read;
        if (out->frames > maximum_frames) {
            result = rawframe_decode_too_long;
            break;
        }
        if (status == MA_AT_END || read == 0) {
            break;
        }
        if (status != MA_SUCCESS) {
            result = rawframe_decode_unreadable;
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    release_held();
    if (result != rawframe_decode_ok || out->frames == 0) {
        rawframe_release_decoded(out);
        return result == rawframe_decode_ok ? rawframe_decode_unreadable : result;
    }
    return rawframe_decode_ok;
}

void rawframe_release_decoded(struct rawframe_decoded* decoded) {
    free(decoded->samples);
    decoded->samples = NULL;
    decoded->frames = 0;
}
