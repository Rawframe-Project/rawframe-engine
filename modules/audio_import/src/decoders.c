// miniaudio's MP3 decoder and stb_vorbis in one translation unit, with every
// miniaudio function internal to it so it never meets the runtime's device
// layer. Only this file's two functions and stb_vorbis's own leave it
// (source decoders live in import tooling only).

#include "decoders.h"

#include <limits.h>
#include <math.h>
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

// Each held pointer's slot in `held`, plus one, by open addressing (nought
// is an empty entry) at twice held's capacity, so a free finds its block at
// once however many are held: stb_vorbis frees a comment header's entries
// one by one, and a source of a few megabytes can hold a million. A pointer
// not handed out here is looked up, never read through.
static _Thread_local struct held_block* held = NULL;
static _Thread_local size_t held_count = 0;
static _Thread_local size_t held_capacity = 0;
static _Thread_local size_t held_bytes = 0;
static _Thread_local size_t* held_index = NULL;
static _Thread_local size_t held_mask = 0;

static size_t held_home(const void* pointer) {
    return (size_t)((((uint64_t)(uintptr_t)pointer >> 4) * UINT64_C(0x9E3779B97F4A7C15)) >> 32) & held_mask;
}

// Where `pointer`'s entry is, or the empty entry its own would take.
static size_t held_entry(const void* pointer) {
    size_t at = held_home(pointer);
    while (held_index[at] != 0 && held[held_index[at] - 1].pointer != pointer) {
        at = (at + 1) & held_mask;
    }
    return at;
}

// Empties an entry, moving back each one after it that its home allows, so
// every pointer is still found from its home without a gap.
static void held_unindex(size_t at) {
    held_index[at] = 0;
    for (size_t next = (at + 1) & held_mask; held_index[next] != 0; next = (next + 1) & held_mask) {
        const size_t home = held_home(held[held_index[next] - 1].pointer);
        if (((next - home) & held_mask) >= ((next - at) & held_mask)) {
            held_index[at] = held_index[next];
            held_index[next] = 0;
            at = next;
        }
    }
}

static int held_grow(void) {
    // Both grow or neither: an index of fewer entries than held's capacity
    // could fill, and a lookup in a full index would never end.
    const size_t grown = held_capacity == 0 ? 64 : held_capacity * 2;
    size_t* const index = calloc(grown * 2, sizeof(size_t));
    if (index == NULL) {
        return 0;
    }
    struct held_block* const larger = realloc(held, grown * sizeof(struct held_block));
    if (larger == NULL) {
        free(index);
        return 0;
    }
    held = larger;
    held_capacity = grown;
    free(held_index);
    held_index = index;
    held_mask = (grown * 2) - 1;
    for (size_t slot = 0; slot < held_count; ++slot) {
        held_index[held_entry(held[slot].pointer)] = slot + 1;
    }
    return 1;
}

static void* held_malloc(size_t size) {
    if (size > vorbis_budget - held_bytes) {
        return NULL;
    }
    if (held_count == held_capacity && !held_grow()) {
        return NULL;
    }
    void* const made = calloc(1, size);
    if (made != NULL) {
        held_index[held_entry(made)] = held_count + 1;
        held[held_count++] = (struct held_block){made, size};
        held_bytes += size;
    }
    return made;
}

static void held_free(void* pointer) {
    if (pointer == NULL || held_index == NULL) {
        return;
    }
    const size_t at = held_entry(pointer);
    if (held_index[at] == 0) {
        return;
    }
    const size_t slot = held_index[at] - 1;
    held_bytes -= held[slot].size;
    free(pointer);
    held_unindex(at);
    const size_t last = held_count - 1;
    if (slot != last) {
        held_index[held_entry(held[last].pointer)] = slot + 1;
        held[slot] = held[last];
    }
    held_count = last;
}

static void release_held(void) {
    for (size_t index = 0; index < held_count; ++index) {
        free(held[index].pointer);
    }
    free(held);
    free(held_index);
    held = NULL;
    held_index = NULL;
    held_count = 0;
    held_capacity = 0;
    held_mask = 0;
    held_bytes = 0;
}

// stb_vorbis counts a codebook's lookup values by casting floor() of a
// float worked from the header's entries and dimensions to int, and a
// damaged header makes that infinite (no dimensions) or past what an int
// holds (many). The cast would then be undefined. Within its implementation
// floor() is this: the same wherever the result fits an int, clamped where
// it does not, and nought for not a number, which every real codebook's
// count is unchanged by and which leads a damaged one to be refused.
static double int_floor(double value) {
    const double floored = floor(value);
    if (floored != floored) {
        return 0;
    }
    // Below INT_MAX by more than one, so stb_vorbis's ++ on it still fits.
    const double highest = (double)INT_MAX - 1;
    return floored > highest ? highest : (floored < (double)INT_MIN ? (double)INT_MIN : floored);
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
#define floor(value) int_floor(value)
#include "extras/stb_vorbis.c"
#undef malloc
#undef free
#undef floor
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
// It also takes the vendor string's length and each comment's plus one in
// an int, so every field of the header must lie within its packet: the
// stream is refused when the packet ends before the count, before any of
// the count's comments, or inside one. That bounds the count by the packet
// too, at four bytes a comment.
// Where stb_vorbis would read that packet's bytes differently, the stream
// is refused as well: a page of no segments, whose lacing it takes from the
// page before, and a page carrying the packet on without saying it
// continues one, past which it reads on. So is a comment header cut by the
// end of the data: from memory, stb_vorbis reads past the end as zeros.
// What is not the comment header at all is left to stb_vorbis, which
// refuses it.
static int vorbis_comments_fit(const unsigned char* bytes, size_t size) {
    enum {
        vendor_length,
        comment_count,
        comment_length,
        read_whole
    };
    size_t page = 0;
    size_t packet = 0;
    // The second packet's bytes so far, and where its next four-byte field
    // begins: the vendor string's length after its type and "vorbis" (7),
    // the comment count after the vendor string, each comment's length after
    // the comment before. Once all are read, where the last comment ends.
    uint64_t at = 0;
    uint64_t field = 7;
    uint32_t value = 0;
    int stage = vendor_length;
    uint32_t vendor = 0;
    uint32_t left = 0;
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
                if (stage == read_whole || at < field) {
                    continue;
                }
                value |= (uint32_t)bytes[data + index] << (8 * (at - field));
                if (at - field < 3) {
                    continue;
                }
                field = at + 1;
                if (stage == vendor_length) {
                    vendor = value;
                    field += vendor;
                    stage = comment_count;
                } else if (stage == comment_count) {
                    const uint64_t held_before = (((uint64_t)sizeof(stb_vorbis) + 7) & ~(uint64_t)7) +
                                                 (((uint64_t)vendor + 1 + 7) & ~(uint64_t)7);
                    if ((int32_t)value <= 0) {
                        stage = read_whole;
                    } else if (held_before + (uint64_t)value * sizeof(char*) > vorbis_budget) {
                        return 0;
                    } else {
                        left = value;
                        stage = comment_length;
                    }
                } else {
                    field += value;
                    if (--left == 0) {
                        stage = read_whole;
                    }
                }
                value = 0;
            }
            // A segment cut short ends the data, not the packet its lacing
            // says it ends.
            if (packet == 1 && data + lacing > size) {
                return 0;
            }
            data += lacing;
            if (lacing < 255 && ++packet == 2) {
                return stage == read_whole && at >= field;
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
