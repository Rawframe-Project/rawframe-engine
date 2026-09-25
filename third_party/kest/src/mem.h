#ifndef KEST_MEM_H
#define KEST_MEM_H

// Whether this build checks itself. Several things here are shortcuts — an
// arena's bounds, an index over the names a program declares — and what says a
// shortcut is still true is a walk that costs more than the shortcut saves. So
// they are in the build that is already paying for that sort of thing, and
// nowhere else.
//
// Written once, because the compilers do not spell it the same: one defines a
// name and the other answers a question, and a file that only asked the first
// of them would compile under the second into a build with none of these
// checks in it and nothing to say so. What says so is `--version`, which reads
// this. See D330.
#if defined(__SANITIZE_ADDRESS__)
#define KEST_CHECKED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define KEST_CHECKED 1
#else
#define KEST_CHECKED 0
#endif
#else
#define KEST_CHECKED 0
#endif

#include <stdbool.h>
#include <stddef.h>

// A bump allocator for everything the compiler produces before the program
// runs: tokens, syntax nodes, types, diagnostic strings. None of it outlives
// compilation, so none of it is freed individually.
typedef struct KestArena KestArena;

KestArena *kest_arena_new(void);
void kest_arena_free(KestArena *arena);

// An arena taken under another: what it holds is counted beside what the
// other holds, against the other one's ceiling, for as long as it holds it. A
// stage that works in memory of its own and throws it away -- the bodies a
// backend writes, the verifier's tables -- is still inside what a host gave a
// build. See D1247.
KestArena *kest_arena_new_under(KestArena *under);

// What an arena holds, with the most everything taken under it ever held at
// once on top -- never less than the most the two held together: what a
// ceiling has to be for the same work to fit inside it again, and a number
// that still grows with what is asked of the arena after that work is done.
size_t kest_arena_widest(const KestArena *arena);

// The most the arenas taken under this one held at once: the part of what a
// build cost that nothing is left holding.
size_t kest_arena_most_beneath(const KestArena *arena);

// Returns zeroed memory, or NULL when the host is out of it. Alignment must be
// a power of two.
void *kest_arena_alloc(KestArena *arena, size_t size, size_t align);

// Makes the last thing handed out bigger, when it is the last thing handed out.
// Answers where it is now, which is where it was when the block it is in had
// the room, and somewhere else when the thing had a block to itself and the
// block was made bigger. Answers NULL when neither, and then the caller does
// what it did before: takes a new one and copies.
//
// What it gains is nought, like everything else handed out. What moves is only
// ever the thing itself, because a block that is made bigger is one nothing
// else is in.
void *kest_arena_extend(KestArena *arena, void *last, size_t was, size_t want);

// Where an arena is now, and how to put it back there. A walk that needs room
// for the length of one answer — working out what a program would have needed,
// at the moment it ran out — takes it from the heap the program is running on,
// and a refusal that costs a program memory it never gets back is a frame
// budget that shrinks every time something goes wrong. What a mark is is the
// block that was answering and how much of it had gone. See D571.
typedef struct {
    void *block;
    size_t used;
    size_t handed;
    size_t allocations;
} KestMark;

KestMark kest_arena_mark(const KestArena *arena);

// And back to it: what was handed out since is handed back, and a block taken
// since goes back to the machine underneath rather than being kept for a
// program that never asked for it. Everything an arena keeps rather than works
// out is put back to what the mark says, because a shortcut left pointing at
// what a rewind undid is the one thing a rewind could break.
void kest_arena_rewind(KestArena *arena, KestMark mark);

// Hands everything back at once and keeps the arena, which is what a program
// wants between frames: the block it started with stays, and only what was
// handed out of it is cleared again. Taking a block from the host and giving
// one back every time round a loop is a cost a frame budget can see.
void kest_arena_reset(KestArena *arena);

// Copies len bytes and terminates them, so the result is usable wherever a C
// string is expected.
char *kest_arena_strndup(KestArena *arena, const char *text, size_t len);

// How many bytes have been handed out. What a running program allocated is
// the cost D012 defers, and a number is what makes it a thing a host can see
// rather than a thing to argue about.
size_t kest_arena_used(const KestArena *arena);

// And how many it is still holding, which is the same number until something
// is given back. What a stage leaves behind for the next one is the difference:
// the tokens a file is read into are dead the moment its tree is made, and an
// arena that has given them back says so here and not above. See D747.
// How many times it was asked for something, which is what tells a stage that
// keeps a lot from one that asks a lot: the same bytes in ten allocations and
// in ten thousand are two different shapes of work. See D752.
size_t kest_arena_askings(const KestArena *arena);

size_t kest_arena_held(const KestArena *arena);

// And how many it has ever handed out, which never goes down. What a stage or
// a call cost is the difference between two readings of this, and the two
// numbers above cannot answer that any more: a rewind takes bytes off them,
// and so does a walk of what a running program can still reach. See D996.
size_t kest_arena_taken(const KestArena *arena);

// Counts bytes handed out by an arena that has since been freed as bytes this
// one asked the host for. What it keeps true is that `kest_arena_used` means
// the same thing it always meant, and that a ceiling refuses the same programs.
void kest_arena_charge(KestArena *arena, size_t bytes);

// And says those bytes are the host's again, which is what tells `used` from
// `held`. Charged where a scratch arena grows, because that is where a ceiling
// refuses; returned where it is freed, because that is when the room is back.
void kest_arena_returned(KestArena *arena, size_t bytes);

// How much room a ceiling leaves, for capping a scratch arena the same way:
// work moved out of this arena is work that must still be refused where this
// one would have refused it. Nought when there is no ceiling, and one when
// there is a ceiling with nothing left under it.
size_t kest_arena_ceiling_left(const KestArena *arena);

// What that ceiling is, and nought where there is none. Read beside the two
// numbers above by whatever says what happened: a run stopped short of what it
// was allowed is told what it was allowed, because a number nobody is told is
// a number nobody can raise.
size_t kest_arena_ceiling(const KestArena *arena);

// What a scratch arena under this one was refused, taken as this one's own.
// Work moved out of an arena is work its ceiling still refuses, so a refusal
// has to come back the way the charge does: the arena a reader is told about
// is the one with the ceiling written on it, and it is not the one that was
// standing there when the allocation failed. See D843.
void kest_arena_also_refused(KestArena *arena, const KestArena *other);

// Whether the last refusal was the ceiling rather than the host. The number
// above is the same number either way and the two are not the same thing to do
// anything about: one is a promise this arena kept and the other is the
// machine underneath having nothing left. Answers false when nothing has been
// refused, which is why it is read beside the number and not instead of it.
bool kest_arena_refused_by_ceiling(const KestArena *arena);

// Whether anything has been refused since the last build opened, over every
// arena rather than over one. A program is read into an arena of its own and
// checked into another, so the one that runs out is rarely the one a list of
// diagnostics is kept in — and what a reader needs to know is not which arena
// it was but that there was no room. Set where a refusal is, forgotten where a
// build begins. See D880.
bool kest_arena_refused_anywhere(void);
void kest_arena_forget_refusals(void);

// What the allocation this arena last refused was asking for, and nought when
// it has refused nothing. A ceiling stops a program at the allocation that
// would have crossed it, so what was handed out stops short of the ceiling by
// this much: the two numbers are the same number said from either side.
size_t kest_arena_refused(const KestArena *arena);

// What the arena held, with what was taken under it, when a ceiling refused
// it: the number a refusal says it had taken.
size_t kest_arena_refused_holding(const KestArena *arena);

// Whether this arena handed out the address: inside one of its blocks and
// below what that block has given away. A machine asks it about a pointer it
// was handed from outside, because reading one it never gave out is reading
// whatever is at that address — and what a handle is checked for is four bytes
// at the front, which any four bytes can be.
//
// What all the blocks sit between answers most of it without a walk, and the
// block that answered last answers the rest of it: the same handle crosses
// every frame. What is left is a walk, which is why it is asked at a boundary
// crossing and not at an instruction.
bool kest_arena_holds(KestArena *arena, const void *at);

// The most this arena will ever hand out. Zero is none, which is what an arena
// has until somebody says otherwise. Past it an allocation answers NULL, which
// is what every caller already handles, because the alternative is a caller
// that handles running out one way and being capped another.
//
// A ceiling under what has already been handed out takes nothing back: what is
// out is out, and every allocation after it is refused. Nothing here caps a
// running arena — the machine caps its heap once, where the heap is made, and
// the loader and the parser cap a scratch where the scratch is made — so this
// is what the number means rather than a thing anything does. See D826.
void kest_arena_cap(KestArena *arena, size_t bytes);

#define KEST_ARENA_NEW(arena, type)                                            \
    ((type *)kest_arena_alloc((arena), sizeof(type), _Alignof(type)))

#define KEST_ARENA_ARRAY(arena, type, count)                                   \
    ((type *)kest_arena_alloc((arena), sizeof(type) * (count), _Alignof(type)))

#endif
