#include "ground.h"

// For `KEST_CHECKED`, which is the one thing this file wants from the module
// above it: whether this build is the one that checks its own work. Asking the
// compiler again here would be two readings of one question that agree until
// one of them moves. See D330.
#include "mem.h"

#include <stdlib.h>
#include <string.h>

// A place is one allocation as far as the host is concerned only when it is
// the whole of a plot, so reading one element past the end of something in a
// plot is memory this file owns and nothing anywhere says a word about it. The
// sanitised build is told instead: a plot is poisoned when it is taken, a
// place is opened to what was asked for and no further, and a place given back
// is closed again. Off the end of a thing is the read this project has got
// wrong before, and this is what makes it visible -- the arena beside this one
// has said it since D786, and what a program makes moved off the arena.
//
// Opened to what was asked for and not a byte further, which is what makes a
// read one element past the end of a value visible: a place is wider than the
// value in it, and every byte of the difference is closed. The sanitiser keeps
// one shadow byte for every eight, so a value that ends in the middle of one
// leaves that step half open and a read running off it is reported as a crash
// of no particular kind rather than by name -- which is still a crash, and
// still the read this is here to find.
#if KEST_CHECKED
#include <sanitizer/asan_interface.h>
#define POISON(at, bytes) __asan_poison_memory_region((at), (bytes))
#define OPEN(at, bytes) __asan_unpoison_memory_region((at), (bytes))
#else
#define POISON(at, bytes) ((void)(at), (void)(bytes))
#define OPEN(at, bytes) ((void)(at), (void)(bytes))
#endif

// The sanitised build says when this stops agreeing with itself, and no other
// build does: what it says it is holding against what its plots have given
// away. They are the same number and nothing separates them -- a place is
// handed out whole or not at all, so what is standing on the ground is the
// width of every place in use and nothing besides. A ceiling is refused
// against that number, so a number that has drifted is a program stopped early
// or let past what a host allowed it, and nothing about the program's
// behaviour would say so. It is a walk of the plots, which is what everything
// here is written to avoid.
#if KEST_CHECKED
#include <stdio.h>
#endif

// The one place in this tree that asks the host for aligned memory. An address
// says which place it is in by being masked, so the places have to sit at a
// known boundary — and the two compilers do not spell that the same. ISO C has
// `aligned_alloc` and the older of the two Windows libraries has never had it.
#if defined(_WIN32)
#include <malloc.h>
#define GROUND_ALLOC(align, bytes) _aligned_malloc((bytes), (align))
#define GROUND_FREE(at) _aligned_free(at)
#else
#define GROUND_ALLOC(align, bytes) aligned_alloc((align), (bytes))
#define GROUND_FREE(at) free(at)
#endif

// How far apart two places sit, which is what an address is masked to. Big
// enough that the shape costs a small part of what it holds, and small enough
// that a program which makes one piece of text and one run of numbers does not
// reserve a plot of sixty-four kilobytes for each: every width this hands out
// has a plot of its own, so the floor is paid once a width rather than once,
// and a program that makes four kinds of thing pays it four times.
#define PLOT 16384u
#define PLOT_MASK (~(uintptr_t)(PLOT - 1))

// The most places one plot can hold, which is what the bitmaps are sized for:
// sixteen bytes is the narrowest a place is, because a machine asks for
// alignment of sixteen and a place that could not answer that would be a place
// nothing could use.
#define NARROWEST 16u
#define MOST_PLACES (PLOT / NARROWEST)
#define BITMAP_WORDS (MOST_PLACES / 64u)

// What this hands out, in bytes. Everything wider than the last of them gets a
// plot to itself. The steps are close together where the values are — a piece
// of text is tens of bytes and a run of numbers is hundreds — and double after
// that, because a program that asks for four kilobytes is not a program whose
// memory is decided by the difference between four and six.
static const uint32_t WIDTHS[] = {
    16,    32,    48,    64,    96,     128,    192,    256,   384,
    512,   768,   1024,  1536,  2048,   3072,   4096,   6144,  8192,
    12288, 16384, 24576, 32768, 49152,  65536,  98304,  131072};
#define WIDTH_COUNT (sizeof(WIDTHS) / sizeof(WIDTHS[0]))
#define WIDEST 131072u

// How many of one width a plot holds. A plot of one would be an allocation to
// the host for every value, which is what the widths are here to avoid; a plot
// of very many would make the first program to ask for one wide thing pay for
// eight. Eight is where the shape costs under a fifth of a per cent of what it
// holds and a plot is never more than a megabyte.
#define PER_PLOT 8

// And the most a plot is, so that one program asking for one wide value does
// not take a megabyte to hold it.
#define PLOT_MOST (256u * 1024u)

// What a plot is, so a walk that met a pointer into the middle of one can tell
// it from memory that is somebody else's. Read after the mask and before
// anything else.
#define IS_PLOT 0x4b504c54u

typedef struct Plot {
    uint32_t what;
    // Which of the widths above this one hands out, or WIDTH_COUNT for a plot
    // holding one thing wider than any of them.
    uint16_t width_index;
    // A width, not one of the widths above: a plot holding one thing wider
    // than any of them has a stride of the whole of what it holds, and that
    // does not fit in the width of a width.
    size_t stride;
    uint32_t places;
    uint32_t taken;
    // How many of the places have ever been handed out. Below this a free
    // place is found in the bitmap, and at it a place is had for the first
    // time — which is what keeps a fresh plot from reading a bitmap of
    // nothing to find the first place in it.
    uint32_t reached;
    // Where a search for a free place starts, so a plot that is filling up
    // does not read the same full words at the front of the bitmap every
    // time.
    uint32_t hint;
    // How many bytes the places are, altogether. The shape is not among them:
    // it is asked for separately, so the places fill what was asked for
    // exactly and a value of a hundred and thirty kilobytes does not take a
    // hundred and ninety-two to hold.
    size_t bytes;
    struct Plot *next;
    // The next plot of this width with a free place in it. A plot leaves this
    // list when it fills and joins it when a sweep gives it something back.
    struct Plot *next_free;
    bool listed_free;
    unsigned char *data;
    uint64_t used[BITMAP_WORDS];
    uint64_t marks[BITMAP_WORDS];
    // What each place holds, two bits of it a place. A byte a place would be
    // four kilobytes of every sixty-four, which is the difference between a
    // shape that costs three per cent of what it holds and one that costs
    // nine.
    uint64_t kind_low[BITMAP_WORDS];
    uint64_t kind_high[BITMAP_WORDS];
} Plot;

static void say_kind(Plot *plot, uint32_t place, KestGroundKind kind) {
    uint64_t bit = (uint64_t)1 << (place % 64);
    if (((unsigned)kind & 1u) != 0) {
        plot->kind_low[place / 64] |= bit;
    } else {
        plot->kind_low[place / 64] &= ~bit;
    }
    if (((unsigned)kind & 2u) != 0) {
        plot->kind_high[place / 64] |= bit;
    } else {
        plot->kind_high[place / 64] &= ~bit;
    }
}

static KestGroundKind kind_of(const Plot *plot, uint32_t place) {
    uint64_t bit = (uint64_t)1 << (place % 64);
    unsigned low = (plot->kind_low[place / 64] & bit) != 0 ? 1u : 0u;
    unsigned high = (plot->kind_high[place / 64] & bit) != 0 ? 2u : 0u;
    return (KestGroundKind)(low | high);
}

// Where in a plot a place is, so the two bitmaps and the address agree.
#if KEST_CHECKED
static void holds_together(const KestGround *ground, const char *after);
#endif

// Which bit is the lowest one set in a word that has one: the lowest bit on its
// own, multiplied by a sequence in which every run of six bits is different,
// leaves which it was in the top six. Stepping a bit at a time was most of
// what giving back and finding a place cost in a plot with a few places free.
// See D1173.
static uint32_t lowest_set(uint64_t word) {
    static const uint8_t AT[64] = {
        0,  1,  56, 2,  57, 49, 28, 3,  61, 58, 42, 50, 38, 29, 17, 4,
        62, 47, 59, 36, 45, 43, 51, 22, 53, 39, 33, 30, 24, 18, 12, 5,
        63, 55, 48, 27, 60, 41, 37, 16, 46, 35, 44, 21, 52, 32, 23, 11,
        54, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6};
    return AT[((word & ((uint64_t)0 - word)) * 0x03f79d71b4ca8b09u) >> 58];
}

static uint32_t place_of(const Plot *plot, const void *at) {
    size_t away = (size_t)((const unsigned char *)at - plot->data);
    return (uint32_t)(away / plot->stride);
}

typedef struct Counts {
    uint64_t allocations;
    uint64_t asked;
    uint64_t given;
    uint64_t grown;
    uint64_t sweeps;
    uint64_t reclaimed;
    uint64_t plots_made;
    uint64_t plots_freed;
    uint64_t blocks;
    // The same allocations again, split two ways: by what the place holds and
    // by which of the widths it was cut from. A total says how much a program
    // asks for and neither of these does; what they say is what it asks for --
    // a program that is all short pieces of text and one that is all wide runs
    // have the same total and nothing else in common. Counted here rather than
    // worked out from the plots, because a plot says what is in it now and
    // this is what was ever handed out. See D1032.
    uint64_t by_kind[4];
    uint64_t by_width[WIDTH_COUNT + 1];
} Counts;

typedef struct KestGround {
    Plot *plots;
    Plot *free_plots[WIDTH_COUNT];
    // Every plot this holds, by the boundary its addresses mask to: a wide
    // plot covers several of those and is written under each, so one masking
    // and one lookup answer for every address whatever it is in.
    Plot **index;
    size_t index_capacity;
    size_t index_count;
    size_t used;
    size_t since;
    size_t taken;
    size_t ceiling;
    size_t refused;
    bool refused_by_ceiling;
    // The blocks of working memory that are open, innermost last, and what
    // each has taken. A block gives back what it took rather than waiting for
    // a walk to find it, which is what a `scratch { }` block is for.
    struct Block {
        void **took;
        size_t count;
        size_t capacity;
    } *blocks;
    uint32_t block_count;
    uint32_t block_room;
    // What this has done, for a host that asked to be told. Counted always
    // rather than only when somebody is asking, because every one of them is
    // at an allocation, a sweep or a plot -- a program does millions of
    // instructions between any two of those, and a counter there costs
    // nothing measurable where one at the top of the dispatch loop cost a
    // third of the machine. See D979 for that one and D1007 for these.
    Counts counted;
} KestGround;

uint32_t kest_ground_widths(const KestGround *ground, uint32_t *widths,
                            uint64_t *taken, uint32_t many, uint64_t *kinds) {
    if (ground == NULL) {
        return 0;
    }
    if (kinds != NULL) {
        for (uint32_t i = 0; i < 4; i++) {
            kinds[i] = ground->counted.by_kind[i];
        }
    }
    uint32_t said = 0;
    for (uint32_t i = 0; i <= WIDTH_COUNT && said < many; i++) {
        if (widths != NULL) {
            // The last of them is every place wider than the ladder, each of
            // which got a plot of its own, and there is no one width to say.
            widths[said] = i < WIDTH_COUNT ? WIDTHS[i] : 0;
        }
        if (taken != NULL) {
            taken[said] = ground->counted.by_width[i];
        }
        said++;
    }
    return said;
}

void kest_ground_plots(const KestGround *ground, KestGroundPlots *into) {
    if (into == NULL) {
        return;
    }
    memset(into, 0, sizeof *into);
    if (ground == NULL) {
        return;
    }
    for (const Plot *plot = ground->plots; plot != NULL; plot = plot->next) {
        into->plots++;
        into->bytes += plot->bytes;
        into->places += plot->places;
        into->taken += plot->taken;
        uint32_t free_places = plot->places - plot->taken;
        into->free_places += free_places;
        into->free_bytes += (uint64_t)free_places * (uint64_t)plot->stride;
    }
}

KestGround *kest_ground_new(void) {
    KestGround *ground = calloc(1, sizeof(KestGround));
    return ground;
}

static void forget_plot(KestGround *ground, const Plot *plot);

void kest_ground_free(KestGround *ground) {
    if (ground == NULL) {
        return;
    }
    Plot *plot = ground->plots;
    while (plot != NULL) {
        Plot *next = plot->next;
        OPEN(plot->data, plot->bytes);
        GROUND_FREE(plot->data);
        free(plot);
        plot = next;
    }
    for (uint32_t i = 0; i < ground->block_room; i++) {
        free(ground->blocks[i].took);
    }
    free(ground->blocks);
    free(ground->index);
    free(ground);
}

// The index is addresses masked to a boundary, and those are already spread
// out: the low sixteen bits are nought for every one of them. So what is
// hashed is what is left after the mask is taken off.
static size_t scatter(uintptr_t unit) {
    uint64_t x = (uint64_t)(unit / PLOT);
    x *= 0x9e3779b97f4a7c15ull;
    x ^= x >> 29;
    return (size_t)x;
}

static bool index_put(KestGround *ground, uintptr_t unit, Plot *plot);

static bool index_grow(KestGround *ground) {
    size_t bigger = ground->index_capacity == 0 ? 64 : ground->index_capacity * 2;
    Plot **was = ground->index;
    size_t was_capacity = ground->index_capacity;
    Plot **now = calloc(bigger, sizeof(Plot *) * 2);
    if (now == NULL) {
        return false;
    }
    ground->index = now;
    ground->index_capacity = bigger;
    ground->index_count = 0;
    for (size_t i = 0; i < was_capacity; i++) {
        uintptr_t unit = (uintptr_t)was[i * 2];
        if (unit != 0) {
            if (!index_put(ground, unit, was[i * 2 + 1])) {
                free(now);
                ground->index = was;
                ground->index_capacity = was_capacity;
                return false;
            }
        }
    }
    free(was);
    return true;
}

// Two pointers a slot: the boundary an address masks to, and the plot it is
// in. Kept in one allocation rather than two so a lookup reads one line of
// memory rather than two.
static bool index_put(KestGround *ground, uintptr_t unit, Plot *plot) {
    if (ground->index_capacity == 0 ||
        (ground->index_count + 1) * 10 >= ground->index_capacity * 7) {
        if (!index_grow(ground)) {
            return false;
        }
    }
    size_t mask = ground->index_capacity - 1;
    size_t at = scatter(unit) & mask;
    while (ground->index[at * 2] != NULL) {
        if ((uintptr_t)ground->index[at * 2] == unit) {
            ground->index[at * 2 + 1] = plot;
            return true;
        }
        at = (at + 1) & mask;
    }
    ground->index[at * 2] = (Plot *)unit;
    ground->index[at * 2 + 1] = plot;
    ground->index_count++;
    return true;
}

static Plot *index_get(const KestGround *ground, uintptr_t unit) {
    if (ground->index_capacity == 0) {
        return NULL;
    }
    size_t mask = ground->index_capacity - 1;
    size_t at = scatter(unit) & mask;
    while (ground->index[at * 2] != NULL) {
        if ((uintptr_t)ground->index[at * 2] == unit) {
            return ground->index[at * 2 + 1];
        }
        at = (at + 1) & mask;
    }
    return NULL;
}

// Taking an entry out of an open-addressed table leaves a hole a later probe
// stops at, so what goes in its place is whatever probed past it. This is the
// standard walk forward, and it is here rather than left undone because a plot
// given back and never forgotten would answer for memory the host owns again.
static void index_take(KestGround *ground, uintptr_t unit) {
    if (ground->index_capacity == 0) {
        return;
    }
    size_t mask = ground->index_capacity - 1;
    size_t at = scatter(unit) & mask;
    while (ground->index[at * 2] != NULL) {
        if ((uintptr_t)ground->index[at * 2] == unit) {
            break;
        }
        at = (at + 1) & mask;
    }
    if (ground->index[at * 2] == NULL) {
        return;
    }
    ground->index[at * 2] = NULL;
    ground->index[at * 2 + 1] = NULL;
    ground->index_count--;
    size_t after = (at + 1) & mask;
    while (ground->index[after * 2] != NULL) {
        uintptr_t moving = (uintptr_t)ground->index[after * 2];
        Plot *plot = ground->index[after * 2 + 1];
        ground->index[after * 2] = NULL;
        ground->index[after * 2 + 1] = NULL;
        ground->index_count--;
        index_put(ground, moving, plot);
        after = (after + 1) & mask;
    }
}

static void forget_plot(KestGround *ground, const Plot *plot) {
    uintptr_t base = (uintptr_t)plot->data;
    for (size_t away = 0; away < plot->bytes; away += PLOT) {
        index_take(ground, base + away);
    }
}

static Plot *plot_holding(const KestGround *ground, const void *at) {
    if (at == NULL) {
        return NULL;
    }
    uintptr_t unit = (uintptr_t)at & PLOT_MASK;
    Plot *plot = index_get(ground, unit);
    if (plot == NULL || plot->what != IS_PLOT) {
        return NULL;
    }
    const unsigned char *low = plot->data;
    const unsigned char *high = plot->data + (size_t)plot->places * plot->stride;
    if ((const unsigned char *)at < low || (const unsigned char *)at >= high) {
        return NULL;
    }
    return plot;
}

// A ceiling is over what the program is holding and not over what this asked
// the host for. The two differ by the shape of the places and by the places
// that are standing empty, and neither of those is the program's: an arena
// took blocks of sixty-four kilobytes and charged a ceiling for the bytes it
// handed out of them, and a machine given sixty-four kilobytes could hold
// sixty-four kilobytes. It still can.
static bool room_for(KestGround *ground, size_t width) {
    if (ground->ceiling != 0 && ground->used + width > ground->ceiling) {
        ground->refused = width;
        ground->refused_by_ceiling = true;
        return false;
    }
    return true;
}

static Plot *new_plot(KestGround *ground, size_t width_index, size_t bytes) {
    Plot *plot = calloc(1, sizeof(Plot));
    unsigned char *data = plot == NULL ? NULL : GROUND_ALLOC(PLOT, bytes);
    ground->counted.plots_made += data == NULL ? 0 : 1;
    if (data == NULL) {
        free(plot);
        ground->refused = bytes;
        ground->refused_by_ceiling = false;
        return NULL;
    }
    plot->what = IS_PLOT;
    plot->width_index = (uint16_t)width_index;
    plot->bytes = bytes;
    plot->data = data;
    POISON(data, bytes);
    if (width_index < WIDTH_COUNT) {
        plot->stride = WIDTHS[width_index];
        plot->places = (uint32_t)(bytes / plot->stride);
        if (plot->places > MOST_PLACES) {
            plot->places = MOST_PLACES;
        }
    } else {
        plot->stride = bytes;
        plot->places = 1;
    }
    uintptr_t base = (uintptr_t)data;
    for (size_t away = 0; away < bytes; away += PLOT) {
        if (!index_put(ground, base + away, plot)) {
            for (size_t undo = 0; undo < away; undo += PLOT) {
                index_take(ground, base + undo);
            }
            GROUND_FREE(data);
            free(plot);
            ground->refused = bytes;
            ground->refused_by_ceiling = false;
            return NULL;
        }
    }
    plot->next = ground->plots;
    ground->plots = plot;
    return plot;
}

static void joined_free(KestGround *ground, Plot *plot) {
    if (plot->listed_free || plot->width_index >= WIDTH_COUNT) {
        return;
    }
    plot->listed_free = true;
    plot->next_free = ground->free_plots[plot->width_index];
    ground->free_plots[plot->width_index] = plot;
}

// The first place at or after `from` that nothing is in. A word with every bit
// set is stepped over whole, which is what makes finding a place in a plot
// that is nearly full cost about what finding one in an empty plot costs.
static uint32_t free_place(const Plot *plot, uint32_t from) {
    uint32_t word = from / 64;
    // The places before `from` in its word are counted as taken, so a search
    // from the middle of a word does not find one behind where it began.
    uint64_t behind = from % 64 == 0 ? 0 : ~(uint64_t)0 >> (64 - from % 64);
    while (word * 64 < plot->places) {
        uint64_t free = ~(plot->used[word] | behind);
        if (free != 0) {
            uint32_t place = word * 64 + lowest_set(free);
            return place < plot->places ? place : plot->places;
        }
        behind = 0;
        word++;
    }
    return plot->places;
}

static bool remember(KestGround *ground, void *at) {
    if (ground->block_count == 0) {
        return true;
    }
    struct Block *block = &ground->blocks[ground->block_count - 1];
    if (block->count == block->capacity) {
        size_t bigger = block->capacity == 0 ? 32 : block->capacity * 2;
        void **grown = realloc(block->took, bigger * sizeof(void *));
        if (grown == NULL) {
            return false;
        }
        block->took = grown;
        block->capacity = bigger;
    }
    block->took[block->count++] = at;
    return true;
}

static void give_back(KestGround *ground, Plot *plot, uint32_t place) {
    uint64_t bit = (uint64_t)1 << (place % 64);
    if ((plot->used[place / 64] & bit) == 0) {
        return;
    }
    plot->used[place / 64] &= ~bit;
    POISON(plot->data + (size_t)place * plot->stride, plot->stride);
    plot->taken--;
    ground->used -= plot->stride;
    if (place < plot->hint) {
        plot->hint = place;
    }
    joined_free(ground, plot);
}

static void *room_in_a_plot(KestGround *ground, size_t bytes,
                            KestGroundKind kind, Plot **held);

void *kest_ground_take(KestGround *ground, size_t bytes,
                       KestGroundKind kind) {
    Plot *plot = NULL;
    void *at = room_in_a_plot(ground, bytes, kind, &plot);
    if (at != NULL) {
        ground->counted.allocations++;
        ground->counted.asked += bytes;
        ground->counted.given += plot->stride;
        ground->counted.by_kind[(unsigned)kind & 3u]++;
        ground->counted.by_width[plot->width_index >= WIDTH_COUNT
                                     ? WIDTH_COUNT
                                     : plot->width_index]++;
    }
    return at;
}

KestGroundKind kest_ground_kind(const KestGround *ground, const void *at) {
    if (ground == NULL) {
        return KEST_GROUND_PLAIN;
    }
    const Plot *plot = plot_holding(ground, at);
    if (plot == NULL) {
        return KEST_GROUND_PLAIN;
    }
    return kind_of(plot, place_of(plot, at));
}

// A place of at least `bytes`, marked as holding `kind`, and the plot it is
// in: the one asking has no need to look up what this has just chosen.
static void *room_in_a_plot(KestGround *ground, size_t bytes,
                            KestGroundKind kind, Plot **held) {
    if (ground == NULL || bytes == 0) {
        return NULL;
    }
    if (bytes > WIDEST) {
        size_t want = (bytes + PLOT - 1) & ~(size_t)(PLOT - 1);
        if (!room_for(ground, want)) {
            return NULL;
        }
        Plot *plot = new_plot(ground, WIDTH_COUNT, want);
        if (plot == NULL) {
            return NULL;
        }
        ground->refused = 0;
        ground->refused_by_ceiling = false;
        plot->used[0] = 1;
        say_kind(plot, 0, kind);
        plot->taken = 1;
        plot->reached = 1;
        ground->used += plot->stride;
        ground->since += plot->stride;
        ground->taken += plot->stride;
        OPEN(plot->data, bytes);
        memset(plot->data, 0, bytes);
        if (!remember(ground, plot->data)) {
            give_back(ground, plot, 0);
            return NULL;
        }
        *held = plot;
        return plot->data;
    }
    size_t which = 0;
    while (WIDTHS[which] < bytes) {
        which++;
    }
    // Asked before a place is looked for rather than only where a plot is
    // made, because the place a full world has room for is one already
    // standing empty in a plot the host has already given.
    if (!room_for(ground, WIDTHS[which])) {
        return NULL;
    }
    Plot *plot = ground->free_plots[which];
    while (plot != NULL && plot->taken == plot->places) {
        plot->listed_free = false;
        ground->free_plots[which] = plot->next_free;
        plot = plot->next_free;
    }
    if (plot == NULL) {
        // Enough places that a plot is worth asking the host for, and never
        // so much memory that a program with a small ceiling cannot have one.
        size_t many = PLOT_MOST / WIDTHS[which];
        if (many > PER_PLOT) {
            many = PER_PLOT;
        }
        if (many == 0) {
            many = 1;
        }
        size_t span = many * WIDTHS[which];
        span = (span + PLOT - 1) & ~(size_t)(PLOT - 1);
        plot = new_plot(ground, which, span);
        if (plot == NULL) {
            return NULL;
        }
        joined_free(ground, plot);
    }
    uint32_t place;
    if (plot->reached < plot->places) {
        place = plot->reached++;
    } else {
        place = free_place(plot, plot->hint);
        if (place >= plot->places) {
            // The hint is where a search last stopped, not a promise: a place
            // given back behind it is one only a search from the front finds.
            place = free_place(plot, 0);
        }
        if (place >= plot->places) {
            return NULL;
        }
        plot->hint = place + 1;
    }
    ground->refused = 0;
    ground->refused_by_ceiling = false;
    plot->used[place / 64] |= (uint64_t)1 << (place % 64);
    say_kind(plot, place, kind);
    plot->taken++;
    if (plot->taken == plot->places && plot->listed_free) {
        plot->listed_free = false;
        ground->free_plots[which] = plot->next_free;
    }
    ground->used += plot->stride;
    ground->since += plot->stride;
    ground->taken += plot->stride;
    unsigned char *at = plot->data + (size_t)place * plot->stride;
    OPEN(at, bytes);
    memset(at, 0, bytes);
    if (!remember(ground, at)) {
        give_back(ground, plot, place);
        return NULL;
    }
    *held = plot;
    return at;
}

void *kest_ground_grow(KestGround *ground, void *was, size_t had, size_t want) {
    if (ground == NULL || was == NULL || want <= had) {
        return was;
    }
    Plot *plot = plot_holding(ground, was);
    if (plot == NULL) {
        return NULL;
    }
    if (plot->data + (size_t)place_of(plot, was) * plot->stride != was) {
        return NULL;
    }
    if (want > plot->stride) {
        return NULL;
    }
    // What a place holds past what was asked for is nought, because that is
    // what it was handed out as, and growing into it must not turn up what a
    // value that used to be there left behind.
    OPEN((unsigned char *)was + had, want - had);
    memset((unsigned char *)was + had, 0, want - had);
    ground->counted.grown++;
    return was;
}

size_t kest_ground_room(const KestGround *ground, const void *at) {
    const Plot *plot = plot_holding(ground, at);
    return plot == NULL ? 0 : plot->stride;
}

bool kest_ground_holds(const KestGround *ground, const void *at) {
    if (ground == NULL) {
        return false;
    }
    const Plot *plot = plot_holding(ground, at);
    if (plot == NULL) {
        return false;
    }
    uint32_t place = place_of(plot, at);
    return (plot->used[place / 64] & ((uint64_t)1 << (place % 64))) != 0;
}

bool kest_ground_reached(KestGround *ground, const void *at, void **start,
                         KestGroundKind *kind) {
    if (start != NULL) {
        *start = NULL;
    }
    if (kind != NULL) {
        *kind = KEST_GROUND_PLAIN;
    }
    if (ground == NULL) {
        return false;
    }
    Plot *plot = plot_holding(ground, at);
    if (plot == NULL) {
        return false;
    }
    uint32_t place = place_of(plot, at);
    uint64_t bit = (uint64_t)1 << (place % 64);
    if ((plot->used[place / 64] & bit) == 0) {
        return false;
    }
    if ((plot->marks[place / 64] & bit) != 0) {
        return false;
    }
    plot->marks[place / 64] |= bit;
    if (start != NULL) {
        *start = plot->data + (size_t)place * plot->stride;
    }
    if (kind != NULL) {
        *kind = kind_of(plot, place);
    }
    return true;
}

#if KEST_CHECKED
static void holds_together(const KestGround *ground, const char *after) {
    size_t given = 0;
    size_t places = 0;
    for (const Plot *plot = ground->plots; plot != NULL; plot = plot->next) {
        uint32_t in_use = 0;
        for (uint32_t which = 0; which < plot->places; which++) {
            if ((plot->used[which / 64] & ((uint64_t)1 << (which % 64))) != 0) {
                in_use++;
            }
        }
        if (in_use != plot->taken) {
            fprintf(stderr,
                    "kest: after %s a plot says %u of its places are in use "
                    "and %u of them are\n",
                    after, plot->taken, in_use);
            abort();
        }
        given += (size_t)in_use * plot->stride;
        places += in_use;
    }
    if (given != ground->used) {
        fprintf(stderr,
                "kest: after %s this ground says it is holding %zu of the %zu "
                "its plots gave away, over %zu place(s)\n",
                after, ground->used, given, places);
        abort();
    }
}
#endif

void kest_ground_sweep(KestGround *ground) {
    if (ground == NULL || ground->block_count != 0) {
        return;
    }
    size_t held = ground->used;
    ground->counted.sweeps++;
    Plot **link = &ground->plots;
    Plot *plot = ground->plots;
    while (plot != NULL) {
        Plot *next = plot->next;
        for (uint32_t word = 0; word * 64 < plot->places; word++) {
            uint64_t dead = plot->used[word] & ~plot->marks[word];
            while (dead != 0) {
                uint32_t bit = lowest_set(dead);
                dead &= dead - 1;
                give_back(ground, plot, word * 64 + bit);
            }
            plot->marks[word] = 0;
        }
        // A plot nothing is in goes back to the host rather than being kept
        // for a program that may never ask again: a world that grew once and
        // shrank is a world whose memory the host should have back.
        if (plot->taken == 0) {
            *link = next;
            if (plot->listed_free) {
                Plot **free_link = &ground->free_plots[plot->width_index];
                while (*free_link != NULL && *free_link != plot) {
                    free_link = &(*free_link)->next_free;
                }
                if (*free_link == plot) {
                    *free_link = plot->next_free;
                }
            }
            forget_plot(ground, plot);
            OPEN(plot->data, plot->bytes);
            GROUND_FREE(plot->data);
            free(plot);
            ground->counted.plots_freed++;
        } else {
            plot->hint = 0;
            joined_free(ground, plot);
            link = &plot->next;
        }
        plot = next;
    }
    ground->counted.reclaimed += held - ground->used;
    ground->since = 0;
#if KEST_CHECKED
    holds_together(ground, "a sweep");
#endif
}

void kest_ground_empty(KestGround *ground) {
    if (ground == NULL) {
        return;
    }
    Plot *plot = ground->plots;
    while (plot != NULL) {
        Plot *next = plot->next;
        forget_plot(ground, plot);
        OPEN(plot->data, plot->bytes);
        GROUND_FREE(plot->data);
        free(plot);
        plot = next;
    }
    ground->plots = NULL;
    for (size_t which = 0; which < WIDTH_COUNT; which++) {
        ground->free_plots[which] = NULL;
    }
    ground->used = 0;
    ground->since = 0;
    ground->taken = 0;
    // A ground with nothing on it has refused nobody. A host raises a ceiling
    // by what the last allocation asked for, and one raised by a number about
    // a heap that has gone is raised for nothing. See D826.
    ground->refused = 0;
    ground->refused_by_ceiling = false;
    for (uint32_t open = 0; open < ground->block_count; open++) {
        ground->blocks[open].count = 0;
    }
    ground->block_count = 0;
}

void kest_ground_unmark(KestGround *ground) {
    if (ground == NULL) {
        return;
    }
    for (Plot *plot = ground->plots; plot != NULL; plot = plot->next) {
        memset(plot->marks, 0, sizeof(plot->marks));
    }
}

bool kest_ground_open(KestGround *ground) {
    if (ground == NULL) {
        return false;
    }
    if (ground->block_count == ground->block_room) {
        uint32_t bigger = ground->block_room == 0 ? 8 : ground->block_room * 2;
        struct Block *grown =
            realloc(ground->blocks, (size_t)bigger * sizeof(struct Block));
        if (grown == NULL) {
            return false;
        }
        memset(grown + ground->block_room, 0,
               (size_t)(bigger - ground->block_room) * sizeof(struct Block));
        /* Counted where the block is opened rather than here, which is only
           the list growing. */
        ground->blocks = grown;
        ground->block_room = bigger;
    }
    ground->blocks[ground->block_count].count = 0;
    ground->block_count++;
    ground->counted.blocks++;
    return true;
}

void kest_ground_close(KestGround *ground) {
    if (ground == NULL || ground->block_count == 0) {
        return;
    }
    struct Block *block = &ground->blocks[--ground->block_count];
    for (size_t i = 0; i < block->count; i++) {
        Plot *plot = plot_holding(ground, block->took[i]);
        if (plot != NULL) {
            give_back(ground, plot, place_of(plot, block->took[i]));
            if (plot->taken == 0 && plot->width_index >= WIDTH_COUNT) {
                // A wide plot holds one thing, so one gone is the whole of it.
                Plot **link = &ground->plots;
                while (*link != NULL && *link != plot) {
                    link = &(*link)->next;
                }
                if (*link == plot) {
                    *link = plot->next;
                }
                forget_plot(ground, plot);
                OPEN(plot->data, plot->bytes);
                GROUND_FREE(plot->data);
                free(plot);
            }
        }
    }
    block->count = 0;
#if KEST_CHECKED
    holds_together(ground, "a block of working memory closing");
#endif
}

uint32_t kest_ground_open_count(const KestGround *ground) {
    return ground == NULL ? 0 : ground->block_count;
}

size_t kest_ground_used(const KestGround *ground) {
    return ground == NULL ? 0 : ground->used;
}

size_t kest_ground_taken(const KestGround *ground) {
    return ground == NULL ? 0 : ground->taken;
}

size_t kest_ground_since(const KestGround *ground) {
    return ground == NULL ? 0 : ground->since;
}

void kest_ground_cap(KestGround *ground, size_t bytes) {
    if (ground != NULL) {
        ground->ceiling = bytes;
    }
}

size_t kest_ground_refused(const KestGround *ground) {
    return ground == NULL ? 0 : ground->refused;
}

bool kest_ground_refused_by_ceiling(const KestGround *ground) {
    return ground != NULL && ground->refused_by_ceiling;
}

void kest_ground_counted(const KestGround *ground, KestGroundCounts *into) {
    if (into == NULL) {
        return;
    }
    if (ground == NULL) {
        memset(into, 0, sizeof *into);
        return;
    }
    into->allocations = ground->counted.allocations;
    into->asked = ground->counted.asked;
    into->given = ground->counted.given;
    into->grown = ground->counted.grown;
    into->sweeps = ground->counted.sweeps;
    into->reclaimed = ground->counted.reclaimed;
    into->plots_made = ground->counted.plots_made;
    into->plots_freed = ground->counted.plots_freed;
    into->blocks = ground->counted.blocks;
}
