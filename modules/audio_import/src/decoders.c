// miniaudio's MP3 decoder and stb_vorbis in one translation unit, with every
// miniaudio function internal to it so it never meets the runtime's device
// layer. Only this file's two functions and stb_vorbis's own leave it
// (source decoders live in import tooling only).

#include "decoders.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// stb_vorbis sizes some allocations from a stream's headers, which a
// damaged file can make absurd, and on some damaged streams leaves what it
// allocated behind. Within its implementation an allocation that would take
// what it holds at once past a budget fails as if memory were out, which it
// handles, and every allocation is held until the decode ends, when what is
// left is freed. (stb_vorbis's whole-file helpers, which reallocate, are not
// used here.) The budget bounds the work too: a setup header of a few
// damaged bytes can declare codebooks of millions of entries, each of which
// it builds and sorts, and each costs bytes. A real stream's setup takes a
// few hundred kilobytes (tone.ogg's, 202,304 bytes in all).
//
// Where an allocation fails partway through filling a list of
// allocations, as its comment header's can, stb_vorbis frees every entry of
// the list, filled or not. So what it allocates starts zeroed, an entry it
// never filled is null, and only a pointer handed out here is freed.
enum {
    vorbis_budget = 16 * 1024 * 1024
};

struct held_block {
    void* pointer;
    size_t size;
};

static _Thread_local struct held_block* held = NULL;
static _Thread_local size_t held_count = 0;
static _Thread_local size_t held_capacity = 0;
static _Thread_local size_t held_bytes = 0;

static void* held_malloc(size_t size) {
    if (size > vorbis_budget - held_bytes) {
        return NULL;
    }
    if (held_count == held_capacity) {
        const size_t grown = held_capacity == 0 ? 64 : held_capacity * 2;
        struct held_block* const larger = realloc(held, grown * sizeof(struct held_block));
        if (larger == NULL) {
            return NULL;
        }
        held = larger;
        held_capacity = grown;
    }
    void* const made = calloc(1, size);
    if (made != NULL) {
        held[held_count++] = (struct held_block){made, size};
        held_bytes += size;
    }
    return made;
}

static void held_free(void* pointer) {
    for (size_t index = 0; index < held_count; ++index) {
        if (held[index].pointer == pointer) {
            held_bytes -= held[index].size;
            held[index] = held[--held_count];
            free(pointer);
            return;
        }
    }
}

static void release_held(void) {
    for (size_t index = 0; index < held_count; ++index) {
        free(held[index].pointer);
    }
    free(held);
    held = NULL;
    held_count = 0;
    held_capacity = 0;
    held_bytes = 0;
}

#define MA_API static
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
// WAVE is read by the audio module's own decoder; FLAC is not taken yet.
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

// stb_vorbis allocates a stream's comment list at the count its comment
// header declares, and when that allocation fails it walks the list it did
// not get to free its entries. So the count is read here first, from the
// stream's second packet as its pages' lacing makes it, and a stream is
// refused unless the budget holds the list beside what stb_vorbis holds
// before it: itself and the vendor string, each rounded up to eight bytes
// as it rounds them.
// Where stb_vorbis would read that packet's bytes differently, the stream
// is refused too: a page of no segments, whose lacing it takes from the page
// before, and a page carrying the packet on without saying it continues one,
// past which it reads on. So is a comment header cut before its count's
// last byte: from memory, stb_vorbis reads past the end as zeros, which
// makes a count of what is there. What is not the comment header at all is
// left to stb_vorbis, which refuses it.
static int vorbis_comments_fit(const unsigned char* bytes, size_t size) {
    size_t page = 0;
    size_t packet = 0;
    // The second packet's bytes so far: its type and "vorbis" (7), the
    // vendor string's length (4), the vendor string, the comment count (4).
    uint64_t at = 0;
    uint32_t vendor = 0;
    uint32_t count = 0;
    while (page + 27 <= size && memcmp(bytes + page, "OggS", 4) == 0) {
        const size_t segments = bytes[page + 26];
        size_t data = page + 27 + segments;
        if (data > size) {
            return packet != 1;
        }
        if (packet == 1 && (segments == 0 || (at > 0 && (bytes[page + 5] & 1U) == 0))) {
            return 0;
        }
        for (size_t segment = 0; segment < segments; ++segment) {
            const size_t lacing = bytes[page + 27 + segment];
            for (size_t index = 0; packet == 1 && index < lacing && data + index < size; ++index, ++at) {
                const uint32_t byte = bytes[data + index];
                if (at >= 7 && at < 11) {
                    vendor |= byte << (8 * (at - 7));
                } else if (at >= 11 && at - 11 >= vendor && at - 11 - vendor < 4) {
                    count |= byte << (8 * (at - 11 - vendor));
                    if (at - 11 - vendor == 3) {
                        const uint64_t held_before = (((uint64_t)sizeof(stb_vorbis) + 7) & ~(uint64_t)7) +
                                                     (((uint64_t)vendor + 1 + 7) & ~(uint64_t)7);
                        return (int32_t)count <= 0 || held_before + (uint64_t)count * sizeof(char*) <= vorbis_budget;
                    }
                }
            }
            // A segment cut short ends the data, not the packet its lacing
            // says it ends.
            if (packet == 1 && data + lacing > size) {
                return 0;
            }
            data += lacing;
            if (lacing < 255 && ++packet == 2) {
                return 1;
            }
        }
        page = data;
    }
    return packet != 1;
}

enum rawframe_decode_result rawframe_decode_compressed(const void* bytes,
                                                       size_t size,
                                                       enum rawframe_compressed form,
                                                       uint64_t maximum_frames,
                                                       struct rawframe_decoded* out) {
    out->samples = NULL;
    out->frames = 0;
    // Float at the stream's own channels and rate: nothing is converted or
    // resampled here (resampling is the mixer's alone).
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.encodingFormat = form == rawframe_compressed_vorbis ? ma_encoding_format_vorbis : ma_encoding_format_mp3;
    if (form == rawframe_compressed_vorbis && !vorbis_comments_fit(bytes, size)) {
        return rawframe_decode_unreadable;
    }
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
