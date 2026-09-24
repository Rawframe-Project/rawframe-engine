#ifndef KEST_GROUND_H
#define KEST_GROUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// What a running program's values stand on, and the one thing the arena under
// them could not do: give a piece back without giving all of it back.
//
// An arena is the right shape for a compiler, where nothing outlives the
// compilation, and the wrong shape for a world that is kept: a program that
// writes a new name into a live thing every round abandons the old one, and an
// arena abandons it forever. Two hundred things over ten thousand rounds is
// two hundred megabytes of names nothing can reach, and a world with two
// hundred things in it is not a world that needs two hundred megabytes. See
// D996.
//
// So this is a heap with places in it that can be had again. What decides
// which is a walk from what the machine can still reach — the machine's slots
// and the worlds it holds — rather than a count kept on each value, because a
// count on each value is a cost paid by every program on every copy for the
// sake of the ones that churn, and because what a slot holds is not always the
// start of the thing it names: a piece of text cut out of another names a
// place inside it, and so does the address of an element. A walk finds the
// thing an inside place is inside of; a count could not have been kept on one.
typedef struct KestGround KestGround;

// What a place holds, which a walk has to know before it reads it: a pointer
// found in a slot is eight bytes that look like an address, and what is at
// that address is whatever the machine put there. Guessing by reading a tag
// out of the thing itself would read a piece of text as a handle the first
// time four bytes of somebody's name spelled one.
typedef enum {
    // Bytes with nothing in them to follow: a piece of text, and the runs a
    // world keeps beside its places.
    KEST_GROUND_PLAIN,
    // An `Array` header.
    KEST_GROUND_ARRAY,
    // The elements of one, which say what they are at the front of themselves
    // so a walk that met them without meeting the header can still read them.
    KEST_GROUND_ELEMS,
    // A `Store` header.
    KEST_GROUND_STORE,
} KestGroundKind;

// What this has done since it was made, for a host or a tool that asked. Each
// of them is at an allocation, a sweep or a plot rather than at an
// instruction, so they are always counted: a program runs millions of
// instructions between any two, where a counter at the top of the dispatch
// loop cost a third of the machine (D979). See D1007.
typedef struct {
    // Places handed out, what was asked for, and what the places they came
    // from are worth: `asked` against `given` is what the ladder of widths
    // costs, which is the one number nothing else here says.
    uint64_t allocations;
    uint64_t asked;
    uint64_t given;
    // Times something grew where it stood rather than moving, which is what a
    // run of bytes filled one element at a time wants.
    uint64_t grown;
    // Walks that swept, and what they gave back.
    uint64_t sweeps;
    uint64_t reclaimed;
    // Plots asked of the host and handed back to it.
    uint64_t plots_made;
    uint64_t plots_freed;
    // Blocks of working memory opened.
    uint64_t blocks;
} KestGroundCounts;

void kest_ground_counted(const KestGround *ground, KestGroundCounts *into);

// What the plots come to and how much of them is in places nothing is using.
// A non-moving heap gives a plot back to the host only when every place in it
// is free, so free places spread thinly over many plots are memory this is
// holding and cannot hand back. That is the one fragmentation figure a heap of
// this shape has, and it is asked for here rather than worked out from a plot
// size written down somewhere, because a plot holding one wide thing is as
// wide as that thing. See D1027.
typedef struct {
    uint64_t plots;
    // What the host gave for them, and what is in places that are handed out.
    uint64_t bytes;
    uint64_t taken;
    // Places altogether and places nobody is using, and what those come to.
    uint64_t places;
    uint64_t free_places;
    uint64_t free_bytes;
} KestGroundPlots;

void kest_ground_plots(const KestGround *ground, KestGroundPlots *into);

// Every allocation this has ever handed out, split by the width it was cut
// from and by what the place holds. `widths` gets the width in bytes and
// `taken` how many came from it, in the ladder's own order, with a last entry
// of nought for the places wider than the ladder -- each of those got a plot
// of its own and there is no one width to say. `kinds` gets four counts in the
// order of `KestGroundKind`. Any of the three may be NULL. Answers how many
// rungs it wrote.
//
// A total says how much a program asks for and this says what it asks for,
// which is a different question: a program that is all short pieces of text
// and one that is all wide runs have the same total and nothing else in
// common. See D1032.
uint32_t kest_ground_widths(const KestGround *ground, uint32_t *widths,
                            uint64_t *taken, uint32_t many, uint64_t *kinds);

KestGround *kest_ground_new(void);
void kest_ground_free(KestGround *ground);

// Room for one value, zeroed, or NULL when there is none. The alignment is the
// alignment every place has, which is sixteen: the machine asks for eight and
// for sixteen and nothing wider, so one alignment serves both and a place does
// not have to say which it is.
void *kest_ground_take(KestGround *ground, size_t bytes,
                       KestGroundKind kind);

// What the place an address is in holds, and PLAIN for an address this did not
// hand out — which is safe to read as nothing to follow, because it is.
KestGroundKind kest_ground_kind(const KestGround *ground, const void *at);

// Makes the last thing this handed out bigger where the place it is in has the
// room, which is what a run of bytes growing one element at a time wants: a
// place of two hundred and fifty-six bytes holds a hundred and twenty-eight
// before it has to move. Answers where it is now, which is where it was when
// the place had the room, and NULL when it did not — and then the caller does
// what it did before: takes a new one and copies.
void *kest_ground_grow(KestGround *ground, void *was, size_t had, size_t want);

// How wide the place a thing is in is, which is what it may grow into without
// moving. A run of bytes that asked for a hundred and thirty is in a place of
// a hundred and ninety-two, and a caller that fills the place rather than what
// it asked for is one that grows fewer times.
size_t kest_ground_room(const KestGround *ground, const void *at);

// Whether this handed out the address, which a machine asks of a pointer it
// was handed from outside: reading one it never gave out is reading whatever
// is at that address.
bool kest_ground_holds(const KestGround *ground, const void *at);

// Marks the thing an address is in, whether it is the start of it or a place
// inside it, and says where that thing starts and what kind it is. Answers
// whether it was not marked already: false for an address this did not hand
// out, and false for one already marked — so a walk that follows what it marks
// terminates on a ring without keeping a list of where it has been.
//
// One lookup rather than three. A walk asks all three of those about every
// address it follows, and each of them was a hash of the same address and a
// probe of the same table. See D1008.
bool kest_ground_reached(KestGround *ground, const void *at, void **start,
                         KestGroundKind *kind);

// Gives back every place nothing marked, and forgets the marks. What a place
// held is not read again, so a value that was there is gone as far as anything
// can see, and the place is handed out again to whatever asks next.
void kest_ground_sweep(KestGround *ground);

// The same ground, emptied: every place given back, every plot handed to the
// host, and nothing remembered about what it last refused. It is what a host
// throwing the heap away asks for, and it keeps the ground rather than making
// another because a machine that resets every frame would otherwise ask the
// host for the shape of one every frame.
void kest_ground_empty(KestGround *ground);

// Forgets the marks without giving anything back, which is what a walk that
// could not finish does: a sweep after a walk that stopped short would give
// away memory something can still reach, so the walk that stopped short takes
// nothing and leaves the ground as it found it.
void kest_ground_unmark(KestGround *ground);

// A block of working memory, which is the other way a place is given back: a
// `scratch { }` block opens one, everything the block takes is remembered
// against it, and closing it gives all of it back at once. Nothing in the
// block may outlive it — the checker refuses a block that grows what does, see
// D972 — so this is the same promise the arena's mark and rewind made, kept
// against a heap that is not a stack.
//
// Answers false when there is no room to remember what a block takes, which is
// the one way opening one fails.
bool kest_ground_open(KestGround *ground);
void kest_ground_close(KestGround *ground);
// How many are open, so a machine can put itself back if one is left open by a
// run that stopped in the middle.
uint32_t kest_ground_open_count(const KestGround *ground);

// What is in places that are handed out, which is what a ceiling is held
// against and what a host is told a program is holding. It goes down when a
// sweep gives places back, which is the whole of why this is here.
size_t kest_ground_used(const KestGround *ground);

// And every byte it has ever handed out, which a sweep does not take back:
// what a call cost is a difference of two readings of this, where the number
// above answers what is being held right now.
size_t kest_ground_taken(const KestGround *ground);

// How much has been taken since the last sweep, which is what says when the
// next one is worth doing.
size_t kest_ground_since(const KestGround *ground);

// The most this may ask the host for, over the places it holds. Nought is no
// ceiling, which is what it has until somebody says otherwise. Past it a take
// answers NULL, which is what every caller already handles.
void kest_ground_cap(KestGround *ground, size_t bytes);

// What the take this last refused was asking for, and nought when it has
// refused nothing. Read beside the number above by whatever says what
// happened, because a ceiling that stops a program says what it stopped it at.
size_t kest_ground_refused(const KestGround *ground);

// Whether the last refusal was the ceiling rather than the host, which are the
// same number and not the same thing to do anything about.
bool kest_ground_refused_by_ceiling(const KestGround *ground);

#endif
