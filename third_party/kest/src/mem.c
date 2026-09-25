#include "mem.h"

// And the one thing outside this file that wants the answer `mem.h` works out.
// It lives here rather than beside `kest_version`, where a reader would look
// for it, because `src/kest.c` is above this file in the pipeline and a file
// may not reach down — and what it answers is this file's own reading of the
// compiler. A host outside the tree cannot include `mem.h`, and a host inside
// it should not have to: what it wants to know is about the library it is
// linked against and not about its own build. See D828.
#include "kest.h"

bool kest_checked(void) {
    return KEST_CHECKED != 0;
}

#include <stdbool.h>
// Only the sanitised build says anything, and only when this arena has stopped
// agreeing with itself. The release build includes nothing but what it uses.
#if KEST_CHECKED
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

// A block is one allocation as far as the host is concerned, so reading one
// element past the end of something inside it is memory this arena owns and
// nothing anywhere says a word about it. The sanitised build is told instead:
// a block is poisoned when it is taken, each allocation is opened to its own
// size, and a gap is left after it that stays poisoned. Off the end of a thing
// is the read this project has got wrong before, and this is what makes it
// visible.
//
// The release build includes nothing but ISO C. This is a header of the
// sanitiser, in a build that is already standing on it.
#if KEST_CHECKED
#include <sanitizer/asan_interface.h>
#define KEPT_BACK 16
#define POISON(at, bytes) __asan_poison_memory_region((at), (bytes))
#define OPEN(at, bytes) __asan_unpoison_memory_region((at), (bytes))
#else
#define KEPT_BACK 0
#define POISON(at, bytes) ((void)(at), (void)(bytes))
#define OPEN(at, bytes) ((void)(at), (void)(bytes))
#endif

#define BLOCK_SIZE (64 * 1024)

typedef struct Block {
    struct Block *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
} Block;

struct KestArena {
    Block *head;
    // The one it started with, which is the one a reset keeps. Kept rather
    // than found by walking to the end of the list every time.
    Block *first;
    // The block that last answered `kest_arena_holds`. A host handing the same
    // handle over every frame asks about the same block every frame, and the
    // block it is in may be an old one — the walk would be as long as the
    // program has grown, at a crossing that happens every frame.
    Block *recent;
    // What all the blocks together sit between. A pointer outside it belongs
    // to somebody else and is refused without a walk, which is what a host's
    // own string is. It widens and never narrows while blocks are added,
    // because a bound too wide costs a walk and a bound too narrow is wrong.
    const unsigned char *low;
    const unsigned char *high;
    // Kept rather than counted, because a ceiling is asked about at every
    // allocation and walking the blocks to answer would make an arena slower
    // the longer a program runs.
    size_t handed;
    // And every byte this has ever handed out, which a rewind does not take
    // back: what something cost is a difference of two readings of it.
    size_t taken;
    // And what was handed out on its behalf by an arena that has since been
    // given back: the tokens a file is read into live in one of those. A
    // ceiling is refused against both, because what a build asked the host for
    // is the same number whether it kept it or not. See D747.
    size_t also;
    // And how many of those have been given back. What is asked for and what
    // is held apart by exactly this: an arena taken on another's behalf is
    // charged where it grows, because a ceiling refuses there, and returned
    // where it is freed, because that is when the host has the room again.
    // See D748.
    size_t returned;
    // How many times it has handed something out. What a block has given away
    // is what was asked for plus the gap the sanitised build keeps after it,
    // so the two numbers agree only when the number of gaps is known. Kept for
    // that and read nowhere else.
    size_t allocations;
    size_t ceiling;
    // The arena this one is taken under, whose ceiling holds it too; what the
    // arenas taken under this one hold now, which its ceiling counts beside
    // its own; and the most this one and everything under it ever held at
    // once, which is what a build given exactly what it cost has to have had
    // room for. See D1247.
    KestArena *under;
    size_t beneath;
    size_t widest;
    // And the most the arenas under this one held at once, which is what a
    // build needed on top of what it keeps: what it holds afterwards grows
    // with what is asked of it, and this does not. See D1247.
    size_t most_beneath;
    // What the last allocation this arena refused was asking for. A ceiling
    // stops a program at the allocation that would have crossed it, so what a
    // host reads afterwards is a total that stopped short — and the difference
    // between missing by eight bytes and missing by a megabyte is the whole of
    // what a host does about it. See D248.
    size_t refused;
    // And what was held when a ceiling refused it, counting what was held
    // under it then: what is held afterwards may be less, once a stage that
    // worked in memory of its own has given it back. See D1247.
    size_t refused_holding;
    // And which of the two refused it: the ceiling above, or the host with
    // nothing left. Read beside the number, because a refusal of nought bytes
    // is not a thing that happens. See D321.
    bool refused_by_ceiling;
};

// The four things this arena keeps rather than works out: the block it started
// with, the one that answered last, and what all of them sit between. Every one
// of them is a shortcut, and a shortcut that stops being true is a reset
// keeping the wrong block or a pointer refused because it fell outside a bound
// that had not widened. Nothing about a program's behaviour would say so.
//
// So the sanitised build says so, where this arena already does its other
// work of saying what it handed out. It is a walk of the blocks, which is
// exactly the walk everything above is written to avoid, and that is why it is
// here and not in a build anybody runs a frame in.
#if KEST_CHECKED
static void holds_together(const KestArena *arena, const char *after) {
    bool listed = false;
    const Block *last = NULL;
    size_t given = 0;
    for (const Block *block = arena->head; block != NULL; block = block->next) {
        given += block->used;
        if (block == arena->recent) {
            listed = true;
        }
        if (block->data < arena->low ||
            block->data + block->capacity > arena->high) {
            fprintf(stderr,
                    "kest: after %s a block sits outside what the arena says "
                    "its blocks sit between\n",
                    after);
            abort();
        }
        last = block;
    }
    if (last != arena->first) {
        fprintf(stderr,
                "kest: after %s the block this arena started with is not the "
                "one its list ends at\n",
                after);
        abort();
    }
    if (!listed) {
        fprintf(stderr,
                "kest: after %s the block that answered last is not one of "
                "this arena's\n",
                after);
        abort();
    }
    // What it says it has handed out against what its blocks have given away.
    // They differ by the gap kept after each allocation and by nothing else:
    // the padding before one is counted as handed out, because a hole a block
    // is left with has been given to nobody and cannot be given to anybody.
    // A ceiling is refused against this number, so a number that has drifted
    // is a program stopped early or let past what a host allowed it.
    if (given != arena->handed + KEPT_BACK * arena->allocations) {
        fprintf(stderr,
                "kest: after %s this arena says it handed out %zu of the %zu "
                "its blocks gave away, in %zu allocations\n",
                after, arena->handed, given, arena->allocations);
        abort();
    }
}
// And what it hands out: nought, every time. A block is taken zeroed, a reset
// clears what had been handed out of the one it keeps, and what an extension
// gains is cleared where it is gained — three places, each of which is the
// promise and none of which is the whole of it. What reads an allocation
// expecting nought is everything above this file: a header whose unwritten
// fields are noughts, a length nobody has set yet, a slot nobody has stored to.
static void arrives_as_nought(const unsigned char *at, size_t size,
                              const char *from) {
    for (size_t i = 0; i < size; i++) {
        if (at[i] != 0) {
            fprintf(stderr,
                    "kest: %s handed out %zu bytes and byte %zu of them was "
                    "not nought\n",
                    from, size, i);
            abort();
        }
    }
}
#else
#define holds_together(arena, after) ((void)(arena), (void)(after))
#define arrives_as_nought(at, size, from)                                      \
    ((void)(at), (void)(size), (void)(from))
#endif

static Block *block_new(size_t capacity) {
    Block *block = calloc(1, sizeof(Block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->capacity = capacity;
    POISON(block->data, capacity);
    return block;
}

// Whether any arena has refused anything since this build began, defined
// with the rest of what refusals keep further down.
static bool anybody_refused;

// What an arena holds with what is taken under it, which is what its ceiling
// is asked about.
static size_t holding(const KestArena *arena) {
    return arena->handed + arena->also + arena->beneath;
}

// Whether handing out `taking` more crosses a ceiling: this arena's own, or,
// for one taken under another, the other's with what everything under it
// holds. Said as a refusal by the ceiling where it is, and where the other
// one is too, because what a host reads is the build's.
static bool crosses(KestArena *arena, size_t taking) {
    bool over = arena->ceiling != 0 && holding(arena) + taking > arena->ceiling;
    KestArena *under = arena->under;
    if (!over && under != NULL && under->ceiling != 0 &&
        holding(under) + taking > under->ceiling) {
        under->refused = taking;
        under->refused_by_ceiling = true;
        under->refused_holding = holding(under);
        over = true;
    }
    if (over) {
        arena->refused = taking;
        arena->refused_by_ceiling = true;
        arena->refused_holding = holding(arena);
        anybody_refused = true;
    }
    return over;
}

static void widened(KestArena *arena) {
    if (holding(arena) > arena->widest) {
        arena->widest = holding(arena);
    }
}

// What this arena holds went up by `by` or down by `lost`, which is what the
// one it is taken under counts beneath it.
static void grew(KestArena *arena, size_t by) {
    widened(arena);
    if (arena->under != NULL) {
        arena->under->beneath += by;
        if (arena->under->beneath > arena->under->most_beneath) {
            arena->under->most_beneath = arena->under->beneath;
        }
        widened(arena->under);
    }
}

static void shrank(KestArena *arena, size_t lost) {
    if (arena->under != NULL) {
        arena->under->beneath -= lost;
    }
}

#if KEST_CHECKED
// And one arena, counted apart: taking an arena is asking the host for its
// first block, and the refusals above are aimed at what an arena hands out,
// so every stage that takes an arena of its own -- the verifier, the bodies,
// a machine -- had never been seen told no. `KEST_REFUSE_ARENA=n` refuses the
// n-th, in the build that checks itself only. See D1251.
static uint64_t arenas_so_far;

static bool refuse_this_arena(void) {
    static uint64_t refuse_arena_at;
    static bool asked;
    if (!asked) {
        const char *said = getenv("KEST_REFUSE_ARENA");
        refuse_arena_at = said == NULL ? 0 : strtoull(said, NULL, 10);
        asked = true;
    }
    arenas_so_far++;
    return refuse_arena_at != 0 && arenas_so_far == refuse_arena_at;
}
#endif

KestArena *kest_arena_new(void) {
#if KEST_CHECKED
    if (refuse_this_arena()) {
        return NULL;
    }
#endif
    KestArena *arena = calloc(1, sizeof(KestArena));
    if (arena == NULL) {
        return NULL;
    }
    arena->head = block_new(BLOCK_SIZE);
    if (arena->head == NULL) {
        free(arena);
        return NULL;
    }
    arena->first = arena->head;
    arena->recent = arena->head;
    arena->low = arena->head->data;
    arena->high = arena->head->data + arena->head->capacity;
    holds_together(arena, "starting");
    return arena;
}

KestArena *kest_arena_new_under(KestArena *under) {
    KestArena *arena = kest_arena_new();
    if (arena != NULL) {
        arena->under = under;
    }
    return arena;
}

void kest_arena_free(KestArena *arena) {
    if (arena == NULL) {
        return;
    }
    // What it holds goes back from what the one it was under counts.
    shrank(arena, arena->handed);
    Block *block = arena->head;
    while (block != NULL) {
        Block *next = block->next;
        // Given back the way it was taken: memory left poisoned is memory the
        // host may hand out again and be told about.
        OPEN(block->data, block->capacity);
        free(block);
        block = next;
    }
    free(arena);
}

// Whether this block handed out that address. Below what was handed out rather
// than below what the block holds: a pointer into the part nobody has been
// given is a pointer this arena has not given anybody.
static bool block_holds(const Block *block, const unsigned char *address) {
    size_t reach = block->used < block->capacity ? block->used : block->capacity;
    return address >= block->data && address < block->data + reach;
}

bool kest_arena_holds(KestArena *arena, const void *at) {
    if (arena == NULL || at == NULL) {
        return false;
    }
    const unsigned char *address = at;
    // Outside all of them, which is where a host's own pointer is, and it is
    // two comparisons rather than a walk.
    if (address < arena->low || address >= arena->high) {
        return false;
    }
    if (arena->recent != NULL && block_holds(arena->recent, address)) {
        return true;
    }
    for (Block *block = arena->head; block != NULL; block = block->next) {
        if (block_holds(block, address)) {
            // Asked once is asked again: the same handle crosses every frame.
            arena->recent = block;
            return true;
        }
    }
    return false;
}

void kest_arena_reset(KestArena *arena) {
    if (arena == NULL) {
        return;
    }
    // The block this arena started with is the one it keeps, because it is
    // the one that is always there and always the same size. The rest are
    // what a program grew into and what it is being asked to give back.
    Block *first = arena->first;
    Block *block = arena->head;
    while (block != first) {
        Block *next = block->next;
        OPEN(block->data, block->capacity);
        free(block);
        block = next;
    }
    first->next = NULL;
    // Only what was handed out of it, because the rest was never written to
    // and an allocation is promised memory that is nought. Clearing a whole
    // block to give back a hundred bytes is the reset costing more than the
    // work it is undoing.
    //
    // What was handed out can reach past the end of the block: the sanitised
    // build counts a gap into that number so that nothing is handed out beside
    // anything else, and there is no memory there to clear.
    OPEN(first->data, first->used);
    memset(first->data, 0,
           first->used < first->capacity ? first->used : first->capacity);
    POISON(first->data, first->capacity);
    first->used = 0;
    arena->head = first;
    arena->recent = first;
    arena->low = first->data;
    arena->high = first->data + first->capacity;
    shrank(arena, arena->handed);
    arena->handed = 0;
    arena->allocations = 0;
    // A new heap has refused nobody.
    arena->refused = 0;
    arena->refused_by_ceiling = false;
    holds_together(arena, "a reset");
}

KestMark kest_arena_mark(const KestArena *arena) {
    KestMark mark = {arena->head, arena->head->used, arena->handed,
                     arena->allocations};
    return mark;
}

void kest_arena_rewind(KestArena *arena, KestMark mark) {
    Block *until = mark.block;
    // Blocks taken since the mark go back to the machine underneath. A walk
    // that needed a block of its own is a walk whose block is worth giving
    // back: keeping it would make a refusal cost the program a block it never
    // asked for, which is the thing this is here to stop.
    Block *block = arena->head;
    while (block != until) {
        Block *next = block->next;
        OPEN(block->data, block->capacity);
        free(block);
        block = next;
    }
    // Only what was handed out since the mark, for the reason a reset clears
    // only what was handed out: an allocation is promised memory that is
    // nought, and clearing a whole block to give back a hundred bytes is the
    // undoing costing more than the work.
    size_t to = until->used < until->capacity ? until->used : until->capacity;
    size_t from = mark.used < to ? mark.used : to;
    OPEN(until->data + from, to - from);
    memset(until->data + from, 0, to - from);
    POISON(until->data + from, until->capacity - from);
    until->used = mark.used;
    arena->head = until;
    // The block that answered last may have been one of the ones just given
    // back, and a shortcut pointing at freed memory is worse than no shortcut.
    arena->recent = until;
    shrank(arena, arena->handed - mark.handed);
    arena->handed = mark.handed;
    arena->allocations = mark.allocations;
    holds_together(arena, "a rewind");
}

#if KEST_CHECKED
// Which allocation to refuse, counted over every arena this process makes. A
// compiler that runs out of room is easy to watch and hard to aim at: which
// message a half-built program gives depends on exactly which allocation
// failed, so the same program at neighbouring ceilings says different things
// and nothing can be asked of it twice. Counted here, in the build that checks
// itself and only when somebody says where, so the release build is the
// release build and a run nobody aimed at is the run it always was.
//
// It is the only way to ask a compiler what it says when it has nothing left
// and mean a particular nothing. See D880.
static uint64_t allocations_so_far;
static uint64_t refuse_at;
static bool refuse_asked;

static bool refuse_this_one(void) {
    if (!refuse_asked) {
        const char *said = getenv("KEST_REFUSE_AT");
        refuse_at = said == NULL ? 0 : strtoull(said, NULL, 10);
        refuse_asked = true;
    }
    allocations_so_far++;
    return refuse_at != 0 && allocations_so_far >= refuse_at;
}
#endif

// Whether anything has been refused since the last build opened. One bit,
// because that is what a reader needs: not which arena ran out but that one
// did. See D880.
static bool anybody_refused;



bool kest_arena_refused_anywhere(void) {
    return anybody_refused;
}

void kest_arena_forget_refusals(void) {
    anybody_refused = false;
}

void *kest_arena_alloc(KestArena *arena, size_t size, size_t align) {
    size_t offset = (arena->head->used + align - 1) & ~(align - 1);
    bool fresh = offset + size > arena->head->capacity;
    // What this costs, which is the padding as well as the size: a block that
    // is left with a hole in it has handed that hole out to nobody.
    size_t taking = fresh ? size : offset + size - arena->head->used;
#if KEST_CHECKED
    // The one somebody aimed at, refused the way the host would have refused
    // it: not by a ceiling, because nobody set one, and so said with the
    // sentence a machine with nothing left says.
    if (refuse_this_one()) {
        arena->refused = taking;
        arena->refused_by_ceiling = false;
        anybody_refused = true;
        return NULL;
    }
#endif
    // Asked before a block is taken from the host, so a refusal costs nothing.
    if (crosses(arena, taking)) {
        return NULL;
    }
    if (fresh) {
        size_t capacity = size > BLOCK_SIZE ? size : BLOCK_SIZE;
        Block *block = block_new(capacity);
        if (block == NULL) {
            // The other way to be refused, and the same number either way:
            // what the allocation that failed was asking for. The block the
            // host would not give may be bigger than that — a block is at
            // least what a block is — but what a reader wants is what was
            // being made when this happened. See D320.
            arena->refused = taking;
            arena->refused_by_ceiling = false;
            anybody_refused = true;
            return NULL;
        }
        block->next = arena->head;
        arena->head = block;
        if (block->data < arena->low) {
            arena->low = block->data;
        }
        if (block->data + block->capacity > arena->high) {
            arena->high = block->data + block->capacity;
        }
        offset = 0;
    }
    void *result = arena->head->data + offset;
    // The gap is not handed to anybody, so it is not counted as handed out:
    // what a program is told it used is the same number in both builds, and so
    // is what a ceiling refuses.
    arena->head->used = offset + size + KEPT_BACK;
    arena->handed += taking;
    arena->taken += taking;
    grew(arena, taking);
    arena->allocations++;
    OPEN(result, size);
    arrives_as_nought(result, size, "an allocation");
    holds_together(arena, "an allocation");
    return result;
}

void *kest_arena_extend(KestArena *arena, void *last, size_t was,
                        size_t want) {
    if (arena == NULL || last == NULL || want <= was) {
        return NULL;
    }
    Block *block = arena->head;
    unsigned char *end = (unsigned char *)last + was;
    // The last thing handed out is the one the block ends at, gap and all.
    // Anything else has something after it, and moving that is not what this
    // is for.
    if (end + KEPT_BACK != block->data + block->used) {
        return NULL;
    }
    size_t offset = (size_t)((unsigned char *)last - block->data);
    size_t taking = want - was;
    if (crosses(arena, taking)) {
        return NULL;
    }
    if (offset + want + KEPT_BACK <= block->capacity) {
        block->used = offset + want + KEPT_BACK;
        arena->handed += taking;
        arena->taken += taking;
        grew(arena, taking);
        // What was the gap is now part of the thing, and the gap moves to the
        // end of it.
        OPEN(end, taking);
        arrives_as_nought(end, taking, "an extension");
        POISON((unsigned char *)last + want, KEPT_BACK);
        return last;
    }
    // A thing too big for a block of its own size gets one, so anything over
    // that size is alone in its block. Then the host can be asked for a bigger
    // block instead of a second one: nothing else is in it to move, and what
    // the host gets back is the block a copy would have left behind.
    if (offset != 0) {
        return NULL;
    }
    OPEN(block->data, block->capacity);
    Block *bigger = realloc(block, sizeof(Block) + want + KEPT_BACK);
    if (bigger == NULL) {
        POISON(block->data + was, block->capacity - was);
        return NULL;
    }
    // What it gains is nought, because that is what everything handed out is.
    memset(bigger->data + was, 0, want + KEPT_BACK - was);
    bigger->capacity = want + KEPT_BACK;
    bigger->used = want + KEPT_BACK;
    arena->head = bigger;
    // The host moved it, so everything that named it by where it was names
    // somewhere else now.
    if (block == arena->first) {
        arena->first = bigger;
    }
    arena->recent = bigger;
    if (bigger->data < arena->low) {
        arena->low = bigger->data;
    }
    if (bigger->data + bigger->capacity > arena->high) {
        arena->high = bigger->data + bigger->capacity;
    }
    arena->handed += taking;
    arena->taken += taking;
    grew(arena, taking);
    POISON(bigger->data + want, KEPT_BACK);
    arrives_as_nought(bigger->data + was, want - was, "a block the host moved");
    // After what it was given is counted, and not before: a check of the two
    // numbers against each other is a check of both of them.
    holds_together(arena, "a block the host moved");
    return bigger->data;
}

size_t kest_arena_refused(const KestArena *arena) {
    return arena == NULL ? 0 : arena->refused;
}

void kest_arena_also_refused(KestArena *arena, const KestArena *other) {
    if (arena == NULL || other == NULL || other->refused == 0) {
        return;
    }
    arena->refused = other->refused;
    arena->refused_by_ceiling = other->refused_by_ceiling;
}

size_t kest_arena_ceiling(const KestArena *arena) {
    return arena == NULL ? 0 : arena->ceiling;
}

bool kest_arena_refused_by_ceiling(const KestArena *arena) {
    return arena != NULL && arena->refused_by_ceiling;
}

size_t kest_arena_used(const KestArena *arena) {
    return arena->handed + arena->also;
}

size_t kest_arena_askings(const KestArena *arena) {
    return arena->allocations;
}

size_t kest_arena_taken(const KestArena *arena) {
    return arena->taken;
}

size_t kest_arena_held(const KestArena *arena) {
    return arena->handed + arena->also - arena->returned;
}

void kest_arena_charge(KestArena *arena, size_t bytes) {
    arena->also += bytes;
    widened(arena);
}

void kest_arena_returned(KestArena *arena, size_t bytes) {
    arena->returned += bytes;
}

size_t kest_arena_ceiling_left(const KestArena *arena) {
    if (arena->ceiling == 0) {
        return 0;
    }
    size_t used = holding(arena);
    // One rather than nought for an arena already at its ceiling, because
    // nought is what an arena with no ceiling says and the two are not the
    // same thing: a scratch that may have nothing is not a scratch that may
    // have everything.
    return arena->ceiling > used ? arena->ceiling - used : 1;
}

void kest_arena_cap(KestArena *arena, size_t bytes) {
    arena->ceiling = bytes;
}

char *kest_arena_strndup(KestArena *arena, const char *text, size_t len) {
    char *copy = kest_arena_alloc(arena, len + 1, 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

size_t kest_arena_widest(const KestArena *arena) {
    size_t kept = arena->handed + arena->also + arena->most_beneath;
    return arena->widest > kept ? arena->widest : kept;
}

size_t kest_arena_refused_holding(const KestArena *arena) {
    return arena->refused_holding;
}

size_t kest_arena_most_beneath(const KestArena *arena) {
    return arena->most_beneath;
}

