#include "vm.h"
#include "ground.h"
#include "lexer.h"

// The sanitised build is told where every block a host has ends, which is the
// one thing a library cannot work out for itself about somebody else's memory.
#if KEST_CHECKED
#include <sanitizer/asan_interface.h>
#endif

#include <stdatomic.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Whether instructions hand over through a table of label addresses, which is
// GCC's and clang's and not the standard's. See D1181. Not on WebAssembly,
// which has no jump to an address: clang makes the table one more `switch`
// around the real one, and the machine ran a third slower for it. See D1255.
#if defined(__GNUC__) && !KEST_CHECKED && !defined(__wasm__)
#define KEST_THREADED 1
#else
#define KEST_THREADED 0
#endif

// What a host gets when it says nothing.
// The two a host gets by saying nothing, which `kest.h` names so that a host
// can say the same thing on purpose.
#define STACK_SLOTS KEST_STACK_SLOTS
#define MAX_FRAMES KEST_CALL_DEPTH

// How many of a thing the program can be told it has. `len` gives back an
// `i32`, so this is one number and not three: an array, a store and a text
// are counted by the same builtin and stop at the same place.
#define MAX_COUNTED INT32_MAX

// An array is a length and a run of elements laid out the way the host lays
// them out: an array of `f32` is four bytes an element. The block is separate
// from the header so that it can one day be the host's own. What frees it is
// not decided; see D012.
// What a handle is. A host holds these as opaque values and can hand one back
// where another was wanted, which nothing at the boundary can see: the machine
// carries no types and D046 says why. So the handle says what it is.
// How many places in stores a machine can tell apart. A reference carries the
// stamp its slot was handed out with and a stamp is a `u32`, so this is where
// they run out — four thousand million of them, after which a stamp handed out
// again would make a reference from the first occupant read as the newest one,
// which is the one thing a reference is for.
// What a reference is made of: which handout it is, and which place. Forty
// bits and twenty-four.
//
// It used to say which world it came from as well, in sixteen bits taken from
// a count of the machines this process had made -- and a count of sixteen bits
// comes round. The sixty-five thousand and thirty-seventh machine was told it
// was the first, and a reference made in the first world, which was still
// standing, resolved in the new one and answered its object. Two live worlds
// accepting one authority. See D1033.
//
// So there is no world here any more. A handout number is taken from one count
// for the whole process and is never handed out twice, which makes it say
// everything the world said and everything the per-machine count said, in one
// field: a reference carrying a number this store never wrote names nothing
// here, whether it came from another machine, another store, or the same place
// before it was given back.
//
// None of this is written down as an ABI: what a reference is made of is the
// runtime's and is read only by the runtime, and a program cannot see the
// number at all -- there is no text for a reference and no whole number of
// one, which is what keeps a count shared across threads out of what a
// `deterministic` program answers.
#define REF_SERIAL_BITS 40
// Read from `types.h`, because the type layer hashes a reference by its place
// and has to take one apart the same way this does. See D1054.
#define REF_INDEX_BITS KEST_REF_PLACE_BITS
// The ceilings a program runs into. `check-ceilings.sh` lowers one of these in
// a copy of this tree to watch the refusal happen, so they are their own
// numbers rather than the masks below: a mask that moved with a lowered
// ceiling took a reference apart wrongly and every one of them read as stale.
#define MOST_STAMPS 1099511627775ull
#define MOST_PLACES 16777215u
#define REF_SERIAL_MASK ((1ull << REF_SERIAL_BITS) - 1ull)
#define REF_INDEX_MASK ((1u << REF_INDEX_BITS) - 1u)

#define KEST_IS_ARRAY 0x4b415252u
#define KEST_IS_STORE 0x4b53544fu
// What a lend the host has taken back is. It is not any of the others, so it
// is refused wherever a handle is used, and it is not nothing either: the
// program is told what happened to it rather than told it never was one.
#define KEST_WAS_LENT 0x4b454e44u

// Both headers begin with it, so which one a handle is can be read without
// knowing which one it was meant to be.
#define KEST_HANDLE_IS(handle, tag)                                          \
    ((handle) != NULL && *(const uint32_t *)(handle) == (tag))

typedef struct {
    uint32_t what;
    uint32_t length;
    uint32_t capacity;
    uint16_t stride;
    // Lent by the host, which means the block is not ours to move and the
    // array is not ours to grow.
    bool borrowed;
    unsigned char *bytes;
    // What one of them is. A stride says how far apart two of them are and
    // says nothing about what is inside one: two `i32` and four `f32` are both
    // a stride, and a host that lent the first where the second was wanted was
    // read sixty-four bytes past the end of its own memory and told nobody.
    // The type is the build's own and there is one of each, so this is a
    // pointer comparison at the one door a handle can arrive through. NULL for
    // a handle made before anything knew, which is refused rather than
    // trusted. See D927.
    const KestType *of;
} Array;

// The same shape, said again where anything outside this file can read it,
// and held to being the same shape by the compiler rather than by a rule
// somebody remembers. A generated file reads a run of elements through
// `KestRun` (D1112); a field that moved here and not there is every such file
// reading memory that means something else.
_Static_assert(sizeof(Array) == sizeof(KestRun),
               "a run of elements is one shape, and the header says another");
_Static_assert(offsetof(Array, what) == offsetof(KestRun, what) &&
                   offsetof(Array, length) == offsetof(KestRun, length) &&
                   offsetof(Array, capacity) == offsetof(KestRun, capacity) &&
                   offsetof(Array, stride) == offsetof(KestRun, stride) &&
                   offsetof(Array, borrowed) == offsetof(KestRun, borrowed) &&
                   offsetof(Array, bytes) == offsetof(KestRun, bytes) &&
                   offsetof(Array, of) == offsetof(KestRun, of),
               "a run of elements keeps its pieces where the header says");
_Static_assert(KEST_IS_ARRAY == KEST_RUN_IS,
               "what the first word of a run says is said twice");

// What reading a value out of memory turned up, which is one thing: a tag that
// is no case of its own enum. Every other piece of a value means what its width
// says, and a tag means which of several things the pieces beside it are — so a
// number with no case behind it is a value nothing can read, and the `match`
// that meets it has no arm to take.
//
// It is read where the reading happens rather than where the memory arrives,
// because a lend is memory the host goes on writing to: anything held at the
// lend is a promise about a moment that has passed. See D710.
typedef struct {
    const KestType *type;
    int32_t tag;
    bool wrong;
} TagRead;

// Memory to stack and back. Everything goes through memcpy, because a
// borrowed block is aligned the way its owner aligned it and not the way this
// machine would like.
static void unpack(KestValue *out, const KestLayout *layout,
                   const unsigned char *from, TagRead *told);
static void pack(unsigned char *to, const KestLayout *layout,
                 const KestValue *from);


// One scalar, read out of memory into slots and written back. It is the same
// switch `unpack` and `pack` run over a piece, factored out so that all three
// say it once -- and so that a scalar moved on its own does not have to build
// a layout of one piece and walk it. Building one was nine per cent of
// `bench/rules.kest`, whose elements carry tagged unions and so are moved by
// their type rather than by a flat list of pieces. See D1028.
//
// And written into every place that reads one, where the compiler can be
// told to: left to itself it made this one function the whole machine
// called, so every element read in every instruction went through one call
// and one switch, and which case came next was a guess about the whole
// program rather than about the instruction reading it. See D1193.
#if defined(__GNUC__)
#define WHERE_IT_IS_READ __attribute__((always_inline))
#else
#define WHERE_IT_IS_READ
#endif
static inline WHERE_IT_IS_READ void read_piece(KestValue *out, uint8_t kind,
                                               const unsigned char *at) {
    switch (kind) {
    case KEST_L_TEXT: {
        memcpy(&out[0], at, 8);
        uint64_t many;
        memcpy(&many, at + 8, 8);
        out[1].integer = (int64_t)many;
        break;
    }
    case KEST_L_I8: {
        int8_t v;
        memcpy(&v, at, 1);
        out[0].integer = v;
        break;
    }
    case KEST_L_I16: {
        int16_t v;
        memcpy(&v, at, 2);
        out[0].integer = v;
        break;
    }
    case KEST_L_I32: {
        int32_t v;
        memcpy(&v, at, 4);
        out[0].integer = v;
        break;
    }
    // The byte an optional keeps after its value is one byte, read the way any
    // other byte is. Its kind is what it is for and not what it is, and what
    // it is is this. See D714. A truth is the third of them, and the same
    // byte. See D839.
    case KEST_L_U8:
    case KEST_L_FLAGS8:
    case KEST_L_BOOL:
    case KEST_L_HELD: {
        uint8_t v;
        memcpy(&v, at, 1);
        out[0].integer = v;
        break;
    }
    case KEST_L_FLAGS16:
    case KEST_L_U16: {
        uint16_t v;
        memcpy(&v, at, 2);
        out[0].integer = v;
        break;
    }
    case KEST_L_FLAGS32:
    case KEST_L_U32: {
        uint32_t v;
        memcpy(&v, at, 4);
        out[0].integer = v;
        break;
    }
    case KEST_L_F32: {
        float v;
        memcpy(&v, at, 4);
        out[0].real = v;
        break;
    }
    case KEST_L_F64: {
        double v;
        memcpy(&v, at, 8);
        out[0].real = v;
        break;
    }
    default:
        memcpy(&out[0], at, 8);
        break;
    }
}

static inline void write_piece(unsigned char *at, uint8_t kind,
                        const KestValue *from) {
    switch (kind) {
    case KEST_L_TEXT: {
        memcpy(at, &from[0], 8);
        uint64_t many = (uint64_t)from[1].integer;
        memcpy(at + 8, &many, 8);
        break;
    }
    case KEST_L_I8:
    case KEST_L_U8:
    case KEST_L_FLAGS8:
    case KEST_L_BOOL:
    case KEST_L_HELD: {
        uint8_t v = (uint8_t)from[0].integer;
        memcpy(at, &v, 1);
        break;
    }
    case KEST_L_I16:
    case KEST_L_FLAGS16:
    case KEST_L_U16: {
        uint16_t v = (uint16_t)from[0].integer;
        memcpy(at, &v, 2);
        break;
    }
    case KEST_L_I32:
    case KEST_L_FLAGS32:
    case KEST_L_U32: {
        uint32_t v = (uint32_t)from[0].integer;
        memcpy(at, &v, 4);
        break;
    }
    case KEST_L_F32: {
        float v = (float)from[0].real;
        memcpy(at, &v, 4);
        break;
    }
    case KEST_L_F64: {
        double v = from[0].real;
        memcpy(at, &v, 8);
        break;
    }
    default:
        memcpy(at, &from[0], 8);
        break;
    }
}

// A run of one kind read out of memory into slots, one conversion a loop. The
// kinds a slot holds bit for bit -- text, a handle, a whole number or a float
// of sixty-four bits -- are moved a slot at a time as well rather than as one
// copy: a copy of the whole run is a call, and a wide read of slots written
// one at a time a moment before waits for every one of those writes to land,
// which took a run of four `f64` from a tenth fewer cycles to a sixth more.
// See D1177.
#define READ_RUN(type, member, count)                                           \
    for (uint32_t k = 0; k < (uint32_t)(count); k++) {                        \
        type v;                                                                \
        memcpy(&v, at + k * sizeof(type), sizeof(type));                       \
        to[k].member = v;                                                      \
    }

#define WRITE_RUN(type, member, count)                                          \
    for (uint32_t k = 0; k < (uint32_t)(count); k++) {                        \
        type v = (type)from[k].member;                                         \
        memcpy(at + k * sizeof(type), &v, sizeof(type));                       \
    }

#define COPY_IN(count)                                                         \
    for (uint32_t k = 0; k < (uint32_t)(count); k++) {                        \
        memcpy(&to[k], at + k * 8, 8);                                         \
    }

#define COPY_OUT(count)                                                        \
    for (uint32_t k = 0; k < (uint32_t)(count); k++) {                        \
        memcpy(at + k * 8, &from[k], 8);                                       \
    }

// And one of them, which is most steps, in the same switch as the runs: a step
// that went to a second switch for its kind paid for two jumps.
#define READ_ONE(type, member)                                                  \
    do {                                                                       \
        type v;                                                                \
        memcpy(&v, at, sizeof(type));                                          \
        to[0].member = v;                                                      \
    } while (0)

#define WRITE_ONE(type, member)                                                 \
    do {                                                                       \
        type v = (type)from[0].member;                                         \
        memcpy(at, &v, sizeof(type));                                          \
    } while (0)

static void walk_in(KestValue *out, const KestMoving *walk, uint32_t first,
                    uint32_t count, const unsigned char *from, TagRead *told) {
    uint32_t end = first + count;
    for (uint32_t i = first; i < end; i++) {
        const KestMoveStep *step = &walk->steps[i];
        KestValue *to = out + step->slot;
        const unsigned char *at = from + step->byte;
        uint16_t many = step->many;
        switch (step->kind) {
        case KEST_MOVE_RUN | KEST_L_I8:
            READ_RUN(int8_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_I16:
            READ_RUN(int16_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_I32:
            READ_RUN(int32_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_U8:
        case KEST_MOVE_RUN | KEST_L_FLAGS8:
        case KEST_MOVE_RUN | KEST_L_BOOL:
        case KEST_MOVE_RUN | KEST_L_HELD:
            READ_RUN(uint8_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_U16:
        case KEST_MOVE_RUN | KEST_L_FLAGS16:
            READ_RUN(uint16_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_U32:
        case KEST_MOVE_RUN | KEST_L_FLAGS32:
            READ_RUN(uint32_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_F32:
            READ_RUN(float, real, many);
            break;
        case KEST_MOVE_RUN | KEST_L_TEXT:
            COPY_IN(2 * many);
            break;
        case KEST_MOVE_RUN | KEST_L_I64:
        case KEST_MOVE_RUN | KEST_L_U64:
        case KEST_MOVE_RUN | KEST_L_F64:
        case KEST_MOVE_RUN | KEST_L_WORD:
        case KEST_MOVE_RUN | KEST_L_FLAGS64:
        case KEST_MOVE_RUN | KEST_L_FN:
        case KEST_MOVE_RUN | KEST_L_REF:
            COPY_IN(many);
            break;
        case KEST_MOVE_CASES: {
            int32_t tag;
            memcpy(&tag, at, 4);
            to[0].integer = tag;
            // A tag and one slot of what a case carries is the commonest
            // shape there is, and a loop of one the compiler made into a
            // call to `memset`. See D1197.
            if (step->slots == 2) {
                to[1].integer = 0;
            } else {
                for (uint16_t s = 1; s < step->slots; s++) {
                    to[s].integer = 0;
                }
            }
            if (tag >= 0 && (uint32_t)tag < step->case_count) {
                const KestMoveRun *run =
                    &walk->ranges[step->cases + (uint32_t)tag];
                // A tag is written last among the steps of its run, so the
                // last step is the commonest place for one, and there the
                // walk goes on into the case rather than calling itself for
                // it. A case's steps are written after the value's, so the
                // step before them is one this walk has. See D1227.
                if (i + 1 == end) {
                    i = run->first - 1;
                    end = run->first + run->count;
                    continue;
                }
                walk_in(out, walk, run->first, run->count, from, told);
            } else if (told != NULL && !told->wrong) {
                // The payload slots are left at nought above, which is what
                // made this readable at all; what it is not is a value of this
                // type. The first one found is the one said, because a run of
                // them is one mistake about one piece of memory said as many
                // times as the program looks at it.
                told->type = step->type;
                told->tag = tag;
                told->wrong = true;
            }
            break;
        }
        case KEST_L_I8:
            READ_ONE(int8_t, integer);
            break;
        case KEST_L_I16:
            READ_ONE(int16_t, integer);
            break;
        case KEST_L_I32:
            READ_ONE(int32_t, integer);
            break;
        case KEST_L_U8:
        case KEST_L_FLAGS8:
        case KEST_L_BOOL:
        case KEST_L_HELD:
            READ_ONE(uint8_t, integer);
            break;
        case KEST_L_U16:
        case KEST_L_FLAGS16:
            READ_ONE(uint16_t, integer);
            break;
        case KEST_L_U32:
        case KEST_L_FLAGS32:
            READ_ONE(uint32_t, integer);
            break;
        case KEST_L_F32:
            READ_ONE(float, real);
            break;
        case KEST_L_TEXT:
            memcpy(to, at, 16);
            break;
        default:
            memcpy(to, at, 8);
            break;
        }
    }
}

static void walk_out(unsigned char *to_bytes, const KestMoving *walk,
                     uint32_t first, uint32_t count, const KestValue *from_slots) {
    uint32_t end = first + count;
    for (uint32_t i = first; i < end; i++) {
        const KestMoveStep *step = &walk->steps[i];
        unsigned char *at = to_bytes + step->byte;
        const KestValue *from = from_slots + step->slot;
        uint16_t many = step->many;
        switch (step->kind) {
        case KEST_MOVE_RUN | KEST_L_I8:
        case KEST_MOVE_RUN | KEST_L_U8:
        case KEST_MOVE_RUN | KEST_L_FLAGS8:
        case KEST_MOVE_RUN | KEST_L_BOOL:
        case KEST_MOVE_RUN | KEST_L_HELD:
            WRITE_RUN(uint8_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_I16:
        case KEST_MOVE_RUN | KEST_L_U16:
        case KEST_MOVE_RUN | KEST_L_FLAGS16:
            WRITE_RUN(uint16_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_I32:
        case KEST_MOVE_RUN | KEST_L_U32:
        case KEST_MOVE_RUN | KEST_L_FLAGS32:
            WRITE_RUN(uint32_t, integer, many);
            break;
        case KEST_MOVE_RUN | KEST_L_F32:
            WRITE_RUN(float, real, many);
            break;
        case KEST_MOVE_RUN | KEST_L_TEXT:
            COPY_OUT(2 * many);
            break;
        case KEST_MOVE_RUN | KEST_L_I64:
        case KEST_MOVE_RUN | KEST_L_U64:
        case KEST_MOVE_RUN | KEST_L_F64:
        case KEST_MOVE_RUN | KEST_L_WORD:
        case KEST_MOVE_RUN | KEST_L_FLAGS64:
        case KEST_MOVE_RUN | KEST_L_FN:
        case KEST_MOVE_RUN | KEST_L_REF:
            COPY_OUT(many);
            break;
        case KEST_MOVE_CASES: {
            int32_t tag = (int32_t)from[0].integer;
            // The same shape the other way: eight bytes, said as eight so
            // that it is a store rather than a call. See D1197.
            if (step->size == 8) {
                memset(at, 0, 8);
            } else {
                memset(at, 0, step->size);
            }
            memcpy(at, &tag, 4);
            if (tag >= 0 && (uint32_t)tag < step->case_count) {
                const KestMoveRun *run =
                    &walk->ranges[step->cases + (uint32_t)tag];
                if (i + 1 == end) {
                    i = run->first - 1;
                    end = run->first + run->count;
                    continue;
                }
                walk_out(to_bytes, walk, run->first, run->count, from_slots);
            }
            break;
        }
        case KEST_L_I8:
        case KEST_L_U8:
        case KEST_L_FLAGS8:
        case KEST_L_BOOL:
        case KEST_L_HELD:
            WRITE_ONE(uint8_t, integer);
            break;
        case KEST_L_I16:
        case KEST_L_U16:
        case KEST_L_FLAGS16:
            WRITE_ONE(uint16_t, integer);
            break;
        case KEST_L_I32:
        case KEST_L_U32:
        case KEST_L_FLAGS32:
            WRITE_ONE(uint32_t, integer);
            break;
        case KEST_L_F32:
            WRITE_ONE(float, real);
            break;
        case KEST_L_TEXT:
            memcpy(at, from, 16);
            break;
        default:
            memcpy(at, from, 8);
            break;
        }
    }
}

// Every value is moved by the walk its layout was written out as: every run of
// scalars where it sits, and for a tag, the tag and then the steps of the case
// it names. What a case does not carry is nought in the slots and nought in
// the bytes, so one value is one run of each whatever was there before. See
// D711, D1159 and D1177.
//
// One piece with no tag in it is most of what a program moves -- a number, a
// handle, a piece of text -- and is moved here rather than through a call and
// a walk of one. See D1154.
static void unpack(KestValue *out, const KestLayout *layout,
                   const unsigned char *from, TagRead *told) {
    if (layout->count == 1 && !layout->tagged) {
        read_piece(out, layout->pieces[0].kind, from + layout->pieces[0].offset);
        return;
    }
    const KestMoving *walk = layout->walk;
    walk_in(out, walk, 0, walk->count, from, told);
}

static void pack(unsigned char *to, const KestLayout *layout,
                 const KestValue *from) {
    if (layout->count == 1 && !layout->tagged) {
        write_piece(to + layout->pieces[0].offset, layout->pieces[0].kind, from);
        return;
    }
    const KestMoving *walk = layout->walk;
    walk_out(to, walk, 0, walk->count, from);
}

// A slot map. Removing marks the slot dead and steps its generation, so a
// reference handed out before is recognised as stale rather than followed.
// Nothing is notified and nothing is counted; see D014.
typedef struct {
    uint32_t what;
    KestValue *elements;
    // What each place was stamped with the last time it was handed out, which
    // is a number no other place in this process has ever been stamped with.
    // See D1033.
    uint64_t *serials;
    // Which slots are live, a bit each rather than a byte each. A walk of a
    // store is a walk of this, and what it costs is what it has to read: at a
    // byte a slot, a store that had held a million and holds eight read a
    // megabyte to find them. At a bit a slot it reads sixteen kilobytes, and a
    // word of nothing is one test rather than sixty-four. See D954.
    uint64_t *live;
    uint32_t *free_slots;
    uint32_t free_count;
    // How far a walk goes, and how far the counts have been written. They are
    // the same until a store is emptied: a walk over a store that held a
    // million and holds none would step over a million dead slots, so the
    // extent goes back to nought there. What each slot has counted stays,
    // because that is what makes a reference from before stale, so `high` is
    // where a slot has never been used at all and needs its first count.
    uint32_t used;
    uint32_t high;
    uint32_t count;
    uint32_t capacity;
    uint16_t stride;
    // The same, for the same reason: what one place in it holds.
    const KestType *of;
    // And how it is laid out, kept rather than looked up: a walk of what a
    // world can still reach reads every live place in it, and looking the
    // layout up would be a search of the module for every one of them. See
    // D996.
    const KestLayout *holds;
} Store;

// How many working-memory blocks one machine may have open at once. Nesting is
// lexical and a body is refused for more than eight, but a body that calls
// itself can open one a call deep, so this is a number met while running.
#define MAX_KEPT 64

// Where a piece of text is first found in another from a place on, or -1:
// the one answer both engines give. The first byte is looked for with the C
// library's own search and the rest compared where it lands, which is what
// the byte-at-a-time walk it replaced spent most of `bench/words.kest` doing
// by hand. See D1169.
static int64_t found_at(const char *bytes, int64_t length, const char *needle,
                        int64_t needle_length, int64_t from) {
    if (needle_length == 0) {
        return from;
    }
    const char *at = bytes + from;
    const char *end = bytes + length;
    while (end - at >= needle_length) {
        const char *first =
            memchr(at, needle[0], (size_t)(end - at - needle_length + 1));
        if (first == NULL) {
            return -1;
        }
        if (memcmp(first + 1, needle + 1, (size_t)(needle_length - 1)) == 0) {
            return first - bytes;
        }
        at = first + 1;
    }
    return -1;
}

// What a piece of text is: bytes and how many. It is two slots wherever a
// value lives, and this is the pair read out of them. See D964.
typedef struct {
    const char *bytes;
    uint32_t length;
} Said;

static Said said(const KestValue *slots) {
    Said out;
    out.bytes = slots[0].text;
    out.length = (uint32_t)slots[1].integer;
    return out;
}

typedef struct {
    const KestChunk *chunk;
    const uint8_t *ip;
    // Where this call's slots begin. The operand stack sits above them.
    KestValue *base;
    // Where in the source this frame's own call is written, for a frame whose
    // body is compiled C and so has no instruction to read one off. Nought
    // for every frame the machine made, which reads it from where the
    // instruction pointer has got to. See D1098.
    uint32_t said_at;
#if KEST_CHECKED
    // How deep the operand stack is to be at this frame's next instruction,
    // plus one, as the verifier's table says the last one left it: nought
    // before the first. It sits in what the three above leave over, so a
    // frame is no bigger for it. See D1239.
    uint32_t expect;
#endif
} Frame;

#if KEST_CHECKED
#define FRESH(frame) ((frame)->expect = 0)
#else
#define FRESH(frame) ((void)0)
#endif

// The same shape, said again where a generated file can read it, and held to
// being the same shape by the compiler rather than by a rule somebody
// remembers. A body the host's compiler compiled writes one of these itself
// (D1122); a field that moved here and not there is every such file writing
// into memory that means something else.
_Static_assert(sizeof(Frame) == sizeof(KestCall),
               "a call is one shape, and the header says another");
_Static_assert(offsetof(Frame, chunk) == offsetof(KestCall, chunk) &&
                   offsetof(Frame, ip) == offsetof(KestCall, ip) &&
                   offsetof(Frame, base) == offsetof(KestCall, base) &&
                   offsetof(Frame, said_at) == offsetof(KestCall, said_at),
               "a call keeps its pieces where the header says");

struct KestRuntime {
    // What this machine is made of, in an arena of its own: the stack, the
    // frames, the table of what the host provides, and this struct. It goes
    // when the machine goes, rather than sitting on the build's arena until
    // the build does — a host that starts a machine, frees it and starts
    // another was paying for every one of them. See D574.
    KestArena *own;
    // Everything a call needs, kept between calls, so the host can call in
    // more than once and what the program allocated is still there.
    const KestModule *module;
    KestNative *natives;
    void **contexts;
    KestDiags *diags;
    KestValue *stack;
    KestValue *limit;
    // Separate from the arena the compiler used, so what a running program
    // allocates is visibly its own. What is on it is what lasts as long as the
    // machine does and is never given back a piece at a time: the frames of a
    // world, the list of what is lent, the runs a store keeps beside its
    // places. What a program makes and replaces is not here -- see `ground`.
    KestArena *heap;
    // And where what a program makes stands: text, arrays and worlds, in
    // places that are given back when nothing can reach them. An arena could
    // not give one back without giving all of them back, which is what made a
    // world that replaces a name every round grow without bound. See D996.
    KestGround *ground;
    // What the host has told this machine to keep whatever the program can
    // reach. A handle the host holds and does not hand back in is one the
    // walk cannot see, and a walk that cannot see it gives its memory away.
    // See D996.
    KestValue *held_by_host;
    uint32_t held_count;
    uint32_t held_room;
    // And what the host itself was handed and did not ask to keep: text this
    // machine made for a host, and the header in front of a block a host
    // lent. Neither is reachable from anything the program holds, and both
    // are the machine's memory until the heap goes.
    void **handed;
    uint32_t handed_count;
    uint32_t handed_room;
    // How much may be taken before a walk is worth doing. It is what was
    // still standing after the last walk, so a world that is bigger walks
    // less often for the same fraction of its size -- and never below a floor,
    // so a program holding almost nothing does not walk on every allocation.
    // The block of handout numbers this machine has claimed and how far into
    // it it has got. One write to the count the process shares buys a
    // thousand of them. See D1053.
    uint64_t handout_next;
    uint64_t handout_upto;
    size_t walk_at;
    // Whether to walk before every allocation rather than when enough has
    // been taken. It is how a handle held somewhere a walk does not read is
    // found: under it, the first allocation after such a handle is made
    // gives the handle's memory away, and the program reads something else
    // where it was. `KEST_WALK_EVERY` asks for it. See D1163.
    bool walk_every;
    // How many times what it is holding may be handed out before that is
    // worth a walk. One by default, which is the shortest pause; a host that
    // is not budgeting a frame says otherwise. See D1045.
    uint32_t walk_after;
    // The most this machine was ever holding at once, which is what says a
    // world has settled where the number above says only what it holds now.
    size_t most;
    // What a walk has met and not yet read, and whether it ran out of room to
    // remember. Kept on the machine rather than made for each walk, because a
    // walk happens where a program has just failed to get memory and is not a
    // place to be asking the host for more of it than it must.
    void **grey;
    uint32_t grey_count;
    uint32_t grey_room;
    bool walk_broke;
    // What the machine has just made and has not yet written anywhere a walk
    // can read. Making a store is four runs and a header, and until the last
    // of them is there the first four are named by nothing but a local
    // variable of this C — which a walk cannot read, and a sweep after one
    // would hand back memory the machine is in the middle of using. Six is
    // the deepest any of them goes: a store's four runs, its header, and the
    // elements of an array in front of its header. See D996.
    const void *in_hand[6];
    uint32_t hands;
    // What the host allowed the heap, kept so a reset gets the same ceiling
    // and so a refusal can say which of the two it was.
    size_t heap_bytes;
    // And what it allowed in instructions, which is the one ceiling here that
    // bounds time rather than memory. `fuel_left` is spent by the machine and
    // is the only thing the hot path reads: a machine with no budget starts it
    // at every bit set and never reaches nought in any run a person waits for,
    // so one comparison serves both. A host that cancels writes nought into it
    // from wherever it is, which is why the two are told apart by the flag
    // beside them rather than by the counter. See D921.
    uint64_t fuel_left;
    uint64_t fuel_given;
    bool fuel_bounded;
    // Started for code nobody trusts: only opened doors, and nothing the other
    // backend wrote is entered. See D1246.
    bool untrusted;
    // Where a debugger stopped this machine, or NULL. A machine that stopped
    // is not finished and is not broken: its frames, its stack and its heap
    // are where they were, and `kest_resume` carries on from the instruction
    // the breakpoint was written over. Nothing in the hot path reads it: it is
    // written by the one instruction nothing compiles to. See D991.
    const uint8_t *stopped_at;
    // Where the call a host made put its arguments, kept so that a run which
    // stopped and carried on can hand back what came out of it: the copy out
    // is at the end of `kest_call` and a resume does not go through there.
    KestValue *called_floor;
    // Written by a host that may not be this thread and read by the machine.
    // An `_Atomic int` and nothing weaker: `volatile sig_atomic_t` says the
    // compiler will not cache it and says nothing at all about what another
    // thread sees, which is the whole of what a host cancelling from its own
    // thread needs. Relaxed on the reading side, because the only thing the
    // machine does with it is stop, and released on the writing side, so a
    // host that set something up before cancelling has that visible too.
    //
    // This is cross-thread and is not claimed to be safe from a signal
    // handler: `atomic_int` is only lock-free in practice and the standard
    // does not promise a handler may touch it. See D929.
    atomic_int cancel_asked;
    // And what a bound function said when it could not do what it was asked.
    // NULL when the last crossing worked, which is every crossing that does
    // not call `kest_native_failed`. See D937.
    const char *native_failed;
    // The frames live in the arena rather than on the host's stack, so the
    // depth limit is Kest's own number and not whatever the host allows.
    Frame *frames;
    uint32_t frame_count;
    uint32_t stack_slots;
    uint32_t call_depth;
#if KEST_CHECKED
    // The deepest this machine ever got, in slots and in frames, which only
    // the build that checks itself counts. D812 asked the same of one body;
    // this asks it of the run, which is what a host is sized by. See D813.
    uint32_t went_slots;
    uint32_t went_frames;
    // And whether the program had a least at all. One that does is sized by
    // what it needs and one that does not by what a frame of it costs, and
    // the room left over means a different thing in each. See D815.
    bool had_least;
    // What the machine ran, one count for each instruction there is. The other
    // readings here are high-water marks — how far a run reached — and this is
    // the other question: what it did on the way. A static count of what a
    // program is written with says nothing about a loop, and a loop is what
    // this language is for. Counted only by the build that checks itself, so
    // the release build's numbers are still the release build's.
    //
    // Taken only when somebody asks for it: a hundred and fifty-two counts is
    // twelve hundred bytes, and a machine is two thousand. `examples/embed.c`
    // holds a machine to being smaller than a walk of the program it runs, and
    // it refused one carrying these before anybody had asked to read them.
    // Nought here is a machine that is not counting. See D870.
    uint64_t *ran_checked;
    // What the machine moved, in bytes, counted where it moves it. The
    // instruction histogram beside this says how many times something ran;
    // what a run of it moved depends on its operands and on a layout, so it
    // is counted rather than worked out from the name. Same build, same
    // reason: a counter at every movement in a release machine is what D979
    // measured at a third of it, and a third is nothing beside a sanitiser.
    //
    // Each of these is bytes that were actually copied from somewhere to
    // somewhere. Nothing counts a pointer being moved, a stack pointer being
    // stepped, or a length being read. See D1023.
    uint64_t moved_loaded;
    uint64_t moved_stored;
    uint64_t moved_held;
    uint64_t moved_unpacked;
    uint64_t moved_packed;
    uint64_t moved_shifted;
    uint64_t moved_payload;
    // There is no counter for what a call moves, because a call moves
    // nothing: the callee's frame starts where its arguments already are and
    // what it answers is left where the caller reads it. Neither `call` nor
    // `return` copies a slot, and a counter of nought would read as a
    // measurement rather than as a property of the machine.
    uint64_t moved_text;
    uint64_t moved_shuffled;
    // And which instruction followed which, so that a pair worth one
    // instruction can be told from a pair that never happens. Kept beside the
    // counts above and under the same environment variable, in the build that
    // checks itself. See D1010.
    uint64_t *pairs_checked;
    uint8_t last_checked;
    // And how many questions this build asked of its own compiler on the way:
    // every number an instruction carries is read by something that asks
    // whether it could be that number (D900 to D906), and this is how many
    // times that happened. A count rather than a duration, so what it says is
    // what the build that checks itself does rather than how fast this machine
    // did it. Counted only where somebody has asked to read it, the same as
    // the line above. See D907.
    uint64_t guarded;
#endif
    // What a run did, counted when a caller asked and not otherwise. Every
    // build can be asked, because a profile of the build that checks itself is
    // a profile of a machine doing things this one does not. What it costs a
    // run nobody asked is one test of a pointer that is nothing, at the top of
    // the loop and at the four places something happens worth counting. It is
    // counts and not durations: a machine counts what it did and a clock is
    // the host's. See D979.
    uint64_t *entered;
    uint32_t entered_room;
    uint64_t crossings;
    // What the memory under the program did, which a host reads through
    // `kest_telemetry`. Each of these is at an allocation, a walk, a lend or a
    // copy rather than at an instruction, so counting them always costs
    // nothing a run can see. See D1007.
    //
    // The clock is the host's, because this library is ISO C and there is no
    // monotonic clock in it (D935). A host that gives one gets what a walk
    // took; a host that gives none gets nought there and everything else.
    uint64_t (*clock)(void *);
    void *clock_context;
    // And who is told what each walk cost, which is the distribution the
    // numbers below cannot give: they are a total and a worst. See D1027.
    void (*collected)(const KestPause *pause, void *context);
    void *collected_context;
    uint64_t walked;
    uint64_t worst_walk;
    // Calls in from a host that arrived while one was already running.
    uint64_t reentered;
    uint64_t marking;
    uint64_t sweeping;
    uint64_t roots;
    uint64_t lends;
    uint64_t lent_elements;
    uint64_t copied;
    // How much had been said when this started, and how much of it has been
    // written out since. What failed to compile is not this machine's to
    // report and is not reported twice.
    uint32_t said_before;
    uint32_t reported;
    // And whether this machine has said the one thing a run with no memory
    // can say, which is not in the list because making a list entry is what
    // there was no room for.
    bool starve_said;
    // Where the machine is while a host function it called is running. A host
    // may call back in from there, and what it starts has to stand above what
    // is already on the stack rather than on top of it. NULL when nothing of
    // the program's is running. See D072.
    KestValue *running_top;
    uint32_t running_frames;
    // Which instruction the machine was running when it last asked a door
    // for something. A door takes where it was asked from so that a refusal
    // inside it is reported where the machine would have reported it, and
    // working that out is a walk over the body: this is the machine saying
    // where it is in one store, and the walk happening only if the door
    // refuses. See D1117.
    const uint8_t *asked_at;
    // Where the operand stack had got to when a debugger stopped the machine.
    // One machine stops in one place, so it is the machine's rather than
    // every frame's: it is written at one instruction and read at one, and a
    // frame is a thing there are a thousand of. See D991 and D1098.
    KestValue *stopped_top;
    // Headers of lends the host has ended, kept to be lent again. A host that
    // lends a batch a frame and ends it at the end of the frame would
    // otherwise leave a header on the heap every frame, which is a frame
    // budget that grows for a program that does the same thing every time.
    // The link is the block pointer, which an ended lend has no use for.
    // See D241.
    Array *spare_lends;
    // Where a host asked the heap to be put back to, newest last, and the
    // number each was handed out under. A mark is a place in this list and the
    // number that says which time it was: a host that keeps one past a rewind
    // holds a number nothing answers to rather than a place that has moved.
    // See D957.
    KestMark scratch[KEST_SCRATCH_DEEP];
    uint32_t scratch_given[KEST_SCRATCH_DEEP];
    uint32_t scratch_count;
    uint32_t scratch_handed;
    // And the program's own, which is a different question with the same
    // answer: a `scratch { }` block marks the heap and puts it back. They are
    // kept apart from the host's because the host's are refused while a
    // program is running, which is exactly when these are open. See D966.
    // Taken from this machine's own room the first time a program opens one,
    // so a program with no `scratch { }` in it pays nothing for the door.
    KestMark *kept;
    uint32_t kept_count;
    uint32_t kept_room;
    // Every lend the host has not ended, so that ending one ends every handle
    // over that block: a host lending the same memory twice has two handles
    // and one block, and it is the block it takes back. See D283.
    // How many places this machine has handed out. It is a count and not an
    // authority: what makes a reference this machine's is the handout number
    // in it, which comes from the process rather than from here. See D1033.
    uint64_t stamps;
    // The build's count of what is standing on it, which this machine is one
    // of until it is freed.
    atomic_uint *standing;
    Array **lent;
    uint32_t lent_count;
    uint32_t lent_capacity;
    // What a host was told it needs where the program calls into it, which is
    // what `kest_needs_from` answers and a host sizes a stack from. Held
    // against what the machine turns out to be there. False when the program
    // reaches itself or calls through a value, and then there was no number
    // to give a host and none to hold. See D234.
    // Which of the names a machine can explain it has explained already. What
    // a host asked for and cannot call is a statement about the program, which
    // does not change while a machine lives — it is not something that
    // happened — and saying it again cost 634 bytes an asking, which a host
    // polling for a name it might have pays every frame. One byte a name, and
    // the program's own count of them. See D608.
    uint8_t *said_extern;
    uint8_t *said_copy;
    // And which of the program's layouts it has said something about. A
    // refusal about a lend that is about the program — how wide the program
    // lays a type out, that two of a name are in it, that one holds the
    // machine's own — says the same thing every time it is asked, and a host
    // lending in a frame asks it every frame. One byte a layout, which is
    // what a lend names. See D616.
    uint8_t *said_layout;
    // Where this machine's own room stood when it had been built, and when
    // what it said was last read. What a host has been told is the host's,
    // and the words it was told go back to the machine that wrote them: a
    // program refused every frame says the same sentence every frame, and a
    // machine that kept all of them would hold a frame's words for as long as
    // it ran. See D617.
    KestMark after_said;
    // And whether a host has been told what an index that is no function is.
    // The number in it is how many functions the program has, which does not
    // change; `kest_entry_name` answers the same question for nothing. One
    // bit, because there is one sentence and it is about the program rather
    // than about an index. See D615.
    bool said_no_frame;
    bool host_measured;
    uint32_t host_slots;
    uint32_t host_frames;
    // And which function that measurement was of: the one holding the deepest
    // call into the host, which the walk names. A fault says both it and the
    // function the call it refused is in, because the two being different is
    // the difference between a walk that measured the wrong chunk and one that
    // measured the right chunk wrongly. It is a name in the module, which
    // outlives this machine. See D606.
    const char *host_where;
};

typedef struct KestRuntime Vm;

// How much may be taken before a walk of what can still be reached is worth
// doing. Under this a program that makes almost nothing would walk on every
// other allocation, and what it would find is nothing.
#define WALK_FLOOR (256u * 1024u)

// What sits in front of the elements of an array, so that a walk that met
// those elements without meeting the header can still read them. An address of
// an element is a thing a program holds -- that is what `KEST_OP_ELEM_ADDR`
// makes -- and a walk that could not follow one would give away what the
// elements name. See D996.
typedef struct {
    const KestLayout *layout;
    // How many elements there is room for, not how many there are: what is
    // past the length was left as nought when the room was taken, so reading
    // it finds nothing to follow, and a length kept in step here would be a
    // second place to remember at every push and pop.
    uint32_t places;
    uint16_t stride;
    // Whether anything in an element is an address at all, so an array of
    // numbers is stepped over rather than read.
    bool follow_them;
    // And whether what is in one has to be read every eight bytes rather than
    // by its pieces, which is what a value holding a tagged union is: which
    // type a payload slot holds depends on the tag beside it.
    bool loose;
} Elems;

static void follow(Vm *rt, const void *at);

// Held for as long as it takes to write it somewhere, and let go after. The
// two are always in one function and always in that order, so the count going
// back to what it was is what says a hand was emptied.
static void *in_hand(Vm *rt, void *at) {
    if (at != NULL && rt->hands < (uint32_t)(sizeof(rt->in_hand) /
                                             sizeof(rt->in_hand[0]))) {
        rt->in_hand[rt->hands++] = at;
    }
    return at;
}

static void hands_off(Vm *rt, uint32_t was) {
    rt->hands = was;
}

// Everything from here to there, taken as though each eight bytes might be an
// address. The machine's slots carry no tags -- the language is statically
// typed and an instruction knows what it is working on -- so a walk over them
// cannot be told which hold addresses, and what it does instead is ask the
// ground about each. An eight-byte number that happens to name a place keeps
// that place, which costs memory and cannot cost correctness: nothing is
// moved, so a number read as an address is never written through. See D996.
static void follow_loosely(Vm *rt, const KestValue *from, const KestValue *to) {
    for (const KestValue *at = from; at < to; at++) {
        follow(rt, at->object);
    }
}

static void follow_packed(Vm *rt, const unsigned char *bytes,
                          const Elems *head) {
    if (!head->follow_them) {
        return;
    }
    size_t span = (size_t)head->places * head->stride;
    if (head->loose || head->layout == NULL) {
        // An address is as wide as the machine's, which is less than the
        // word it is kept in where a pointer is four bytes: copying eight into
        // one wrote past it on WebAssembly. See D1255.
        for (size_t at = 0; at + 8 <= span; at += 8) {
            void *what;
            memcpy(&what, bytes + at, sizeof what);
            follow(rt, what);
        }
        return;
    }
    const KestLayout *layout = head->layout;
    for (uint32_t which = 0; which < head->places; which++) {
        const unsigned char *one = bytes + (size_t)which * head->stride;
        for (uint16_t piece = 0; piece < layout->count; piece++) {
            KestScalar kind = layout->pieces[piece].kind;
            if (kind != KEST_L_TEXT && kind != KEST_L_WORD) {
                continue;
            }
            void *what;
            memcpy(&what, one + layout->pieces[piece].offset, sizeof what);
            follow(rt, what);
        }
    }
}

// One place in a world, which is slots rather than packed bytes: a piece of
// text is one piece of a layout and two of these. See D964.
static void follow_slots(Vm *rt, const KestValue *entry,
                         const KestLayout *layout, uint16_t slots) {
    if (layout == NULL || layout->tagged) {
        follow_loosely(rt, entry, entry + slots);
        return;
    }
    uint16_t slot = 0;
    for (uint16_t piece = 0; piece < layout->count && slot < slots; piece++) {
        KestScalar kind = layout->pieces[piece].kind;
        if (kind == KEST_L_TEXT) {
            follow(rt, entry[slot].text);
            slot += 2;
            continue;
        }
        if (kind == KEST_L_WORD) {
            follow(rt, entry[slot].object);
        }
        slot++;
    }
}

// What a walk has met and not yet read. It is a list rather than the C stack
// because the shapes a program builds are the program's to decide: a world
// whose places name arrays whose elements name worlds is as deep as somebody
// wrote it, and a walk that went as deep as that in calls would run off the
// stack of the host rather than say anything.
static bool later(Vm *rt, void *at) {
    if (rt->grey_count == rt->grey_room) {
        uint32_t bigger = rt->grey_room == 0 ? 256 : rt->grey_room * 2;
        void **grown = realloc(rt->grey, (size_t)bigger * sizeof(void *));
        if (grown == NULL) {
            rt->walk_broke = true;
            return false;
        }
        rt->grey = grown;
        rt->grey_room = bigger;
    }
    rt->grey[rt->grey_count++] = at;
    return true;
}

static void follow(Vm *rt, const void *at) {
    // One lookup rather than three. Marking is three quarters of what a
    // collection costs and this is what marking is: for every address a walk
    // follows, whether it is on this heap, where the thing it is in starts,
    // and what kind of thing that is -- which were three hashes of the same
    // address and three probes of the same table. See D1008.
    void *start = NULL;
    KestGroundKind kind = KEST_GROUND_PLAIN;
    if (!kest_ground_reached(rt->ground, at, &start, &kind)) {
        return;
    }
    if (start == NULL || kind == KEST_GROUND_PLAIN) {
        return;
    }
    later(rt, start);
}

static void read_one(Vm *rt, void *start) {
    switch (kest_ground_kind(rt->ground, start)) {
    case KEST_GROUND_PLAIN:
        break;
    case KEST_GROUND_ARRAY: {
        Array *array = start;
        // A lend is a header of the machine's in front of a block that is the
        // host's, and the host's block is not this machine's to keep or to
        // give away. A header the host has ended holds the next spare one in
        // the same field, which is not elements either.
        if (array->what == KEST_IS_ARRAY && !array->borrowed) {
            follow(rt, array->bytes);
        }
        break;
    }
    case KEST_GROUND_ELEMS: {
        Elems *head = start;
        follow_packed(rt, (const unsigned char *)(head + 1), head);
        break;
    }
    case KEST_GROUND_STORE: {
        Store *store = start;
        follow(rt, store->elements);
        follow(rt, store->serials);
        follow(rt, store->live);
        follow(rt, store->free_slots);
        if (store->elements == NULL || store->live == NULL) {
            break;
        }
        for (uint32_t which = 0; which < store->used; which++) {
            uint64_t bit = (uint64_t)1 << (which % 64);
            if ((store->live[which / 64] & bit) == 0) {
                continue;
            }
            follow_slots(rt, store->elements + (size_t)which * store->stride,
                         store->holds, store->stride);
        }
        break;
    }
    }
}

// Where the machine's slots have got to, which is what a walk reads to the top
// of. A host that called in is above whatever was already running, so the
// higher of the two is the edge.
static KestValue *the_edge(Vm *rt, KestValue *reach) {
    KestValue *edge = reach;
    if (rt->running_top != NULL && (edge == NULL || rt->running_top > edge)) {
        edge = rt->running_top;
    }
    return edge == NULL ? rt->stack : edge;
}

// What can still be reached, and then what cannot given back. Nothing is
// moved: a value stays where it was made, which is what lets a walk read a
// slot it cannot be sure is an address and lets a program hold the address of
// an element or of a piece of text cut out of another. See D996.
static bool gather(Vm *rt, KestValue *reach) {
    if (rt->ground == NULL || kest_ground_open_count(rt->ground) != 0) {
        return false;
    }
    uint64_t began = rt->clock == NULL ? 0 : rt->clock(rt->clock_context);
    uint64_t roots_before = rt->roots;
    size_t held_before = kest_ground_used(rt->ground);
    rt->walk_broke = false;
    rt->grey_count = 0;
    KestValue *edge = the_edge(rt, reach);
    rt->roots += (uint64_t)(edge - rt->stack);
    follow_loosely(rt, rt->stack, edge);
    for (uint32_t i = 0; i < rt->hands; i++) {
        follow(rt, rt->in_hand[i]);
    }
    for (uint32_t i = 0; i < rt->held_count; i++) {
        follow(rt, rt->held_by_host[i].object);
    }
    for (uint32_t i = 0; i < rt->handed_count; i++) {
        follow(rt, rt->handed[i]);
    }
    for (uint32_t i = 0; i < rt->lent_count; i++) {
        follow(rt, rt->lent[i]);
    }
    for (Array *spare = rt->spare_lends; spare != NULL;
         spare = (Array *)(void *)spare->bytes) {
        follow(rt, spare);
    }
    while (rt->grey_count > 0 && !rt->walk_broke) {
        read_one(rt, rt->grey[--rt->grey_count]);
    }
    if (rt->walk_broke) {
        // A walk that could not remember where it had got to has not seen
        // everything, and a sweep after one of those gives away memory the
        // program can still reach. So it takes nothing, and says it took
        // nothing to whoever asked for it.
        kest_ground_unmark(rt->ground);
        return false;
    }
    uint64_t marked = rt->clock == NULL ? 0 : rt->clock(rt->clock_context);
    kest_ground_sweep(rt->ground);
    size_t standing = kest_ground_used(rt->ground);
    size_t want = standing * (rt->walk_after == 0 ? 1 : rt->walk_after);
    rt->walk_at = want < WALK_FLOOR ? WALK_FLOOR : want;
    uint64_t ended = rt->clock == NULL ? 0 : rt->clock(rt->clock_context);
    if (rt->clock != NULL) {
        uint64_t took = ended - began;
        rt->walked += took;
        rt->marking += marked - began;
        rt->sweeping += ended - marked;
        if (took > rt->worst_walk) {
            rt->worst_walk = took;
        }
    }
    if (rt->collected != NULL) {
        // Asked of the heap here rather than kept running, because a walk is
        // where it is worth a walk of the plots: there are tens of them and
        // millions of allocations. See D1027.
        KestGroundPlots plots;
        kest_ground_plots(rt->ground, &plots);
        KestPause pause;
        memset(&pause, 0, sizeof pause);
        pause.took = rt->clock == NULL ? 0 : ended - began;
        pause.marking = rt->clock == NULL ? 0 : marked - began;
        pause.sweeping = rt->clock == NULL ? 0 : ended - marked;
        pause.roots = rt->roots - roots_before;
        pause.reclaimed =
            held_before > standing ? (uint64_t)(held_before - standing) : 0;
        pause.live = standing;
        pause.plots = plots.plots;
        pause.plot_bytes = plots.bytes;
        pause.free_bytes = plots.free_bytes;
        rt->collected(&pause, rt->collected_context);
    }
    return true;
}

// What this machine handed a host and nothing in the program names. A host
// holds it in its own memory, which no walk of this machine's can read, so it
// is kept until the heap goes -- which is exactly how long it lasted before
// there was a walk at all, and exactly what `kest_still_holds` says about it.
// See D996.
// Whether this machine handed the address out, which is now two questions: the
// arena still holds what lasts as long as the machine, and the ground holds
// what a program makes. A host asking whether what it kept is still the
// machine's is asking about both.
static bool ours(const KestRuntime *rt, const void *at) {
    return kest_arena_holds(rt->heap, at) || kest_ground_holds(rt->ground, at);
}

static bool handed_over(Vm *rt, void *at) {
    if (at == NULL) {
        return true;
    }
    if (rt->handed_count == rt->handed_room) {
        uint32_t bigger = rt->handed_room == 0 ? 16 : rt->handed_room * 2;
        void **grown = realloc(rt->handed, (size_t)bigger * sizeof(void *));
        if (grown == NULL) {
            return false;
        }
        rt->handed = grown;
        rt->handed_room = bigger;
    }
    rt->handed[rt->handed_count++] = at;
    return true;
}

// Room for something the program is making. What makes this different from
// asking the ground directly is the two things that happen when there is not
// enough: a walk, and then the same ask again. A program running under a tight
// ceiling is one where the room it needs is there and is held by something
// nothing can reach.
// Whether anything in one of these is an address, so an array of numbers is
// stepped over by a walk rather than read element by element.
static bool holds_addresses(const KestLayout *layout) {
    if (layout->tagged) {
        return true;
    }
    for (uint16_t piece = 0; piece < layout->count; piece++) {
        KestScalar kind = layout->pieces[piece].kind;
        if (kind == KEST_L_TEXT || kind == KEST_L_WORD) {
            return true;
        }
    }
    return false;
}

// The ceiling a host set is one number over two places, and only one of them
// can be told about it: what the arena has handed out is already spent, so
// what the ground may ask for is the rest. Said before every take rather than
// once, because the arena's share moves.
static void under_the_ceiling(Vm *rt) {
    if (rt->heap_bytes == 0) {
        return;
    }
    size_t elsewhere = kest_arena_used(rt->heap);
    kest_ground_cap(rt->ground,
                    rt->heap_bytes > elsewhere ? rt->heap_bytes - elsewhere : 1);
}

static void *take(Vm *rt, KestValue *reach, size_t bytes, KestGroundKind kind) {
    under_the_ceiling(rt);
    if (rt->walk_every || kest_ground_since(rt->ground) >= rt->walk_at) {
        gather(rt, reach);
    }
    void *at = kest_ground_take(rt->ground, bytes, kind);
    if (at == NULL) {
        gather(rt, reach);
        at = kest_ground_take(rt->ground, bytes, kind);
    }
    if (at != NULL) {
        size_t holding = kest_heap_used(rt);
        if (holding > rt->most) {
            rt->most = holding;
        }
    }
    return at;
}


// What the program calls a type, which is the last piece of the name it is
// registered under. A host writes `Event`, not the module it came from.
// The program's side of a disagreement about a lend, written the way a
// declaration is, so a host can put it beside its own struct and see which
// field moved. Empty for a type that has no members to write.
// The first few fields of a struct and where each of them sits, for a host
// that has laid the same shape out differently. Four of them, and a count of
// the rest: a reader comparing two declarations has the first disagreement in
// front of them by then.
//
// In the arena and as long as those four are. It was a hundred and ninety-two
// bytes and stopped where they ran out, so a struct with long field names lost
// the rest of the list and the count of what was lost with it.
#define SHOWN_FIELDS 4

static const char *written_shape(KestArena *arena, const KestType *type) {
    uint32_t shown =
        type->member_count < SHOWN_FIELDS ? type->member_count : SHOWN_FIELDS;
    size_t room = strlen(", and 4294967295 more") + 1;
    for (uint32_t i = 0; i < shown; i++) {
        room += strlen(type->members[i].name) +
                strlen(kest_type_name(arena, type->members[i].type)) +
                strlen(", `: ` at 4294967295");
    }
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "";
    }

    out[0] = '\0';
    size_t at = 0;
    for (uint32_t i = 0; i < shown; i++) {
        const KestMember *member = &type->members[i];
        at += (size_t)snprintf(out + at, room - at, "%s`%s: %s` at %u",
                               at == 0 ? "" : ", ", member->name,
                               kest_type_name(arena, member->type),
                               member->byte_offset);
    }
    if (type->member_count > shown) {
        snprintf(out + at, room - at, ", and %u more",
                 type->member_count - shown);
    }
    return out;
}

// Where a type was written, when it was written anywhere: a note points at the
// declaration the host has come apart from, which is the half of the
// disagreement the host cannot see.
// Whether this host has already been told something about this layout. What a
// refusal about a lend says of the program — how wide it lays a type out, that
// two of a name are in it, that one of them holds the machine's own — is true
// of the program and says the same thing every time it is asked. A host that
// lends in a frame asks every frame, and what saying it again cost was 749
// bytes an asking, on the arena the build's diagnostics are written in and
// never handed back. See D616.
//
// A layout the module does not hold is not one of these: it is said, because
// nothing here knows what it is about.
static bool told_about(KestRuntime *runtime, const KestLayout *layout) {
    if (layout == NULL || runtime->module->layouts == NULL) {
        return false;
    }
    size_t which = (size_t)(layout - runtime->module->layouts);
    if (which >= runtime->module->layout_count) {
        return false;
    }
    if (runtime->said_layout[which] != 0) {
        return true;
    }
    runtime->said_layout[which] = 1;
    return false;
}

static void note_declaration(KestRuntime *runtime, const KestLayout *layout,
                             const char *label) {
    const KestType *type = layout->type;
    if (type == NULL || type->declared_in == NULL) {
        return;
    }
    kest_diags_note(runtime->diags, type->declared_in, type->span, "%s", label);
}

bool kest_text(KestRuntime *runtime, const char *bytes, uint32_t length,
               KestValue *into) {
    if (into == NULL) {
        return false;
    }
    into[0].text = "";
    into[1].integer = 0;
    if (runtime == NULL) {
        return false;
    }
    // Nothing to copy is not the same as nothing to say. What comes back for
    // it is an empty piece of text, which is also what comes back for a host
    // that handed over an empty one on purpose, so the two read alike and
    // only this says which. See D436.
    if (bytes == NULL) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "this host handed over %u byte%s of text and no address "
                       "to find them at",
                       length, length == 1 ? "" : "s");
        kest_diags_suggest(runtime->diags,
                           "a host with nothing to say hands over an empty "
                           "piece of text; an address of nothing is bytes that "
                           "were never there");
        return false;
    }
    // Copied into the machine's heap, which is what the program's own text is
    // in: a host that handed a pointer of its own would be promising to keep
    // it as long as the program holds it, and a program holds a piece of text
    // for as long as it likes.
    // Text is UTF-8. A nought is a byte it may hold, since D971, and a byte
    // that begins no character is not: a host that hands over bytes of its own
    // is the boundary where that is worth one walk, because everything the
    // machine makes out of text that was already whole is whole. See D971.
    uint32_t bad = 0;
    if (!kest_utf8_whole(bytes, length, &bad)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "byte %u of what the host handed over begins no "
                       "character, and text is UTF-8",
                       bad);
        return false;
    }
    // No walk here: a host asks for this between calls, when what the program
    // holds is named by the host's own memory and by nothing a walk can read.
    under_the_ceiling(runtime);
    char *held = kest_ground_take(runtime->ground, length + 1,
                                  KEST_GROUND_PLAIN);
    if (held != NULL && !handed_over(runtime, held)) {
        held = NULL;
    }
    if (held == NULL) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0605", nowhere,
                       "out of memory");
        return false;
    }
    memcpy(held, bytes, length);
    held[length] = '\0';
    into[0].text = held;
    into[1].integer = length;
    return true;
}

// A lend that could not be written down. What it costs is a header and a place
// in the list of what is lent, both on the machine's heap, so a host that lends
// without ending pays for every one of them until the heap goes — and the heap
// that runs out here is the one the host itself gave. Nothing about the value
// says which of the reasons in this function it was: a `NULL` for a name that
// is not there and a `NULL` for a heap with nothing left were the same answer,
// and only one of them is about the program. See D350.
static void no_room_to_lend(KestRuntime *runtime) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    if (runtime->heap_bytes != 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0643", nowhere,
                       "this host lent something and the heap it gave has "
                       "%zu of its %zu bytes left",
                       runtime->heap_bytes - kest_heap_used(runtime),
                       runtime->heap_bytes);
    } else {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0643", nowhere,
                       "this host lent something and this machine has no room "
                       "to write it down");
    }
    kest_diags_suggest(runtime->diags,
                       "a lend costs a header and a place in the list of what "
                       "is lent: end the ones this host is done with, or give "
                       "the machine more heap");
}

// A machine that did not start is a machine with nothing in it. `kest_start`
// answers NULL and says why into the build's report, and a host that carries
// on regardless used to take the process down at thirteen of the doors it
// could knock on next -- while eight of the others answered politely, which is
// the shape of a guard somebody wrote where they happened to be. Every door
// answers the same way now: the answer a real machine gives when it has
// nothing, which is what `kest_frame_slots` and `kest_entry_name` already
// said. There is nowhere to say more, because a report belongs to a machine
// and there is none. See D894.
KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      const char *element, size_t size) {
    KestValue value = {0};
    if (runtime == NULL) {
        return value;
    }

    // A lend is an address and a count, and a host with nothing to lend has a
    // count of nought rather than an address of nothing. What used to happen
    // was that the program got an array of four bytes at no address and read
    // it: the machine cannot tell a bad address from a good one, and this is
    // the one address it can. See D357.
    KestSpan missing = {0, 0};
    if (data == NULL && length > 0) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0644", missing,
                       "this host lent %u `%s` and gave no address to find "
                       "them at",
                       length, element);
        kest_diags_suggest(runtime->diags,
                           "a host with nothing to lend lends nought of them; "
                           "an address of nothing is a block that was never "
                           "there");
        return value;
    }

    // What the program lays this type out as. Only a type the program uses as
    // an element has one, which is exactly the set that can be lent, and a
    // host asking beforehand asks the same thing.
    const KestLayout *named_layouts[2] = {NULL, NULL};
    uint32_t named = kest_module_layout_of(runtime->module, element,
                                           named_layouts, 2);
    const KestLayout *layout = named_layouts[0];
    // A lend is not in a file, so nothing is pointed at.
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    if (named == 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program has no array of `%s` to lend to", element);
        // A lend names a type, and only a declared one has a name. A run, an
        // optional or a reference is spelled out of other types and has none,
        // so what a host lends an array of is a struct around it.
        const char *nearest = kest_module_nearest(runtime->module, element);
        if (element[0] == '[' || strchr(element, '<') != NULL ||
            strchr(element, '?') != NULL) {
            kest_diags_suggest(runtime->diags,
                               "a lend names a declared type; give it one: "
                               "`struct Row { m: %s }`",
                               element);
        } else if (nearest != NULL) {
            kest_diags_suggest(runtime->diags, "the nearest one that can be "
                                               "lent to is `%s`",
                               nearest);
        } else {
            kest_diags_suggest(runtime->diags,
                               "only a type the program holds in an array can "
                               "be lent");
        }
        return value;
    }
    if (named > 1) {
        if (!told_about(runtime, named_layouts[0])) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610",
                           nowhere, "more than one `%s` is in this program",
                           element);
            for (uint32_t i = 0; i < 2; i++) {
                note_declaration(runtime, named_layouts[i], "this one");
            }
            const char *askable = kest_module_askable(runtime->module, element);
            kest_diags_suggest(runtime->diags,
                               "write the module it came from: `%s`",
                               askable == NULL ? "world.Event" : askable);
        }
        return value;
    }
    // And what is inside it, which is the one thing about a lend that no
    // number can say. The bytes are the host's: a pointer in them is one the
    // machine did not put there, cannot vouch for, and cannot take back when
    // the lend ends — so a program reading it would be holding the host's
    // memory after the host had moved on. `text` of a lent run is the one
    // place a lend stops being free, and this is what keeps it the one.
    const KestType *own = NULL;
    if (layout->type != NULL && kest_type_holds_own(layout->type, &own)) {
        if (!told_about(runtime, layout)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0647",
                           nowhere,
                           "`%s` holds `%s`, which is the machine's own and "
                           "cannot be lent",
                           element, kest_type_name(runtime->diags->arena, own));
            note_declaration(runtime, layout, "this is what it holds");
            kest_diags_suggest(runtime->diags,
                               "lend the numbers and hand the rest over a "
                               "frame: `kest_text` makes text the machine "
                               "keeps, and what a program keeps of a lend is "
                               "what it copied out of one");
        }
        return value;
    }

    uint16_t stride = layout->size;
    if (size != stride) {
        if (told_about(runtime, layout)) {
            return value;
        }
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program lays `%s` out in %u bytes and this host "
                       "has %zu",
                       element, stride, size);
        const KestType *type = layout->type;
        const char *shape = type != NULL && type->member_count > 0
                                ? written_shape(runtime->diags->arena, type)
                                : "";
        note_declaration(runtime, layout,
                         shape[0] == '\0' ? "this is what it lays out" : shape);
        kest_diags_suggest(runtime->diags,
                           "the two declarations have come apart");
        return value;
    }

    // Whose memory it is. A lend is the host's block, read where it sits and
    // written where it sits — so a host that hands back an address the machine
    // gave it is handing the machine its own memory as something a program may
    // write to. The one that arrives that way is text: a program that is given
    // a piece of it holds a pointer into the heap or into the build, and a host
    // that lends those bytes as a run of numbers has made the one thing this
    // language says cannot be written into a thing that can. A literal rewritten
    // that way stays rewritten for every machine the build starts. See D720.
    if (data != NULL && (ours(runtime, data) ||
                         kest_arena_holds(runtime->module->arena, data))) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0653", nowhere,
                       "this host lent %u `%s` at an address this machine owns",
                       length, element);
        kest_diags_suggest(runtime->diags,
                           "a lend is the host's own memory; what a program "
                           "gave a host is the program's, and copying it out "
                           "is what a host does with one");
        return value;
    }

    // Where the host put it. The size says how far apart two of them are and
    // the pieces say what is inside one; neither says the address is one the
    // program may read a field from. It is the only thing about a lend that
    // nothing in the program can be written to get wrong, and the only one
    // the host alone knows.
    uint16_t align = layout->align == 0 ? 1 : layout->align;
    uintptr_t past = (uintptr_t)data % align;
    if (past != 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program aligns `%s` to %u bytes and this host lent "
                       "one %u past a multiple of that",
                       element, align, (unsigned)past);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "lend an array of the type itself, which the host's "
                           "own compiler aligns; a byte buffer read as one is "
                           "not aligned by anything");
        return value;
    }

    // What can be said in either build is what the program is able to count
    // to, and it is asked first for exactly that reason: a count no `i32`
    // holds is wrong whatever the host owns, and one lend that got two
    // different answers in two builds would be a message a reader could not
    // repeat. See D358. `len` gives back an `i32`, so a lend longer than one holds is a lend
    // whose end the program cannot see, and every loop over it walks off
    // memory that is really there into memory that is not.
    if (length > MAX_COUNTED) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "this host lent %u `%s` and the program counts them "
                       "with an `i32`",
                       length, element);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "lend %d at a time at the most; `len` is where the "
                           "program reads the end from",
                           MAX_COUNTED);
        return value;
    }

    // How many there are is the host's word and nothing here can weigh it in a
    // build that ships: the memory is the host's and its end is not written
    // down anywhere the library can read. The sanitised build can ask, because
    // it is told where every block a host has ends, and a lend that runs past
    // one is the mistake this crossing is shaped around — every loop over it
    // walks off memory that is really there into memory that is not. See D286.
#if KEST_CHECKED
    if (length > 0 &&
        __asan_region_is_poisoned(data, (size_t)length * stride) != NULL) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "this host lent %u `%s` and does not own that many",
                       length, element);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "the count is the host's word and this build "
                           "weighs it: lend what is there");
        return value;
    }
#endif

    // One the host ended, if there is one, and a new one otherwise. What is
    // reused is the header and never the block: the block is the host's and
    // this one is the one just handed over.
    Array *array = runtime->spare_lends;
    if (array != NULL) {
        runtime->spare_lends = (Array *)(void *)array->bytes;
    } else {
        under_the_ceiling(runtime);
        array = kest_ground_take(runtime->ground, sizeof(Array),
                                 KEST_GROUND_ARRAY);
    }
    if (array == NULL) {
        no_room_to_lend(runtime);
        return value;
    }
    // Written down before it is handed over: a lend nothing knows about is one
    // that cannot be taken back with the rest of its block.
    if (runtime->lent_count == runtime->lent_capacity) {
        uint32_t bigger = runtime->lent_capacity == 0
                              ? 8
                              : runtime->lent_capacity * 2;
        // On the heap, where the headers are: a lend costs a header and a
        // place in this, and a heap thrown away takes both with it.
        Array **grown = KEST_ARENA_ARRAY(runtime->heap, Array *, bigger);
        if (grown == NULL) {
            no_room_to_lend(runtime);
            return value;
        }
        for (uint32_t i = 0; i < runtime->lent_count; i++) {
            grown[i] = runtime->lent[i];
        }
        runtime->lent = grown;
        runtime->lent_capacity = bigger;
    }
    runtime->lent[runtime->lent_count++] = array;

    array->what = KEST_IS_ARRAY;
    array->length = length;
    array->capacity = length;
    array->borrowed = true;
    array->stride = stride;
    array->of = layout->type;
    // The block is the host's. The header is ours, and it points at theirs.
    array->bytes = data;
    value.object = array;
    runtime->lends++;
    runtime->lent_elements += length;
    return value;
}

// The instruction being executed, so a failure is reported at the source it
// came from rather than at the byte after it.
// Writes what a program would write to build this value, in the manner of
// snprintf: it returns the length it needed whether or not it fitted, so the
// caller measures with room of nought and then writes.
static size_t format_value(char *out, size_t room, const KestType *type,
                           const KestValue *slots);

static size_t put_text(char *out, size_t room, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0; i < length && i < room; i++) {
        out[i] = text[i];
    }
    return length;
}

// A string is written as a string, quotes and escapes and all, because what
// is being written is the source and not the content. A hole holding text on
// its own is the content, which is the exception D035 names.
static size_t put_quoted(char *out, size_t room, const char *text,
                        size_t length) {
    size_t used = 0;
    if (used < room) {
        out[used] = '"';
    }
    used++;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        // A nought is written as the escape that stands for it rather than as
        // itself, because what this writes is the source a reader takes back
        // and a nought written as itself is a piece of text that ends there.
        if (c == '\0') {
            if (used < room) {
                out[used] = '\\';
            }
            used++;
            if (used < room) {
                out[used] = '0';
            }
            used++;
            continue;
        }
        if (c == '"' || c == '\\') {
            if (used < room) {
                out[used] = '\\';
            }
            used++;
        }
        if (used < room) {
            out[used] = c;
        }
        used++;
    }
    if (used < room) {
        out[used] = '"';
    }
    return used + 1;
}

static size_t format_flags(char *out, size_t room, const KestType *set,
                           uint64_t bits) {
    const char *named = kest_type_written(set);
    size_t used = 0;
    bool any = false;
    for (uint32_t c = 0; c < set->case_count; c++) {
        if ((bits & ((uint64_t)1 << c)) == 0) {
            continue;
        }
        if (any) {
            used += put_text(out + (used < room ? used : room),
                             used < room ? room - used : 0, " | ");
        }
        any = true;
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ".");
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, set->cases[c].name);
    }
    if (!any) {
        used += put_text(out, room, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "()");
    }
    return used;
}

// Only this file writes a value now: what a host asks is `kest_gave_text`,
// and the command line is a host.
static size_t kest_write_value(char *out, size_t room, const KestType *type,
                               const KestValue *slots) {
    return format_value(out, room, type, slots);
}

static size_t format_value(char *out, size_t room, const KestType *type,
                           const KestValue *slots) {
    char buffer[64];
    switch (type->tag) {
    case KEST_T_BOOL:
        return put_text(out, room, slots[0].integer ? "true" : "false");
    case KEST_T_INT:
        kest_write_whole(buffer, (uint64_t)slots[0].integer,
                         type->is_signed);
        return put_text(out, room, buffer);
    case KEST_T_FLOAT:
        kest_write_real(buffer, sizeof(buffer), slots[0].real,
                        type->width == 32);
        return put_text(out, room, buffer);
    case KEST_T_TEXT:
        // How long it is is the slot beside it, not a measurement: a nought
        // is a byte text may hold since D971 and measuring stops at one.
        return put_quoted(out, room, slots[0].text,
                          (size_t)slots[1].integer);
    case KEST_T_FLAGS:
        return format_flags(out, room, type, (uint64_t)slots[0].integer);
    case KEST_T_ENUM: {
        const char *named = kest_type_written(type);
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return put_text(out, room, named);
        }
        const KestVariantType *variant = &type->cases[which];
        size_t used = put_text(out, room, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ".");
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, variant->name);
        if (variant->payload_count == 0) {
            return used;
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "(");
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (p > 0) {
                used += put_text(out + (used < room ? used : room),
                                 used < room ? room - used : 0, ", ");
            }
            used += format_value(out + (used < room ? used : room),
                                 used < room ? room - used : 0,
                                 variant->payload[p],
                                 slots + variant->offsets[p]);
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ")");
        return used;
    }
    // A struct written the way a program writes one: its name, and its fields
    // in the order they were declared. That is not a format chosen here — it
    // is the one the language already prints a case with a payload in, which
    // is the same shape for the same reason. A text inside is quoted, as it is
    // inside a case, because the bytes on their own do not say where one field
    // ends. See D876.
    case KEST_T_STRUCT: {
        size_t used = put_text(out, room, kest_type_written(type));
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "(");
        for (uint32_t m = 0; m < type->member_count; m++) {
            if (m > 0) {
                used += put_text(out + (used < room ? used : room),
                                 used < room ? room - used : 0, ", ");
            }
            used += format_value(out + (used < room ? used : room),
                                 used < room ? room - used : 0,
                                 type->members[m].type,
                                 slots + type->members[m].offset);
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ")");
        return used;
    }
    // And that many of something where it stands, written the way one is
    // written: the elements in brackets, which is what a program builds one
    // from.
    case KEST_T_FIXED: {
        uint16_t stride = type->element->slots == 0 ? 1 : type->element->slots;
        size_t used = put_text(out, room, "[");
        for (uint32_t i = 0; i < type->count; i++) {
            if (i > 0) {
                used += put_text(out + (used < room ? used : room),
                                 used < room ? room - used : 0, ", ");
            }
            used += format_value(out + (used < room ? used : room),
                                 used < room ? room - used : 0, type->element,
                                 slots + (size_t)i * stride);
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "]");
        return used;
    }
    // The tag is the last slot, which is where the value stops.
    case KEST_T_OPTIONAL:
        if (slots[type->element->slots].integer == 0) {
            return put_text(out, room, "none");
        }
        return format_value(out, room, type->element, slots);
    // Every other tag is written out rather than left to a `default`, so that
    // a tag added to the language cannot land here by not being mentioned:
    // `kest_type_has_text` decides what reaches this and lists the same tags,
    // and the compiler holds both lists to being every tag there is.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_ARRAY:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this: a hole and the command line both ask
    // `kest_type_has_text` first, and it says no to every tag above. It is
    // here because C wants a value and because a fault should read like one.
    return put_text(out, room, "<no text>");
}

// Two values of one type are equal when everything that makes them up is. This
// is reached for the two things that are values laid out flat — an enum and a
// struct — and what either of them carries is compared the way the same types
// are compared on their own. See D874.
static bool values_equal(const KestType *type, const KestValue *a,
                         const KestValue *b) {
    switch (type->tag) {
    case KEST_T_FLOAT:
        return a[0].real == b[0].real;
    // A reference is a place and a stamp packed into one whole number, and the
    // stamp is the build's own counter, so the number is the handout. Two
    // references are equal when they are the same handout, which is the same
    // entity and still the entity it was. See D923.
    case KEST_T_REF:
        return a[0].integer == b[0].integer;
    case KEST_T_TEXT:
        return a[1].integer == b[1].integer &&
               memcmp(a[0].text, b[0].text, (size_t)a[1].integer) == 0;
    case KEST_T_ENUM: {
        if (a[0].integer != b[0].integer) {
            return false;
        }
        uint32_t which = (uint32_t)a[0].integer;
        if (which >= type->case_count) {
            return true;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (!values_equal(variant->payload[p], a + variant->offsets[p],
                              b + variant->offsets[p])) {
                return false;
            }
        }
        return true;
    }
    case KEST_T_STRUCT:
        for (uint32_t m = 0; m < type->member_count; m++) {
            const KestMember *member = &type->members[m];
            if (!values_equal(member->type, a + member->offset,
                              b + member->offset)) {
                return false;
            }
        }
        return true;
    case KEST_T_FIXED: {
        uint16_t stride = type->element->slots == 0 ? 1 : type->element->slots;
        for (uint32_t i = 0; i < type->count; i++) {
            if (!values_equal(type->element, a + (size_t)i * stride,
                              b + (size_t)i * stride)) {
                return false;
            }
        }
        return true;
    }
    // One slot with a number in it, which is what these three are and the only
    // thing there is to compare about them.
    case KEST_T_INT:
    case KEST_T_BOOL:
    case KEST_T_FLAGS:
        return a[0].integer == b[0].integer;
    // Every other tag written out rather than left to a `default`, for the
    // reason `hash_value` beside it gives: what reaches this is decided by
    // `has_equality`, the two lists are held to being one another, and a tag
    // added to the language would otherwise compare by slot nought without
    // anybody deciding it should. See D545.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_OPTIONAL:
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this: the checker asks `has_equality` first and it says
    // no to every tag above. Two values of a type that does not compare are
    // not equal, which is the answer that cannot be mistaken for one.
    return false;
}

// A handle that is not what was wanted is a host mistake rather than a
// program one: the machine carries no types, so nothing at the boundary could
// have caught it. It is caught here instead.
// An index is refused where it is used, and seven instructions use one. The
// sentence is here rather than seven times over: an array has a length and
// that many of something has a number, so there are two of these and not one.
#define IN_ARRAY(index, array)                                                 \
    do {                                                                       \
        if ((index) < 0 || (uint64_t)(index) >= (array)->length) {              \
            fail(vmp, frame, instruction, "K0604",                             \
                 "index %lld is outside an array of length %u",                \
                 (long long)(index), (array)->length);                         \
            return false;                                                      \
        }                                                                      \
    } while (0)

// Reading a value out of memory, and what that turned up. A tag is the one
// piece of a value whose number has to mean something — the pieces beside it
// are whatever it says they are — so a number with no case behind it is read
// here and nowhere else: the `match` that meets it has no arm to take, and
// what it would do instead is take an arm belonging to another case. See D710.
#define READ_INTO(where, layout, from)                                         \
    do {                                                                       \
        TagRead told = {NULL, 0, false};                                       \
        MOVED(moved_unpacked, (layout)->size);                                 \
        /* One piece with no tag in it is most of what a program reads out */ \
        /* of a run -- a number, a handle, a piece of text -- and is read   */ \
        /* here rather than through a call and a walk of one. See D1154.    */ \
        if ((layout)->count == 1 && !(layout)->tagged) {                       \
            /* And an `i32`, which is what a count, a timer and an index   */ \
            /* are, before the switch rather than as one of its cases.     */ \
            /* See D1193.                                                  */ \
            if ((layout)->pieces[0].kind == KEST_L_I32) {                      \
                int32_t read_i32;                                              \
                memcpy(&read_i32, (from) + (layout)->pieces[0].offset, 4);     \
                (where)[0].integer = read_i32;                                 \
                break;                                                         \
            }                                                                  \
            read_piece((where), (layout)->pieces[0].kind,                      \
                       (from) + (layout)->pieces[0].offset);                   \
            break;                                                             \
        }                                                                      \
        unpack((where), (layout), (from), &told);                              \
        if (told.wrong) {                                                      \
            fail(vmp, frame, instruction, "K0651",                             \
                 "`%s` here holds tag %lld and has no such case",              \
                 kest_type_written(told.type) != NULL                          \
                     ? kest_type_written(told.type)                            \
                     : "a value with a tag in it",                             \
                 (long long)told.tag);                                         \
            kest_diags_suggest(vmp->diags,                                     \
                               "a tag in memory this machine did not write is "\
                               "the host's to get right, and `kest_case_of` "  \
                               "names the cases");                             \
            return false;                                                      \
        }                                                                      \
    } while (0)

// The numbers that reach out of a body, held to naming something the module
// has. A body names a function, an extern and a layout by an index the
// compiler wrote, and the machine reads the array at that index and takes what
// it finds: a frame from a chunk, a native from a table, a walk from a
// layout's pieces. Past the end of any of those is a pointer rather than the
// nought a constant would be, which is the one place inside this machine where
// a number one out is read as an address. See D905.
#if KEST_CHECKED
#define OF_THE_MODULE(which, many, what)                                       \
    do {                                                                       \
        vmp->guarded++;                                                        \
        if ((uint32_t)(which) >= (uint32_t)(many)) {                           \
            fail(vmp, frame, instruction, "K0655",                             \
                 "this names %s %u of the %u this program has", (what),        \
                 (uint32_t)(which), (uint32_t)(many));                         \
            kest_diags_fault(vmp->diags,                                       \
                             "what the compiler wrote into an instruction and "\
                             "what the program holds disagree");               \
            return false;                                                      \
        }                                                                      \
    } while (0)
#else
#define OF_THE_MODULE(which, many, what) ((void)0)
#endif

// A slot and a constant a fused instruction reads, held to being the body's
// own in the build that checks itself, the way `load.k` holds them.
#if KEST_CHECKED
#define OWN_SLOT_AND_CONSTANT(slot, which)                                     \
    do {                                                                       \
        if (!own_slots(vmp, frame, instruction, (slot), (slot) + 1u) ||         \
            !own_constants(vmp, frame, instruction, (which) + 1u)) {           \
            return false;                                                      \
        }                                                                      \
    } while (0)
#define OWN_CONSTANT(which)                                                    \
    do {                                                                       \
        if (!own_constants(vmp, frame, instruction, (which) + 1u)) {           \
            return false;                                                      \
        }                                                                      \
    } while (0)
#define OWN_SLOT(slot)                                                         \
    do {                                                                       \
        if (!own_slots(vmp, frame, instruction, (slot), (slot) + 1u)) {         \
            return false;                                                      \
        }                                                                      \
    } while (0)
#define OWN_ELEMENT_AND_CONSTANT(holds, at, which)                             \
    do {                                                                       \
        if (!own_slots(vmp, frame, instruction, (holds), (holds) + 1u) ||       \
            !own_slots(vmp, frame, instruction, (at), (at) + 1u) ||             \
            !own_constants(vmp, frame, instruction, (which) + 1u)) {           \
            return false;                                                      \
        }                                                                      \
    } while (0)
#else
#define OWN_SLOT_AND_CONSTANT(slot, which) ((void)0)
#define OWN_CONSTANT(which) ((void)0)
#define OWN_SLOT(slot) ((void)0)
#define OWN_ELEMENT_AND_CONSTANT(holds, at, which) ((void)0)
#endif

#define IN_RUN(index, count)                                                   \
    do {                                                                       \
        if ((index) < 0 || (uint64_t)(index) >= (count)) {                     \
            fail(vmp, frame, instruction, "K0604",                             \
                 "index %lld is outside %u of them", (long long)(index),       \
                 (count));                                                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HOLD(handle, tag, what)                                              \
    do {                                                                     \
        if (!KEST_HANDLE_IS(handle, tag)) {                                  \
            if (KEST_HANDLE_IS(handle, KEST_WAS_LENT)) {                     \
                fail(vmp, frame, instruction, "K0637",                       \
                     "the host has taken this lend back");                   \
                kest_diags_suggest(vmp->diags,                               \
                                   "the block is the host's and it said so; " \
                                   "what a program keeps of a lend is what "  \
                                   "it copied out of one");                   \
                return false;                                                \
            }                                                                \
            fail(vmp, frame, instruction, "K0612", "this is not %s", what);  \
            return false;                                                    \
        }                                                                    \
    } while (0)

// How many words of the live bitmap that many slots need.
#define LIVE_WORDS(slots) (((size_t)(slots) + 63u) / 64u)

static bool is_live(const Store *store, uint32_t index) {
    return (store->live[index / 64u] & (UINT64_C(1) << (index % 64u))) != 0;
}

static void mark_live(Store *store, uint32_t index, bool live) {
    uint64_t bit = UINT64_C(1) << (index % 64u);
    if (live) {
        store->live[index / 64u] |= bit;
    } else {
        store->live[index / 64u] &= ~bit;
    }
}

// The first live slot at or after `from`, or -1. A store hands out slots that
// go dead, so a walk of one looks rather than counts -- and it looks a word at
// a time, so a run of dead slots is skipped sixty-four at a time rather than
// one at a time. The order is the order the slots are in, which is what the
// simulation profile promises a walk of a store is. See D954.
static int64_t live_from(const Store *store, int64_t from) {
    uint32_t at = from < 0 ? 0 : (uint32_t)from;
    while (at < store->used) {
        // What is left of the word this slot is in, with the slots before it
        // taken off: the first turn starts in the middle of a word and every
        // turn after it starts at the beginning of one.
        uint64_t word = store->live[at / 64u] >> (at % 64u);
        if (word != 0) {
            uint32_t found = at;
            while ((word & 1u) == 0) {
                word >>= 1;
                found++;
            }
            return found < store->used ? (int64_t)found : -1;
        }
        at = (at / 64u + 1u) * 64u;
    }
    return -1;
}

static void no_room(Vm *vm, const Frame *frame, const uint8_t *instruction,
                    const KestRuntime *rt);

static void fail(Vm *vm, const Frame *frame, const uint8_t *instruction,
                 const char *code, const char *format, ...) KEST_SAYS(5, 6);

// The same, handed the arguments already gathered. What says something about a
// frame a host filled and what says something about the frame it wrote back
// into are one walk with two sayings, and the second of them ends up here with
// a `va_list` in its hand. See D719.
// Where an instruction was written, which is what a refusal about it points
// at. A body the host's compiler compiled has no instruction and does have
// the offset the resolved form carried, so everything that reports takes one
// of these rather than the two it used to. See D1099.
static KestSpan where_it_is(const Frame *frame, const uint8_t *instruction) {
    uint32_t offset = (uint32_t)(instruction - frame->chunk->code);
    KestSpan span = {kest_chunk_origin(frame->chunk, offset), 1};
    return span;
}

// Where a door was asked from, which is a source offset from a body the
// host's compiler compiled and a word saying `the machine` from the machine.
// The second is turned into the first here, on the way to a message and
// nowhere else. See D1117.
static KestSpan where_asked(const KestRuntime *rt, uint32_t where) {
    if (where != KEST_WHERE_RUNNING) {
        KestSpan said = {where, 1};
        return said;
    }
    if (rt == NULL || rt->frame_count == 0 || rt->asked_at == NULL) {
        KestSpan nowhere = {0, 1};
        return nowhere;
    }
    return where_it_is(&rt->frames[rt->frame_count - 1], rt->asked_at);
}

// The same, where the place is already known rather than worked out from an
// instruction. A body the host's compiler compiled has no instruction to point
// at and does have the source offset the resolved form carried, so it comes in
// here and is reported exactly where the machine would have reported it. See
// D1094.
static void said_at(Vm *vm, const KestSource *source, KestSpan span,
                    const char *code, const char *format, va_list args) {
    kest_diags_in(vm->diags, source);
    kest_diags_addv(vm->diags, KEST_SEVERITY_ERROR, code, span, format, args);

    // And how it got here. Every frame under this one made a call, and its
    // `ip` is just past the instruction that made it, so the byte before is
    // where that call is written. Outermost first, so the notes read as the
    // way in rather than as the way back out.
    //
    // Eight is what a diagnostic holds; a run of calls deeper than that says
    // how many were left out, because a number is what a reader of a deep one
    // wants and the middle of it is not.
    uint32_t depth = vm->frame_count;
    uint32_t shown = depth > KEST_MOST_PLACES + 1 ? KEST_MOST_PLACES : depth - 1;
    for (uint32_t i = 1; i <= shown && i < depth; i++) {
        const Frame *caller = &vm->frames[i - 1];
        const KestChunk *chunk = caller->chunk;
        if (chunk == NULL || chunk->origins == NULL) {
            continue;
        }
        // A frame a compiled body made has no instruction to point at and
        // says where its call is instead; nought there and no instruction is
        // the front of the body, which is where a caller nobody can place
        // belongs. See D1122.
        uint32_t at = caller->ip == NULL || chunk->code == NULL
                          ? 0
                          : (uint32_t)(caller->ip - chunk->code);
        KestSpan call = {caller->said_at != 0
                             ? caller->said_at
                             : kest_chunk_origin(chunk, at > 0 ? at - 1 : 0),
                         1};
        const char *written = vm->frames[i].chunk->wrote;
        if (i == shown && depth - 1 > shown) {
            kest_diags_note(vm->diags, chunk->source, call,
                            "`%s` was called here, and %u more under it",
                            written, depth - 1 - shown);
        } else {
            kest_diags_note(vm->diags, chunk->source, call,
                            "`%s` was called here", written);
        }
    }
}

static void said_here(Vm *vm, const KestSource *source, KestSpan span,
                      const char *code, const char *format, ...) KEST_SAYS(5, 6);

static void said_here(Vm *vm, const KestSource *source, KestSpan span,
                      const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    said_at(vm, source, span, code, format, args);
    va_end(args);
}

static void failv(Vm *vm, const Frame *frame, const uint8_t *instruction,
                  const char *code, const char *format, va_list args) {
    // The file the instruction came from was set when it was compiled, and
    // the machine does not change it.
    said_at(vm, frame->chunk->source, where_it_is(frame, instruction), code,
            format, args);
}

static void fail(Vm *vm, const Frame *frame, const uint8_t *instruction,
                 const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    failv(vm, frame, instruction, code, format, args);
    va_end(args);
}

// An allocation that did not happen. Which of the two it was is the difference
// between a machine that has run out and a host that said this much and no
// more, and only one of those is anybody's mistake.
// What the allocation that did not happen was asking for. There are two places
// a running program's memory comes from and either may be the one that ran
// out, so the number is read from whichever refused: the ground forgets its
// refusal the moment it hands something out, so a number there is this
// refusal and not an older one.
static size_t was_refused(const KestRuntime *rt) {
    size_t ground = kest_ground_refused(rt->ground);
    return ground != 0 ? ground : kest_arena_refused(rt->heap);
}

static void no_room_at(Vm *vm, const KestSource *source, KestSpan span,
                       const KestRuntime *rt) {
    if (rt->heap_bytes != 0) {
        // What it has and what it wanted, because a program that missed by
        // eight bytes and one that missed by a megabyte are the same message
        // otherwise, and they are not the same problem.
        said_here(vm, source, span, "K0617",
                  "the program has used %zu of the %zu bytes it was given, "
                  "and this asked for %zu more",
                  kest_heap_used(rt), rt->heap_bytes, was_refused(rt));
        return;
    }
    // And the same two numbers when nobody set a ceiling, because a host
    // reading `out of memory` learns nothing it did not know: whether this is
    // a program that wants a gigabyte or a machine that has a megabyte left is
    // the whole of what it would do about it.
    said_here(vm, source, span, "K0605",
              "the program has used %zu bytes and this asked for %zu more, "
              "which this machine has not got",
              kest_heap_used(rt), was_refused(rt));
}

static void no_room(Vm *vm, const Frame *frame, const uint8_t *instruction,
                    const KestRuntime *rt) {
    no_room_at(vm, frame->chunk->source, where_it_is(frame, instruction), rt);
}

// And what it was doing when it ran out. What a host raises a ceiling by is not
// what the last allocation asked for: a thing that doubles will ask for the
// double again at the next one. What it needs to know is what was growing and
// how far along it was, which is what this says.
static void no_room_growing_at(Vm *vm, const KestSource *source, KestSpan span,
                               const KestRuntime *rt, const char *what,
                               uint32_t held, size_t each,
                               uint32_t growing_to) {
    no_room_at(vm, source, span, rt);
    kest_diags_suggest(vm->diags,
                       "it was %s holding %u of %zu bytes each, growing to %u",
                       what, held, each, growing_to);
}

static void no_room_growing(Vm *vm, const Frame *frame,
                            const uint8_t *instruction, const KestRuntime *rt,
                            const char *what, uint32_t held, size_t each,
                            uint32_t growing_to) {
    no_room_growing_at(vm, frame->chunk->source,
                       where_it_is(frame, instruction), rt, what, held, each,
                       growing_to);
}

// And what it would have needed, said where it ran out. A host picks the two
// numbers a machine is started with, and the only thing that says whether it
// picked well is the program — so the refusal carries the answer rather than
// sending a reader to `kest emit` for it: a number to ask for, or the reason
// there is not one. Worked out here rather than kept anywhere, because this
// happens once, on the way out. See D569.
static void what_it_needed(Vm *vm, const KestRuntime *rt, int32_t called) {
    uint32_t slots = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // The working out is memory, and the memory it takes is the program's:
    // a host that carries on after a refusal would be paying for this one out
    // of the heap it gave the program. So it is borrowed and put back — after
    // the words are written, because what a diagnostic is written into is the
    // build's arena and what a reason names is the program's own name for a
    // function, and neither is what this hands back. See D571.
    KestMark before = kest_arena_mark(rt->heap);
    if (kest_module_needs(rt->module, rt->heap, -1, &slots, &deep, NULL, NULL,
                          NULL, &why)) {
        kest_diags_suggest(vm->diags,
                           "this program needs %u slots and %u frames, and "
                           "this machine was given %u and %u",
                           slots, deep, rt->stack_slots, rt->call_depth);
        // And what the call a host made needs on its own, at the declaration
        // of the function it called. The number above is the worst of
        // everything the program defines, so a host sized for the functions it
        // calls is being told to go back to saying nothing — and what it wants
        // is the one it asked for, which is this. See D622.
        const KestChunk *one =
            called >= 0 && (uint32_t)called < rt->module->count
                ? rt->module->functions[called]
                : NULL;
        uint32_t its_slots = 0;
        uint32_t its_deep = 0;
        KestReason of_one = {KEST_REACH_UNASKED, NULL};
        if (one != NULL &&
            kest_module_needs(rt->module, rt->heap, called, &its_slots,
                              &its_deep, NULL, NULL, NULL, &of_one)) {
            kest_diags_note(vm->diags, one->source, one->declared,
                            "calling this needs %u slots and %u frames",
                            its_slots, its_deep);
        }
        kest_arena_rewind(rt->heap, before);
        return;
    }
    // And when the working out itself had nowhere to happen, which is a
    // machine that has spent its heap and then run off its stack: both at
    // once, and the second of them is what this was for. Said as what it is,
    // because `there is no number to ask for` would be this machine's trouble
    // written down as the program's — the number is there and this run cannot
    // reach it. See D570.
    if (why.reach == KEST_REACH_NO_ROOM) {
        kest_diags_suggest(vm->diags,
                           "what this program needs cannot be worked out with "
                           "the heap this machine has left");
        kest_arena_rewind(rt->heap, before);
        return;
    }
    // And when there is no number to ask for, which is a thing to be told
    // rather than a silence: a program that reaches itself or calls through a
    // value has no deepest call, so the host that picked a number was always
    // going to find out here, and this is where it does.
    // And what there is instead of a number, which is a bound: this machine
    // was sized from one, so a host reading a refusal is told the shape of it
    // — so much a frame and so much whatever the frames — rather than being
    // left to guess what the next size up costs. One saying and not two,
    // because a diagnostic holds one and the second would take the first's
    // place. See D820.
    uint32_t widest = 0;
    uint32_t in_a_turn = 0;
    uint32_t off_the_turns = 0;
    // Over what the call a host made reaches rather than over everything the
    // file defines. A host sizing a machine for one name asks `kest_bound_of`
    // about that name, so a refusal that answers about the whole program is
    // answering a question the host did not ask — and the arithmetic it hands
    // back is arithmetic about bodies the host never calls. Nought or less
    // where there is no call to name, which is a machine that could not be
    // made rather than one that ran out. See D821.
    kest_module_cycles(rt->module, rt->heap, called, false, &widest,
                       &in_a_turn, &off_the_turns);
    bool by_turns =
        in_a_turn > 0 &&
        (uint64_t)off_the_turns + (uint64_t)in_a_turn * rt->call_depth <
            (uint64_t)widest * rt->call_depth;
    uint32_t a_frame = by_turns ? in_a_turn : widest;
    uint32_t besides = by_turns ? off_the_turns : 0;
    if (widest == 0) {
        kest_diags_suggest(vm->diags, "there is no number to ask for: `%s` %s",
                           why.where == NULL ? "something here" : why.where,
                           kest_reach_name(why.reach));
        kest_arena_rewind(rt->heap, before);
        return;
    }
    kest_diags_suggest(vm->diags,
                       "there is no number to ask for: `%s` %s, and this "
                       "machine was given %u frame(s) of %u slot(s) each and "
                       "%u besides, so twice the frames is %llu slots",
                       why.where == NULL ? "something here" : why.where,
                       kest_reach_name(why.reach), rt->call_depth, a_frame,
                       besides,
                       (unsigned long long)(besides +
                                            (uint64_t)a_frame *
                                                rt->call_depth * 2));
    kest_arena_rewind(rt->heap, before);
}

// The next handout number, for the life of the process. Relaxed, because
// nothing about two machines is ordered by this and each only wants a number
// no other one will get. It starts at one so that nought is a number no place
// is ever stamped with, which makes a reference read out of memory that was
// never written name nothing.
//
// It is the one piece of mutable state this library keeps outside a machine,
// and it is here rather than on the build because two machines of two builds
// are two machines: a count on the build is what let a reference made in one
// of them read an object in the other, which is D936, and a count masked to
// sixteen bits is what let a reference made in one live world read an object
// in another, which is D1033.
// A block at a time, because the one shared thing in this library is the one
// thing that stops it scaling. A machine that does almost nothing but make
// places in a world ran no faster on eight threads than on one, and the whole
// of that was every `add` in the process writing one machine word; a workload
// that makes a world and then works on it scaled 2.5 times on eight. So a
// machine claims a thousand and twenty-four numbers with one write and hands
// them out to itself after that. The numbers a machine claims and does not
// use are never handed out again, which costs nothing: the ceiling is a
// million million and a process would have to make a million million machines
// to feel it. See D1053.
#define HANDOUT_BLOCK 1024ull

static uint64_t claim_handouts(void) {
    static atomic_ullong handed_out;
    return atomic_fetch_add_explicit(&handed_out, HANDOUT_BLOCK,
                                     memory_order_relaxed);
}

static uint64_t next_handout(Vm *rt) {
    if (rt->handout_next >= rt->handout_upto) {
        rt->handout_next = claim_handouts();
        rt->handout_upto = rt->handout_next + HANDOUT_BLOCK;
    }
    return ++rt->handout_next;
}

static int64_t pack_ref(uint64_t serial, uint32_t index) {
    return (int64_t)(((serial & REF_SERIAL_MASK) << REF_INDEX_BITS) |
                     (uint64_t)index);
}

// The slot a reference names, or NULL when what it named is gone.
// Which place a reference names. Read through this and nowhere else: the parts
// a reference is made of are this file's own and a second place that knew the
// widths is a second place to change. See D934.
static uint32_t ref_place(int64_t handle) {
    return (uint32_t)((uint64_t)handle & REF_INDEX_MASK);
}

static KestValue *resolve_ref(Store *store, int64_t handle) {
    uint32_t index = ref_place(handle);
    uint64_t serial = ((uint64_t)handle >> REF_INDEX_BITS) & REF_SERIAL_MASK;
    // One question rather than two. A handout number is the whole of the
    // authority: another machine's reference, another store's, and this
    // store's own from before the place was given back are all a number this
    // place was never stamped with. See D1033.
    if (index >= store->used || !is_live(store, index) ||
        store->serials[index] != serial) {
        return NULL;
    }
    return store->elements + (size_t)index * store->stride;
}

// Room for the elements of an array, with what they are in front of them. The
// one extra byte is what an array of nothing is: a block of nought bytes is a
// place nothing was handed out at, and two arrays with no elements would be
// the same array.
static unsigned char *elements_for(Vm *rt, KestValue *reach,
                                   const KestLayout *layout, uint32_t places) {
    size_t span = sizeof(Elems) + (size_t)places * layout->size + 1;
    Elems *head = take(rt, reach, span, KEST_GROUND_ELEMS);
    if (head == NULL) {
        return NULL;
    }
    head->layout = layout;
    head->places = places;
    head->stride = layout->size;
    head->loose = layout->tagged;
    head->follow_them = holds_addresses(layout);
    return (unsigned char *)(void *)(head + 1);
}

// And more of them, in the place they are in where it has the room. An array
// built by pushing costs what it holds rather than twice that, which is what
// this is for: the place a run of a hundred and twenty-eight bytes sits in
// holds two hundred and fifty-six.
static unsigned char *elements_grown(Vm *rt, KestValue *reach, Array *array,
                                     const KestLayout *layout,
                                     uint32_t capacity) {
    size_t want = sizeof(Elems) + (size_t)capacity * layout->size + 1;
    if (array->bytes != NULL) {
        Elems *head = ((Elems *)(void *)array->bytes) - 1;
        size_t had = sizeof(Elems) + (size_t)array->capacity * layout->size + 1;
        if (kest_ground_grow(rt->ground, head, had, want) != NULL) {
            head->places = capacity;
            return array->bytes;
        }
    }
    uint32_t hands = rt->hands;
    unsigned char *fresh = in_hand(rt, elements_for(rt, reach, layout, capacity));
    hands_off(rt, hands);
    if (fresh == NULL) {
        return NULL;
    }
    if (array->bytes != NULL && array->length > 0) {
        rt->copied += (uint64_t)array->length * layout->size;
        memcpy(fresh, array->bytes, (size_t)array->length * layout->size);
    }
    return fresh;
}

// How many elements the place a run of them is in will hold, which is not how
// many were asked for: a place is as wide as the step above what was asked
// for, and a caller that fills it grows fewer times and copies less. What is
// past the length is nought, because that is what the place was handed out as.
static uint32_t all_it_holds(Vm *rt, unsigned char *bytes,
                             const KestLayout *layout, uint32_t asked) {
    if (bytes == NULL || layout->size == 0) {
        return asked;
    }
    size_t room = kest_ground_room(rt->ground, ((const Elems *)(const void *)bytes) - 1);
    if (room <= sizeof(Elems) + 1) {
        return asked;
    }
    size_t holds = (room - sizeof(Elems) - 1) / layout->size;
    if (holds <= asked || holds > UINT32_MAX) {
        return asked;
    }
    // Asked for through the door that grows one, because what is past what was
    // asked for is not the caller's until it says so: the sanitised build has
    // the rest of the place closed, and this is where it opens.
    if (kest_ground_grow(rt->ground, ((Elems *)(void *)bytes) - 1,
                         sizeof(Elems) + (size_t)asked * layout->size + 1,
                         sizeof(Elems) + holds * layout->size + 1) == NULL) {
        return asked;
    }
    return (uint32_t)holds;
}

// Room for that many, which is what growing is and what being told how many
// there will be is. The four runs beside each other are what a slot costs: the
// value, how many times the slot has been used, whether it is live, and the
// list of the ones that are not.
static bool room_for(Vm *rt, KestValue *reach, Store *store,
                     uint32_t capacity) {
    // Four takes rather than one, and any of them may set off a walk that
    // would find the ones before it named by nothing. So each is held until
    // the store names them all.
    uint32_t hands = rt->hands;
    KestValue *elements =
        in_hand(rt, take(rt, reach,
                         sizeof(KestValue) * (size_t)capacity * store->stride,
                         KEST_GROUND_PLAIN));
    uint64_t *serials =
        elements == NULL ? NULL
                         : in_hand(rt, take(rt, reach,
                                            sizeof(uint64_t) * capacity,
                                            KEST_GROUND_PLAIN));
    uint64_t *live =
        serials == NULL
            ? NULL
            : in_hand(rt, take(rt, reach, sizeof(uint64_t) * LIVE_WORDS(capacity),
                               KEST_GROUND_PLAIN));
    uint32_t *free_slots =
        live == NULL ? NULL
                     : take(rt, reach, sizeof(uint32_t) * capacity,
                            KEST_GROUND_PLAIN);
    hands_off(rt, hands);
    if (elements == NULL || serials == NULL || live == NULL ||
        free_slots == NULL) {
        return false;
    }
    if (store->used > 0) {
        rt->copied += (uint64_t)sizeof(KestValue) * store->used * store->stride;
        memcpy(elements, store->elements,
               sizeof(KestValue) * store->used * store->stride);
        memcpy(serials, store->serials, sizeof(uint64_t) * store->used);
        memcpy(live, store->live, sizeof(uint64_t) * LIVE_WORDS(store->used));
    }
    if (store->free_count > 0) {
        memcpy(free_slots, store->free_slots,
               sizeof(uint32_t) * store->free_count);
    }
    store->elements = elements;
    store->serials = serials;
    store->live = live;
    store->free_slots = free_slots;
    store->capacity = capacity;
    return true;
}

static bool grow_store(Vm *rt, KestValue *reach, Store *store) {
    return room_for(rt, reach, store, store->capacity == 0 ? 8
                                                      : store->capacity * 2);
}

// Which end of a call a frame is being read at. The two ends find the same
// things and do not say the same sentence: a host filling a frame is told what
// the function takes, at no line of the program, because nothing has run yet; a
// host writing back into one is told what the crossing answers with, at the
// line that asked for it and under the calls that got there.
//
// The words are written out twice rather than made out of one sentence and a
// verb, because a code and the message it is raised with are a literal and the
// literal after it everywhere else in this tree, and a check reads them that
// way. One walk, two sayings, and each says its own. See D719.
typedef struct {
    bool at_a_crossing;
    const Frame *frame;
    // What the machine was running when it asked, or nothing for a body the
    // host's compiler compiled: that one has no instructions at all and does
    // have the source offset the resolved form carried, which is `where`.
    // See D1108.
    const uint8_t *instruction;
    uint32_t where;
} Saying;

// What a walk over a frame says when something in it is wrong, said the way
// the engine that asked says everything else. One helper rather than a test
// at each of the places that say something, because a walk that reports
// eleven things is eleven places to forget.
static void crossing_failed(KestRuntime *runtime, const Saying *saying,
                            const char *code, const char *format, ...)
    KEST_SAYS(4, 5);

static void crossing_failed(KestRuntime *runtime, const Saying *saying,
                            const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (saying->instruction != NULL) {
        failv(runtime, saying->frame, saying->instruction, code, format, args);
    } else {
        char said[200];
        vsnprintf(said, sizeof said, format, args);
        kest_native_stopped(runtime, saying->where, code, said);
    }
    va_end(args);
}

#if KEST_CHECKED
// A run of the chunk's own constants, held to being the chunk's. The same
// number written by the same hand as the slots below, into the same kind of
// instruction, and reaching past the other end of a body: what follows the
// constants in the array is room the arena handed out and nobody wrote, so a
// run one long is a value read out of memory that is nought by luck rather
// than by anybody's decision. See D904.
static bool own_constants(Vm *vm, const Frame *frame,
                          const uint8_t *instruction, uint32_t past) {
    vm->guarded++;
    if (past <= frame->chunk->constant_count) {
        return true;
    }
    fail(vm, frame, instruction, "K0655",
         "this reads constant %u of the %u this body was given", past,
         frame->chunk->constant_count);
    kest_diags_fault(vm->diags,
                     "what the compiler wrote into an instruction and what the "
                     "body holds disagree");
    return false;
}

// A run of slots a body names, held to being the body's. Everything inside a
// frame is reached by a number the compiler wrote into the instruction, and a
// count one out reads the slot above the value or writes over the one below
// it -- inside the body, where the guards where a frame changes hands cannot
// see. The named slots are what a body was given and the operand stack is
// above them, so a read that runs past the names is a read of what the body
// was in the middle of working out. See D903.
static bool own_slots(Vm *vm, const Frame *frame, const uint8_t *instruction,
                      uint32_t first, uint32_t past) {
    vm->guarded++;
    if (past <= frame->chunk->slot_count && first <= past) {
        return true;
    }
    fail(vm, frame, instruction, "K0655",
         "this reaches slot %u of the %u this body names", past,
         frame->chunk->slot_count);
    kest_diags_fault(vm->diags,
                     "what the compiler wrote into an instruction and what the "
                     "body holds disagree");
    return false;
}
#endif

// Whether a number a host wrote fits the width the piece it sits in says. A
// slot is sixty-four bits and a piece may be eight, and a number as wide as
// the slot cannot be wrong — so only the narrow ones are asked, off the piece
// and not off the type. See D836.
static bool fits_the_piece(uint8_t kind, KestValue given_as) {
    int64_t given = given_as.integer;
    switch (kind) {
    // A slot holds a double and an `f32` holds less, so a host writing one no
    // `f32` can hold is the same door at a different width: what comes out is
    // an `f32` the program's own `f32` literals do not equal. Not a number is
    // not a number at either width, so it is let through. See D838.
    case KEST_L_F32:
        return given_as.real != given_as.real ||
               (double)(float)given_as.real == given_as.real;
    case KEST_L_I8:
        return given >= INT8_MIN && given <= INT8_MAX;
    case KEST_L_I16:
        return given >= INT16_MIN && given <= INT16_MAX;
    case KEST_L_I32:
        return given >= INT32_MIN && given <= INT32_MAX;
    case KEST_L_U8:
        return given >= 0 && given <= UINT8_MAX;
    // A truth holds one of two, and a slot holds sixty-four bits. See D839.
    case KEST_L_BOOL:
        return given == 0 || given == 1;
    case KEST_L_U16:
        return given >= 0 && given <= UINT16_MAX;
    case KEST_L_U32:
        return given >= 0 && given <= UINT32_MAX;
    default:
        return true;
    }
}


// And what it is called, which is the piece's own word for itself: a host
// reading this is looking at a slot it filled and wants the width it was
// supposed to fill it to.
static const char *the_width_of(uint8_t kind) {
    switch (kind) {
    case KEST_L_I8:
        return "i8";
    case KEST_L_I16:
        return "i16";
    case KEST_L_I32:
        return "i32";
    case KEST_L_U8:
        return "u8";
    case KEST_L_U16:
        return "u16";
    case KEST_L_U32:
        return "u32";
    case KEST_L_F32:
        return "f32";
    case KEST_L_BOOL:
        return "bool";
    default:
        return "a whole number";
    }
}

// And the same off a type, for the walk that has one. A number's own word for
// itself is what a host reading this wants: it is looking at a slot it filled
// or answered with, and wants the width it was supposed to fill it to.
static const char *the_width_of_type(const KestType *type) {
    return the_width_of(kest_scalar_of(type));
}

static void narrower_than_that(KestRuntime *runtime, const char *name,
                               uint32_t at, uint8_t kind, KestValue given) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    if (kind == KEST_L_F32) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636", nowhere,
                       "`%s` takes `f32` in slot %u and %.17g is not one",
                       name, at, given.real);
    } else {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636", nowhere,
                       "`%s` takes `%s` in slot %u and %lld is not one", name,
                       the_width_of(kind), at, (long long)given.integer);
    }
    kest_diags_suggest(runtime->diags,
                       "every width wraps at its own end, and a host narrows "
                       "what it writes the way `u8(n)` does");
}

// What a host handed over in one argument, read by what the argument is rather
// than by what its first piece is. Every check here was a check on the type of
// the whole argument, so a piece of text inside a shape crossed unread: a host
// filling a `struct Npc { name: text, health: i32 }` wrote a pointer of its own
// into the first slot and the program read it as text the machine owned. What
// makes that readable is walking the type the way its slots are laid out, which
// is the same walk `describe` makes and `enum_at` counts. See D718.
//
// `at` is where in the frame this value starts, and it comes back where the
// next one does.
static bool handed_well(KestRuntime *runtime, const Saying *saying,
                        const char *name, const KestType *type,
                        const KestValue *frame, uint32_t *at) {
    KestSpan nowhere = {0, 0};
    if (type == NULL) {
        *at += 1;
        return true;
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (!handed_well(runtime, saying, name, type->members[i].type, frame,
                             at)) {
                return false;
            }
        }
        return true;
    }
    if (type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            if (!handed_well(runtime, saying, name, type->element, frame, at)) {
                return false;
            }
        }
        return true;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        // The flag first, because what is in the value is the flag's to say:
        // an empty one is nought in as many slots as the value takes, and
        // nought where text goes is a slot nobody filled rather than a slot
        // filled wrongly.
        uint16_t wide = type->element->slots == 0 ? 1 : type->element->slots;
        if (frame[*at + wide].integer != 0) {
            if (!handed_well(runtime, saying, name, type->element, frame, at)) {
                return false;
            }
        } else {
            *at += wide;
        }
        *at += 1;
        return true;
    }
    if (type->tag == KEST_T_ENUM) {
        int32_t tag = (int32_t)frame[*at].integer;
        if (tag < 0 || (uint32_t)tag >= type->case_count) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0650",
                     "`%s` answers with a tag in slot %u and %lld is no case "
                     "of it",
                     name, *at, (long long)frame[*at].integer);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes a tag in slot %u and %lld is no "
                               "case of it",
                               name, *at, (long long)frame[*at].integer);
            }
            kest_diags_suggest(runtime->diags,
                               "`kest_case_of` names the cases, and a tag it "
                               "answers nothing for is one nothing here can "
                               "read");
            return false;
        }
        // And what the case carries, which is where a piece of text inside a
        // value with a tag in it is: the slots after the tag are the case's,
        // at the offsets it says.
        const KestVariantType *variant = &type->cases[tag];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            uint32_t inside = *at + variant->offsets[p];
            if (!handed_well(runtime, saying, name, variant->payload[p],
                             frame, &inside)) {
                return false;
            }
        }
        *at += type->slots == 0 ? 1 : type->slots;
        return true;
    }
    if (type->tag == KEST_T_TEXT) {
        // Text is the same question with two places to look: what a program
        // holds is either on the heap, where anything made while running goes,
        // or in the arena the program was compiled into, where the text a file
        // wrote lives. A host's own string is in neither, and a host handing
        // one over is undertaking to keep it as long as the program holds it,
        // which is what `kest_text` exists so that nobody has to do.
        //
        // And nothing at all in a slot that takes text, which is what a host
        // that zeroed a frame and called anyway hands over. Text in this
        // language is never nothing — an empty piece of it is a piece of it —
        // so a slot holding no address is a host that has not filled the
        // frame, and the program reads it at the first thing it does with it.
        // See D629.
        if (frame[*at].text == NULL) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with text in slot %u and there is no "
                     "address there",
                     name, *at);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes text in slot %u and this host "
                               "handed no address",
                               name, *at);
            }
            kest_diags_suggest(runtime->diags,
                               "`kest_text` makes text the machine keeps, and "
                               "an empty piece of it is text as well");
            return false;
        }
        if (!ours(runtime, frame[*at].text) &&
            !kest_arena_holds(runtime->module->arena, frame[*at].text)) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with text in slot %u that did not come "
                     "from this machine",
                     name, *at);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes text in slot %u and this did not "
                               "come from this machine",
                               name, *at);
            }
            kest_diags_suggest(runtime->diags,
                               "`kest_text` copies a host's bytes onto the "
                               "heap, and what it answers is what to hand "
                               "over");
            return false;
        }
        // Two slots: what it is made of, and how many bytes that is. See D964.
        *at += 2;
        return true;
    }
    if (type->tag == KEST_T_ARRAY || type->tag == KEST_T_STORE) {
        // And a handle slot nobody filled, which is the same mistake as the
        // one above and was caught in a different place: the machine reads the
        // four bytes at the front of a handle at the instruction that uses it
        // and says `K0612` there, which points at the program for something
        // the host did. Said at the door, it names the slot. See D630.
        if (frame[*at].object == NULL) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with a handle in slot %u and there is none "
                     "there",
                     name, *at);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes a handle in slot %u and this host "
                               "handed no handle",
                               name, *at);
            }
            kest_diags_suggest(runtime->diags,
                               "a handle is what `kest_call` or `kest_borrow` "
                               "gave back, and a frame of noughts is a frame "
                               "nobody filled");
            return false;
        }
        if (!ours(runtime, frame[*at].object)) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with a handle in slot %u that did not come "
                     "from this machine",
                     name, *at);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes a handle in slot %u and this one "
                               "did not come from this machine",
                               name, *at);
            }
            kest_diags_suggest(runtime->diags,
                               "a handle is what `kest_call` or `kest_borrow` "
                               "gave back, and it belongs to the machine that "
                               "gave it");
            return false;
        }
        // And which of the two kinds of handle it is. Both headers begin with
        // what they are, so a handle can be asked that without knowing what it
        // was meant to be — and until it was asked here, a store handed where
        // an array was wanted got as far as the instruction that walked it,
        // which said `K0612` about the program for something the host did.
        // Said at the door, it names the slot and what was in it. See D630 and
        // D716.
        if (!KEST_HANDLE_IS(frame[*at].object, type->tag == KEST_T_ARRAY
                                                   ? KEST_IS_ARRAY
                                                   : KEST_IS_STORE)) {
            // A lend that has been taken back is its own answer and keeps it
            // here: it is not the wrong kind of handle, it is memory the host
            // said it was done with, and the program is told which of those
            // two things happened.
            if (KEST_HANDLE_IS(frame[*at].object, KEST_WAS_LENT)) {
                if (saying->at_a_crossing) {
                    crossing_failed(runtime, saying, "K0637",
                         "`%s` answers with a handle in slot %u the host has "
                         "taken back",
                         name, *at);
                } else {
                    kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR,
                                   "K0637", nowhere,
                                   "`%s` takes a handle in slot %u and the "
                                   "host has taken this lend back",
                                   name, *at);
                }
                kest_diags_suggest(runtime->diags,
                                   "the block is the host's and it said so; "
                                   "what a program keeps of a lend is what it "
                                   "copied out of one");
                return false;
            }
            const char *asked_for =
                type->tag == KEST_T_ARRAY ? "an array" : "a store";
            const char *handed =
                KEST_HANDLE_IS(frame[*at].object, KEST_IS_ARRAY)
                    ? "an array"
                    : KEST_HANDLE_IS(frame[*at].object, KEST_IS_STORE)
                          ? "a store"
                          : "something this machine did not make";
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with %s in slot %u and this host wrote %s",
                     name, asked_for, *at, handed);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes %s in slot %u and this host handed "
                               "%s",
                               name, asked_for, *at, handed);
            }
            kest_diags_suggest(runtime->diags,
                               "both kinds of handle say what they are, and "
                               "what a program asks for is what its "
                               "declaration says");
            return false;
        }
        // And what one of them holds, which is the thing a kind and a stride
        // between them cannot say. Two `i32` and four `f32` are eight bytes
        // and sixteen, and a host that lent the first where the second was
        // wanted was read past the end of its own memory and told nobody --
        // the machine unpacked whatever was there by the callee's layout. A
        // type is the build's own and there is one of each, so this is a
        // pointer against a pointer at the one door a handle can arrive
        // through. See D927.
        const KestType *holds =
            KEST_HANDLE_IS(frame[*at].object, KEST_IS_ARRAY)
                ? ((const Array *)frame[*at].object)->of
                : ((const Store *)frame[*at].object)->of;
        if (type->element != NULL && holds != type->element) {
            const char *wanted =
                kest_type_name(runtime->diags->arena, type->element);
            const char *given =
                holds == NULL ? "something made before this machine said what "
                                "its handles hold"
                              : kest_type_name(runtime->diags->arena, holds);
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0661",
                     "`%s` answers in slot %u with a handle of `%s` where one "
                     "of `%s` was wanted",
                     name, *at, given, wanted);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0661",
                               nowhere,
                               "`%s` takes a handle of `%s` in slot %u and "
                               "this host handed one of `%s`",
                               name, wanted, *at, given);
            }
            kest_diags_suggest(runtime->diags,
                               "a handle carries what it is of; lend the type "
                               "the program asks for, which is what "
                               "`kest_borrow` was given the name of");
            return false;
        }
        *at += 1;
        return true;
    }
    // And a number, against the width the program keeps it at. The walk above
    // is for the slots that hold something this machine made; this is the slot
    // that holds what the host wrote, and a slot is sixty-four bits where a
    // `u8` is eight. D836 weighed the arguments that are only numbers, in the
    // loop that decides whether to walk at all — this is the other two ways
    // in: a number inside a shape that has text or a handle somewhere else in
    // it, and a number a host answers a crossing with. See D837.
    // A set of bits, against the bits its names cover. A set is not the whole
    // number it is kept in: which bit a name stands for is where it was
    // written, so a bit nothing named is a value the program cannot make and
    // its own text does not say — `State` with every bit set writes itself as
    // the two it has names for, and compares unequal to what it just wrote.
    // See D840.
    if (type->tag == KEST_T_FLAGS) {
        uint64_t named = type->case_count >= 64
                             ? ~(uint64_t)0
                             : ((uint64_t)1 << type->case_count) - 1;
        uint64_t given = (uint64_t)frame[*at].integer;
        if ((given & ~named) != 0) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with `%s` in slot %u and %llu has bits it "
                     "has no names for",
                     name, kest_type_written(type), *at,
                     (unsigned long long)given);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes `%s` in slot %u and %llu has bits "
                               "it has no names for",
                               name, kest_type_written(type), *at,
                               (unsigned long long)given);
            }
            kest_diags_suggest(runtime->diags,
                               "which bit a name stands for is where it was "
                               "written, and a set is the bits it has names "
                               "for");
            return false;
        }
    }
    if (type->tag == KEST_T_FLOAT && type->width == 32) {
        double given = frame[*at].real;
        if (given == given && (double)(float)given != given) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with `f32` in slot %u and %.17g is not one",
                     name, *at, given);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes `f32` in slot %u and %.17g is not "
                               "one",
                               name, *at, given);
            }
            kest_diags_suggest(runtime->diags,
                               "every width wraps at its own end and a truth "
                               "is one of two, and a host writes what the "
                               "program could have made");
            return false;
        }
    }
    if (type->tag == KEST_T_INT) {
        int64_t given = frame[*at].integer;
        if (kest_narrow_to(kest_scalar_of(type), given) != given) {
            if (saying->at_a_crossing) {
                crossing_failed(runtime, saying, "K0652",
                     "`%s` answers with `%s` in slot %u and %lld is not one",
                     name, the_width_of_type(type), *at, (long long)given);
            } else {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                               nowhere,
                               "`%s` takes `%s` in slot %u and %lld is not "
                               "one",
                               name, the_width_of_type(type), *at,
                               (long long)given);
            }
            kest_diags_suggest(runtime->diags,
                               "every width wraps at its own end and a truth "
                               "is one of two, and a host writes what the "
                               "program could have made");
            return false;
        }
    }
    *at += type->slots == 0 ? 1 : type->slots;
    return true;
}

// How much of a budget the loop takes at a time. The counter cannot live in the
// machine and be read every instruction: nothing tells the compiler that moving
// slots is not writing it, so it becomes a load and a store per instruction and
// costs a third of everything -- which is the same reason `ip`, this body's
// slots and its constants are held in locals (D869, D872). So the loop holds a
// slice in a register and comes here when it runs out. A thousand instructions
// is under two microseconds, which is what a host asking a program to stop
// waits for, and the cold half then costs a thousandth of nothing.
#define FUEL_SLICE 1024u

// What work costs, where an instruction does as much of it as the program
// asked for: a unit for every this many bytes or elements. It is a divisor
// rather than a rate so that work and steps are counted in one currency, and
// it is here rather than inside the loop because a host spends at the same
// rate through `kest_fuel_spend`. See D950.
#define FUEL_PER_UNIT 64u

// The next slice, or nought for a run that is over. Nought means one of two
// things and the flag beside the counter is what says which: a host that asked
// this program to stop, or a budget that is spent.
static uint64_t take_fuel(KestRuntime *rt) {
    if (atomic_load_explicit(&rt->cancel_asked, memory_order_relaxed) != 0) {
        return 0;
    }
    if (!rt->fuel_bounded) {
        return FUEL_SLICE;
    }
    uint64_t take =
        rt->fuel_left < FUEL_SLICE ? rt->fuel_left : (uint64_t)FUEL_SLICE;
    rt->fuel_left -= take;
    return take;
}

// What a run that stopped says. Two reasons, neither of them a mistake in the
// program: a budget that is spent, and a host that asked. See D921.
static bool stopped_here(Vm *vmp, KestRuntime *rt, Frame *frame,
                         const uint8_t *instruction) {
    if (atomic_load_explicit(&rt->cancel_asked, memory_order_relaxed) != 0) {
        fail(vmp, frame, instruction, "K0660",
             "the host asked this program to stop");
        return false;
    }
    fail(vmp, frame, instruction, "K0659",
         "this program has taken the %llu step(s) it was given",
         (unsigned long long)rt->fuel_given);
    kest_diags_suggest(rt->diags,
                       "give it more with `kest_fuel_set` and call again: "
                       "what it built is still there");
    return false;
}

// Two bytes of a stream of instructions, little end first, stepping past them.
static uint16_t read_u16(const uint8_t **ip) {
    const uint8_t *at = *ip;
    *ip = at + 2;
    return (uint16_t)(at[0] | ((uint16_t)at[1] << 8));
}

// The three questions asked before a call through a function value enters
// anything: whether the value names a function at all, whether that function
// is of the shape the call was written against, and whether it keeps what the
// body making the call promised. Answers the chunk to enter, or nothing with
// the code, the sentence and the note that goes under it -- so that the
// machine and a body the host's compiler compiled refuse the same call with
// the same words rather than with two copies of them. See D1107.
//
// A program cannot get here with the wrong shape: the shape is the type and
// the type is checked. What can is a host, which writes a number into the
// slot and a number is only in range or not (D835). The promise is asked here
// because which chunk this enters is not known until it runs, which is the
// one call the proof over the emitted code cannot see through (D058, D853).
static const KestChunk *the_function(const KestModule *module,
                                     const KestChunk *mine, int64_t which,
                                     uint16_t handed, uint16_t coming_back,
                                     const char **code, char *said,
                                     size_t room, const char **suggest,
                                     const char **fault) {
    *code = NULL;
    *suggest = NULL;
    *fault = NULL;
    if (module == NULL || mine == NULL || which < 0 ||
        (uint64_t)which >= module->count) {
        *code = "K0609";
        snprintf(said, room, "this is not a function");
        return NULL;
    }
    const KestChunk *callee = module->functions[which];
    if (callee->param_slots != handed || callee->result_slots != coming_back) {
        *code = "K0657";
        snprintf(said, room,
                 "this calls something taking %u slot(s) and giving %u, and "
                 "`%s` takes %u and gives %u",
                 handed, coming_back, callee->wrote, callee->param_slots,
                 callee->result_slots);
        *suggest = "a function value is one slot holding which function it "
                   "is, and `kest_entry` is what a host reads one from";
        return NULL;
    }
    // Either promise, and the one that is broken is the one said: a body
    // promising both and entering one that promises neither is two things
    // wrong with one call, and a reader told the first fixes it and is told
    // the second. See D853.
    const char *broken = mine->no_alloc && !callee->no_alloc ? "no.alloc"
                         : mine->no_host && !callee->no_host ? "no.host"
                                                             : NULL;
    if (broken != NULL) {
        *code = "K0623";
        snprintf(said, room,
                 "`%s` promises `%s` and this enters `%s`, which does not",
                 mine->wrote, broken, callee->wrote);
        *fault = "the shape it was held in promises and the body does not";
        return NULL;
    }
    return callee;
}

// A walk over text read past its end, said away from the loop that reads a
// byte a turn, so the loop is the one compare. See D1245.
#if defined(__GNUC__)
__attribute__((cold, noinline))
#endif
static bool walked_past(Vm *vmp, Frame *frame, const uint8_t *instruction,
                        int64_t index, uint32_t length) {
    fail(vmp, frame, instruction, "K0645",
         "a walk read byte %lld of text of %u bytes", (long long)index,
         length);
    kest_diags_fault(vmp->diags, "a walk over text takes its length before "
                                 "its first turn and reads without asking");
    return false;
}

// `entry` of -1 is a machine carrying on from where a debugger stopped it:
// the frames are where they were, and the instruction and the operand stack
// come out of the frame the stop wrote them into. See D991.
static bool run_body(KestRuntime *rt, int32_t entry, uint16_t arg_slots,
                     uint16_t *returned) {
    bool carrying_on = entry < 0;
    const KestModule *module = rt->module;
    Vm *vmp = rt;

    const KestChunk *chunk =
        carrying_on ? rt->frames[rt->frame_count - 1].chunk
                    : module->functions[entry];
    // Where this run of the machine starts. Nothing is running unless a host
    // function called back in, and then it starts above what that one left.
    KestValue *floor = rt->running_top != NULL ? rt->running_top : rt->stack;
    uint32_t under = rt->running_frames;
    if (!carrying_on &&
        (under >= rt->call_depth ||
         floor + chunk->slot_count + chunk->stack_needed > rt->limit)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(rt->diags, NULL);
        kest_diags_add(rt->diags, KEST_SEVERITY_ERROR, "K0602", nowhere,
                       "there is no room to call in from here");
        // And what there would have been room in, which is the same answer the
        // two refusals inside a run carry: a host that picked the numbers is
        // told the ones to ask for rather than left to find them. This is the
        // door a host meets first, so it is the one most likely to be met by a
        // host that has not asked at all. See D571.
        what_it_needed(vmp, rt, entry);
        return false;
    }

    Frame *frame = NULL;
    KestValue *top = NULL;
    if (carrying_on) {
        frame = &rt->frames[rt->frame_count - 1];
        top = rt->stopped_top;
    } else {
        rt->frame_count = under;
        frame = &rt->frames[rt->frame_count++];
        FRESH(frame);
        frame->chunk = chunk;
        frame->ip = chunk->code;
        frame->base = floor;
        frame->said_at = 0;
        top = floor + (chunk->slot_count > arg_slots ? chunk->slot_count
                                                     : arg_slots);
        // A run that enters a body the host's compiler compiled is that body
        // and nothing else: there are no instructions to walk. It is entered
        // the same way a call enters one -- the frame is pushed, the
        // arguments are where the caller left them, and the answer goes back
        // over them. See D1094.
        if (chunk->native != NULL && !rt->untrusted) {
            KestValue *was_top = rt->running_top;
            rt->running_top = floor + chunk->slot_count + chunk->stack_needed;
            uint16_t gave = 0;
            bool went = chunk->native(rt, floor, &gave);
            rt->running_top = was_top;
            rt->frame_count--;
            if (went && returned != NULL) {
                *returned = gave;
            }
            return went;
        }
    }
    // Where the machine is, held here rather than in the frame. Every
    // instruction reads at least one byte and most read two more, and through a
    // pointer that is a load and a store each time: nothing tells the compiler
    // that a `memcpy` into the stack cannot be writing the frame it is reading
    // the position out of. A local is a register, and the frame is written back
    // at the three places anything else looks at it — under a call, before a
    // host runs, and on the way back out of one. See D869.
    const uint8_t *ip = frame->ip;
    // And where this body's slots are, held for the same reason. `load`,
    // `store` and the instruction a walk turns on all read it, which is three
    // of the four instructions a hop of a `for` is made of. Nothing writes it
    // between a call and the return that undoes the call, so it goes back into
    // the frame at those two places and nowhere else. See D869.
    KestValue *mine = frame->base;
    // And where this body's constants are. Every number, every piece of text
    // and every shape written into a program is read from here, and a frame
    // step an entity reads eight of them — each one a load of `frame->chunk`
    // and then a load of what it points at, because nothing tells the compiler
    // that moving slots cannot be writing the frame. The chunk a body is
    // running does not change while it runs, so this changes where the body
    // does. See D872.
    const KestValue *constants = frame->chunk->constants;
    // And what is left of this run's budget, held here for the same reason as
    // the three above and given back at the one place this returns having
    // worked. A run that ends in a refusal does not give it back, because a
    // machine that refused is one a host gives fuel to before it calls again.
    uint64_t slice = take_fuel(rt);
    // Asked before anything runs. A body with no jump in it and no call has no
    // step to spend, so a machine somebody had asked to stop ran it to the end
    // and answered as though nobody had -- which is the one case a host that
    // cancels most wants refused. See D929.
    if (slice == 0) {
        frame->ip = ip;
        return stopped_here(vmp, rt, frame, ip);
    }
// A step of the budget, spent where a program can do something again: a jump
// that goes back, and a call. Everything unbounded a program can do is one of
// those two -- code is finite, so a run that never ends is going round or going
// deeper -- and a check at each of them costs the loop nothing, where the same
// check on every instruction cost a sixth of everything. What it does not bound
// is a long body with no loop in it, which is bounded by the program's own
// size. See D921.
#define SPEND()                                                                \
    do {                                                                       \
        if (slice == 0) {                                                      \
            slice = take_fuel(rt);                                             \
            if (slice == 0) {                                                  \
                frame->ip = ip;                                                \
                return stopped_here(vmp, rt, frame, instruction);              \
            }                                                                  \
        }                                                                      \
        slice--;                                                               \
    } while (0)
// And a step for work one instruction does that is not one step. Copying a
// piece of text, filling a run of something, making room for it: a single
// opcode there does as much work as the program asked for, and a budget that
// counted a megabyte copy as one step is a budget a program can spend a second
// inside without spending a unit of. One unit per FUEL_PER_UNIT bytes or
// elements, on top of the one the instruction itself costs.
//
// Charged where the amount is known, which for a piece of text this had to
// read is after reading it: a budget may go over by one instruction's work and
// not by more, and the alternative is measuring everything twice. See D950.
#define SPEND_WORK(work)                                                       \
    do {                                                                       \
        if (rt->fuel_bounded) {                                                \
            uint64_t owed = (uint64_t)(work) / FUEL_PER_UNIT;                  \
            while (owed > slice) {                                             \
                owed -= slice;                                                 \
                slice = take_fuel(rt);                                         \
                if (slice == 0) {                                              \
                    frame->ip = ip;                                            \
                    return stopped_here(vmp, rt, frame, instruction);          \
                }                                                              \
            }                                                                  \
            slice -= owed;                                                     \
        }                                                                      \
    } while (0)
#define READ_BYTE() (*ip++)
// How one instruction hands over to the next. Where the host's compiler can
// take the address of a label, each jumps from its own end through a table;
// everywhere else, and in the build that checks itself, which counts every
// instruction at the top of the loop, it is the end of the switch. See D1181.
#if KEST_THREADED
#define THREADED(op) thread_##op:
#define NEXT                                                                   \
    do {                                                                       \
        instruction = ip;                                                      \
        goto *threaded[*ip++];                                                 \
    } while (0)
#else
#define THREADED(op)
#define NEXT break
#endif
// Through a function rather than a comma expression, because a comma inside a
// subscript is a thing one of the two compilers this is built with warns
// about wherever it appears -- and `module->layout_types[READ_U16()]` is
// exactly that. What it does is the same either way. See D970.
#define READ_U16() read_u16(&ip)

    // One macro per storage class rather than thirty near-identical cases.
    // The operands are already the right kind: the compiler chose which
    // instruction this is by reading the type the checker resolved.
// How far two pieces of text had to be read to be put in order, which is what
// comparing them costs: it stops at the first byte that differs, so two long
// ones that differ early are cheap and two long ones that are the same are
// not. Counted rather than left to `strcmp`, because what is charged for has
// to be what was done. See D950.
// A piece of text off the stack and a piece of text onto it. Text is two slots
// -- what it is made of, and how many bytes that is -- so its length is part
// of it rather than something to go and count. See D964.
#define TEXT_OFF() (top -= 2, said(top))
#define TEXT_ON(from, many)                                                    \
    do {                                                                       \
        (top++)->text = (from);                                                \
        (top++)->integer = (int64_t)(many);                                    \
    } while (0)

// Two pieces of text put in order, which is one answer the machine and the
// folder share rather than two (D668, D1103). How far it had to read is what
// comparing them costs and is charged for here: it stops at the first byte
// that differs, so two long ones that differ early are cheap and two long
// ones that are the same are not. Counted rather than left to `memcmp`,
// because what is charged for has to be what was done. See D950.
#define TEXT_ORDER(test)                                                       \
    do {                                                                       \
        Said right = TEXT_OFF();                                               \
        Said left = TEXT_OFF();                                                \
        int64_t read = 0;                                                      \
        int64_t order =                                                        \
            kest_text_order(left.bytes, (int64_t)left.length, right.bytes,     \
                            (int64_t)right.length, &read);                     \
        (top++)->integer = (test);                                             \
        SPEND_WORK(read);                                                      \
    } while (0)

// Bytes moved, counted in the build that counts instructions and nowhere
// else. See D1023.
#if KEST_CHECKED
#define MOVED(what, bytes)                                                     \
    do {                                                                       \
        rt->what += (uint64_t)(bytes);                                         \
    } while (0)
#else
#define MOVED(what, bytes)                                                     \
    do {                                                                       \
    } while (0)
#endif

#define BINARY_I(field, expression)                                            \
    do {                                                                       \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        (top++)->field = (expression);                                         \
    } while (0)

#if KEST_THREADED
    // Every instruction jumps to the next from its own end rather than from
    // the one place at the top of the loop, so the host's compiler holds the
    // frame and the constants in registers rather than a table address, and
    // each jump is predicted from where it is. GCC and clang only; the switch
    // below is the whole of it everywhere else. See D1181.
    static const void *const threaded[256] = {
        [KEST_OP_CONST] = &&thread_KEST_OP_CONST,
        [KEST_OP_CONST_RUN] = &&thread_KEST_OP_CONST_RUN,
        [KEST_OP_CONST_AT] = &&thread_KEST_OP_CONST_AT,
        [KEST_OP_LOAD] = &&thread_KEST_OP_LOAD,
        [KEST_OP_LOAD2] = &&thread_KEST_OP_LOAD2,
        [KEST_OP_LOADK] = &&thread_KEST_OP_LOADK,
        [KEST_OP_STORE] = &&thread_KEST_OP_STORE,
        [KEST_OP_LOADN] = &&thread_KEST_OP_LOADN,
        [KEST_OP_STOREN] = &&thread_KEST_OP_STOREN,
        [KEST_OP_FIELD] = &&thread_KEST_OP_FIELD,
        [KEST_OP_ARRAY] = &&thread_KEST_OP_ARRAY,
        [KEST_OP_MAKE_ARRAY] = &&thread_KEST_OP_MAKE_ARRAY,
        [KEST_OP_ROOM] = &&thread_KEST_OP_ROOM,
        [KEST_OP_PUSH] = &&thread_KEST_OP_PUSH,
        [KEST_OP_FIT] = &&thread_KEST_OP_FIT,
        [KEST_OP_PUSH_TEXT] = &&thread_KEST_OP_PUSH_TEXT,
        [KEST_OP_FIT_TEXT] = &&thread_KEST_OP_FIT_TEXT,
        [KEST_OP_INDEX] = &&thread_KEST_OP_INDEX,
        [KEST_OP_INDEX_LL] = &&thread_KEST_OP_INDEX_LL,
        [KEST_OP_INDEX_TO] = &&thread_KEST_OP_INDEX_TO,
        [KEST_OP_INDEX_TO_LL] = &&thread_KEST_OP_INDEX_TO_LL,
        [KEST_OP_ELEM_FROM_LL] = &&thread_KEST_OP_ELEM_FROM_LL,
        [KEST_OP_ELEM_FROM] = &&thread_KEST_OP_ELEM_FROM,
        [KEST_OP_POP_LAST] = &&thread_KEST_OP_POP_LAST,
        [KEST_OP_TAKE] = &&thread_KEST_OP_TAKE,
        [KEST_OP_CLEAR] = &&thread_KEST_OP_CLEAR,
        [KEST_OP_ELEM_ADDR] = &&thread_KEST_OP_ELEM_ADDR,
        [KEST_OP_ELEM_AT] = &&thread_KEST_OP_ELEM_AT,
        [KEST_OP_LOAD_SLOTS] = &&thread_KEST_OP_LOAD_SLOTS,
        [KEST_OP_STORE_SLOTS] = &&thread_KEST_OP_STORE_SLOTS,
        [KEST_OP_OFFSET_ADDR] = &&thread_KEST_OP_OFFSET_ADDR,
        [KEST_OP_LOAD_AT] = &&thread_KEST_OP_LOAD_AT,
        [KEST_OP_LOAD_ELEM] = &&thread_KEST_OP_LOAD_ELEM,
        [KEST_OP_STORE_ELEM] = &&thread_KEST_OP_STORE_ELEM,
        [KEST_OP_NEW_STORE] = &&thread_KEST_OP_NEW_STORE,
        [KEST_OP_ADD] = &&thread_KEST_OP_ADD,
        [KEST_OP_GET] = &&thread_KEST_OP_GET,
        [KEST_OP_SET] = &&thread_KEST_OP_SET,
        [KEST_OP_REMOVE] = &&thread_KEST_OP_REMOVE,
        [KEST_OP_SEEK_FROM] = &&thread_KEST_OP_SEEK_FROM,
        [KEST_OP_SEEK_NEXT] = &&thread_KEST_OP_SEEK_NEXT,
        [KEST_OP_STORE_REF] = &&thread_KEST_OP_STORE_REF,
        [KEST_OP_COUNT] = &&thread_KEST_OP_COUNT,
        [KEST_OP_TEXT_FLAGS] = &&thread_KEST_OP_TEXT_FLAGS,
        [KEST_OP_TEXT_VALUE] = &&thread_KEST_OP_TEXT_VALUE,
        [KEST_OP_TEXT_I] = &&thread_KEST_OP_TEXT_I,
        [KEST_OP_TEXT_U] = &&thread_KEST_OP_TEXT_U,
        [KEST_OP_TEXT_F] = &&thread_KEST_OP_TEXT_F,
        [KEST_OP_TEXT_F32] = &&thread_KEST_OP_TEXT_F32,
        [KEST_OP_TEXT_B] = &&thread_KEST_OP_TEXT_B,
        [KEST_OP_CONCAT] = &&thread_KEST_OP_CONCAT,
        [KEST_OP_TEXT_FROM] = &&thread_KEST_OP_TEXT_FROM,
        [KEST_OP_HASH_I] = &&thread_KEST_OP_HASH_I,
        [KEST_OP_HASH_F] = &&thread_KEST_OP_HASH_F,
        [KEST_OP_HASH_T] = &&thread_KEST_OP_HASH_T,
        [KEST_OP_HASH_VALUE] = &&thread_KEST_OP_HASH_VALUE,
        [KEST_OP_EQ_VALUE] = &&thread_KEST_OP_EQ_VALUE,
        [KEST_OP_NE_VALUE] = &&thread_KEST_OP_NE_VALUE,
        [KEST_OP_TEXT_LEN] = &&thread_KEST_OP_TEXT_LEN,
        [KEST_OP_TEXT_AT] = &&thread_KEST_OP_TEXT_AT,
        [KEST_OP_TEXT_IN] = &&thread_KEST_OP_TEXT_IN,
        [KEST_OP_TEXT_SLICE] = &&thread_KEST_OP_TEXT_SLICE,
        [KEST_OP_TEXT_REST] = &&thread_KEST_OP_TEXT_REST,
        [KEST_OP_TEXT_MATCHES] = &&thread_KEST_OP_TEXT_MATCHES,
        [KEST_OP_TEXT_FIND] = &&thread_KEST_OP_TEXT_FIND,
        [KEST_OP_LEN] = &&thread_KEST_OP_LEN,
        [KEST_OP_TRUE] = &&thread_KEST_OP_TRUE,
        [KEST_OP_FALSE] = &&thread_KEST_OP_FALSE,
        [KEST_OP_POP] = &&thread_KEST_OP_POP,
        [KEST_OP_POPN] = &&thread_KEST_OP_POPN,
        [KEST_OP_ROTATE] = &&thread_KEST_OP_ROTATE,
        [KEST_OP_ADD_I] = &&thread_KEST_OP_ADD_I,
        [KEST_OP_SUB_I] = &&thread_KEST_OP_SUB_I,
        [KEST_OP_MUL_I] = &&thread_KEST_OP_MUL_I,
        [KEST_OP_DIV_I] = &&thread_KEST_OP_DIV_I,
        [KEST_OP_MOD_I] = &&thread_KEST_OP_MOD_I,
        [KEST_OP_MOD_I_C] = &&thread_KEST_OP_MOD_I_C,
        [KEST_OP_DIV_I_C] = &&thread_KEST_OP_DIV_I_C,
        [KEST_OP_MOD_I_K] = &&thread_KEST_OP_MOD_I_K,
        [KEST_OP_DIV_I_K] = &&thread_KEST_OP_DIV_I_K,
        [KEST_OP_ADD_I_NARROW_C] = &&thread_KEST_OP_ADD_I_NARROW_C,
        [KEST_OP_SUB_I_NARROW_C] = &&thread_KEST_OP_SUB_I_NARROW_C,
        [KEST_OP_MUL_I_NARROW_C] = &&thread_KEST_OP_MUL_I_NARROW_C,
        [KEST_OP_ADD_I_NARROW_K] = &&thread_KEST_OP_ADD_I_NARROW_K,
        [KEST_OP_SUB_I_NARROW_K] = &&thread_KEST_OP_SUB_I_NARROW_K,
        [KEST_OP_MUL_I_NARROW_K] = &&thread_KEST_OP_MUL_I_NARROW_K,
        [KEST_OP_DIV_U] = &&thread_KEST_OP_DIV_U,
        [KEST_OP_MOD_U] = &&thread_KEST_OP_MOD_U,
        [KEST_OP_AND_I] = &&thread_KEST_OP_AND_I,
        [KEST_OP_OR_I] = &&thread_KEST_OP_OR_I,
        [KEST_OP_XOR_I] = &&thread_KEST_OP_XOR_I,
        [KEST_OP_NOT_I] = &&thread_KEST_OP_NOT_I,
        [KEST_OP_SHL] = &&thread_KEST_OP_SHL,
        [KEST_OP_SHR_I] = &&thread_KEST_OP_SHR_I,
        [KEST_OP_SHR_U] = &&thread_KEST_OP_SHR_U,
        [KEST_OP_NEG_I] = &&thread_KEST_OP_NEG_I,
        [KEST_OP_I2F] = &&thread_KEST_OP_I2F,
        [KEST_OP_U2F] = &&thread_KEST_OP_U2F,
        [KEST_OP_TO_F32] = &&thread_KEST_OP_TO_F32,
        [KEST_OP_FLOAT_BITS] = &&thread_KEST_OP_FLOAT_BITS,
        [KEST_OP_BITS_F32] = &&thread_KEST_OP_BITS_F32,
        [KEST_OP_F2I] = &&thread_KEST_OP_F2I,
        [KEST_OP_NARROW] = &&thread_KEST_OP_NARROW,
        [KEST_OP_ADD_I_NARROW] = &&thread_KEST_OP_ADD_I_NARROW,
        [KEST_OP_SUB_I_NARROW] = &&thread_KEST_OP_SUB_I_NARROW,
        [KEST_OP_MUL_I_NARROW] = &&thread_KEST_OP_MUL_I_NARROW,
        [KEST_OP_ADD_I_NARROW_TO] = &&thread_KEST_OP_ADD_I_NARROW_TO,
        [KEST_OP_STORE_K] = &&thread_KEST_OP_STORE_K,
        [KEST_OP_ADD_K_SELF] = &&thread_KEST_OP_ADD_K_SELF,
        [KEST_OP_SUB_K_SELF] = &&thread_KEST_OP_SUB_K_SELF,
        [KEST_OP_SUB_I_NARROW_TO] = &&thread_KEST_OP_SUB_I_NARROW_TO,
        [KEST_OP_ADD_F_TO] = &&thread_KEST_OP_ADD_F_TO,
        [KEST_OP_ADD_F_LL] = &&thread_KEST_OP_ADD_F_LL,
        [KEST_OP_SUB_F_LL] = &&thread_KEST_OP_SUB_F_LL,
        [KEST_OP_SUB_F_TO] = &&thread_KEST_OP_SUB_F_TO,
        [KEST_OP_ADD_F] = &&thread_KEST_OP_ADD_F,
        [KEST_OP_SUB_F] = &&thread_KEST_OP_SUB_F,
        [KEST_OP_MUL_F] = &&thread_KEST_OP_MUL_F,
        [KEST_OP_DIV_F] = &&thread_KEST_OP_DIV_F,
        [KEST_OP_MOD_F] = &&thread_KEST_OP_MOD_F,
        [KEST_OP_NEG_F] = &&thread_KEST_OP_NEG_F,
        [KEST_OP_ADD_F32] = &&thread_KEST_OP_ADD_F32,
        [KEST_OP_SUB_F32] = &&thread_KEST_OP_SUB_F32,
        [KEST_OP_MUL_F32] = &&thread_KEST_OP_MUL_F32,
        [KEST_OP_DIV_F32] = &&thread_KEST_OP_DIV_F32,
        [KEST_OP_MOD_F32] = &&thread_KEST_OP_MOD_F32,
        [KEST_OP_NEG_F32] = &&thread_KEST_OP_NEG_F32,
        [KEST_OP_LT_I] = &&thread_KEST_OP_LT_I,
        [KEST_OP_LE_I] = &&thread_KEST_OP_LE_I,
        [KEST_OP_GT_I] = &&thread_KEST_OP_GT_I,
        [KEST_OP_GE_I] = &&thread_KEST_OP_GE_I,
        [KEST_OP_LT_U] = &&thread_KEST_OP_LT_U,
        [KEST_OP_LE_U] = &&thread_KEST_OP_LE_U,
        [KEST_OP_GT_U] = &&thread_KEST_OP_GT_U,
        [KEST_OP_GE_U] = &&thread_KEST_OP_GE_U,
        [KEST_OP_LT_F] = &&thread_KEST_OP_LT_F,
        [KEST_OP_LE_F] = &&thread_KEST_OP_LE_F,
        [KEST_OP_GT_F] = &&thread_KEST_OP_GT_F,
        [KEST_OP_GE_F] = &&thread_KEST_OP_GE_F,
        [KEST_OP_EQ_I] = &&thread_KEST_OP_EQ_I,
        [KEST_OP_NE_I] = &&thread_KEST_OP_NE_I,
        [KEST_OP_EQ_F] = &&thread_KEST_OP_EQ_F,
        [KEST_OP_NE_F] = &&thread_KEST_OP_NE_F,
        [KEST_OP_EQ_T] = &&thread_KEST_OP_EQ_T,
        [KEST_OP_NE_T] = &&thread_KEST_OP_NE_T,
        [KEST_OP_LT_T] = &&thread_KEST_OP_LT_T,
        [KEST_OP_LE_T] = &&thread_KEST_OP_LE_T,
        [KEST_OP_GT_T] = &&thread_KEST_OP_GT_T,
        [KEST_OP_GE_T] = &&thread_KEST_OP_GE_T,
        [KEST_OP_NOT] = &&thread_KEST_OP_NOT,
        [KEST_OP_JUMP] = &&thread_KEST_OP_JUMP,
        [KEST_OP_JUMP_FALSE] = &&thread_KEST_OP_JUMP_FALSE,
        [KEST_OP_JUMP_TRUE] = &&thread_KEST_OP_JUMP_TRUE,
        [KEST_OP_JUMP_FALSE_LT_E] = &&thread_KEST_OP_JUMP_FALSE_LT_E,
        [KEST_OP_JUMP_FALSE_LE_E] = &&thread_KEST_OP_JUMP_FALSE_LE_E,
        [KEST_OP_JUMP_FALSE_GT_E] = &&thread_KEST_OP_JUMP_FALSE_GT_E,
        [KEST_OP_JUMP_FALSE_GE_E] = &&thread_KEST_OP_JUMP_FALSE_GE_E,
        [KEST_OP_JUMP_FALSE_EQ_E] = &&thread_KEST_OP_JUMP_FALSE_EQ_E,
        [KEST_OP_JUMP_FALSE_NE_E] = &&thread_KEST_OP_JUMP_FALSE_NE_E,
        [KEST_OP_JUMP_FALSE_LT_C] = &&thread_KEST_OP_JUMP_FALSE_LT_C,
        [KEST_OP_JUMP_FALSE_LE_C] = &&thread_KEST_OP_JUMP_FALSE_LE_C,
        [KEST_OP_JUMP_FALSE_GT_C] = &&thread_KEST_OP_JUMP_FALSE_GT_C,
        [KEST_OP_JUMP_FALSE_GE_C] = &&thread_KEST_OP_JUMP_FALSE_GE_C,
        [KEST_OP_JUMP_FALSE_EQ_C] = &&thread_KEST_OP_JUMP_FALSE_EQ_C,
        [KEST_OP_JUMP_FALSE_NE_C] = &&thread_KEST_OP_JUMP_FALSE_NE_C,
        [KEST_OP_JUMP_FALSE_LT_FK] = &&thread_KEST_OP_JUMP_FALSE_LT_FK,
        [KEST_OP_JUMP_FALSE_LE_FK] = &&thread_KEST_OP_JUMP_FALSE_LE_FK,
        [KEST_OP_JUMP_FALSE_GT_FK] = &&thread_KEST_OP_JUMP_FALSE_GT_FK,
        [KEST_OP_JUMP_FALSE_GE_FK] = &&thread_KEST_OP_JUMP_FALSE_GE_FK,
        [KEST_OP_JUMP_FALSE_EQ_FK] = &&thread_KEST_OP_JUMP_FALSE_EQ_FK,
        [KEST_OP_JUMP_FALSE_NE_FK] = &&thread_KEST_OP_JUMP_FALSE_NE_FK,
        [KEST_OP_JUMP_TRUE_LT_FK] = &&thread_KEST_OP_JUMP_TRUE_LT_FK,
        [KEST_OP_JUMP_TRUE_LE_FK] = &&thread_KEST_OP_JUMP_TRUE_LE_FK,
        [KEST_OP_JUMP_TRUE_GT_FK] = &&thread_KEST_OP_JUMP_TRUE_GT_FK,
        [KEST_OP_JUMP_TRUE_GE_FK] = &&thread_KEST_OP_JUMP_TRUE_GE_FK,
        [KEST_OP_JUMP_TRUE_EQ_FK] = &&thread_KEST_OP_JUMP_TRUE_EQ_FK,
        [KEST_OP_JUMP_TRUE_NE_FK] = &&thread_KEST_OP_JUMP_TRUE_NE_FK,
        [KEST_OP_JUMP_FALSE_LT_K] = &&thread_KEST_OP_JUMP_FALSE_LT_K,
        [KEST_OP_JUMP_FALSE_LE_K] = &&thread_KEST_OP_JUMP_FALSE_LE_K,
        [KEST_OP_JUMP_FALSE_GT_K] = &&thread_KEST_OP_JUMP_FALSE_GT_K,
        [KEST_OP_JUMP_FALSE_GE_K] = &&thread_KEST_OP_JUMP_FALSE_GE_K,
        [KEST_OP_JUMP_FALSE_EQ_K] = &&thread_KEST_OP_JUMP_FALSE_EQ_K,
        [KEST_OP_JUMP_FALSE_NE_K] = &&thread_KEST_OP_JUMP_FALSE_NE_K,
        [KEST_OP_JUMP_FALSE_LT_I] = &&thread_KEST_OP_JUMP_FALSE_LT_I,
        [KEST_OP_JUMP_FALSE_LE_I] = &&thread_KEST_OP_JUMP_FALSE_LE_I,
        [KEST_OP_JUMP_FALSE_GT_I] = &&thread_KEST_OP_JUMP_FALSE_GT_I,
        [KEST_OP_JUMP_FALSE_GE_I] = &&thread_KEST_OP_JUMP_FALSE_GE_I,
        [KEST_OP_JUMP_FALSE_EQ_I] = &&thread_KEST_OP_JUMP_FALSE_EQ_I,
        [KEST_OP_JUMP_FALSE_NE_I] = &&thread_KEST_OP_JUMP_FALSE_NE_I,
        [KEST_OP_JUMP_TRUE_LT_I] = &&thread_KEST_OP_JUMP_TRUE_LT_I,
        [KEST_OP_JUMP_TRUE_LE_I] = &&thread_KEST_OP_JUMP_TRUE_LE_I,
        [KEST_OP_JUMP_TRUE_GT_I] = &&thread_KEST_OP_JUMP_TRUE_GT_I,
        [KEST_OP_JUMP_TRUE_GE_I] = &&thread_KEST_OP_JUMP_TRUE_GE_I,
        [KEST_OP_JUMP_TRUE_EQ_I] = &&thread_KEST_OP_JUMP_TRUE_EQ_I,
        [KEST_OP_JUMP_TRUE_NE_I] = &&thread_KEST_OP_JUMP_TRUE_NE_I,
        [KEST_OP_JUMP_FALSE_LT_F] = &&thread_KEST_OP_JUMP_FALSE_LT_F,
        [KEST_OP_JUMP_FALSE_LE_F] = &&thread_KEST_OP_JUMP_FALSE_LE_F,
        [KEST_OP_JUMP_FALSE_GT_F] = &&thread_KEST_OP_JUMP_FALSE_GT_F,
        [KEST_OP_JUMP_FALSE_GE_F] = &&thread_KEST_OP_JUMP_FALSE_GE_F,
        [KEST_OP_JUMP_FALSE_EQ_F] = &&thread_KEST_OP_JUMP_FALSE_EQ_F,
        [KEST_OP_JUMP_FALSE_NE_F] = &&thread_KEST_OP_JUMP_FALSE_NE_F,
        [KEST_OP_JUMP_TRUE_LT_F] = &&thread_KEST_OP_JUMP_TRUE_LT_F,
        [KEST_OP_JUMP_TRUE_LE_F] = &&thread_KEST_OP_JUMP_TRUE_LE_F,
        [KEST_OP_JUMP_TRUE_GT_F] = &&thread_KEST_OP_JUMP_TRUE_GT_F,
        [KEST_OP_JUMP_TRUE_GE_F] = &&thread_KEST_OP_JUMP_TRUE_GE_F,
        [KEST_OP_JUMP_TRUE_EQ_F] = &&thread_KEST_OP_JUMP_TRUE_EQ_F,
        [KEST_OP_JUMP_TRUE_NE_F] = &&thread_KEST_OP_JUMP_TRUE_NE_F,
        [KEST_OP_LOOP] = &&thread_KEST_OP_LOOP,
        [KEST_OP_NEXT_LESS_I] = &&thread_KEST_OP_NEXT_LESS_I,
        [KEST_OP_NEXT_LESS_U] = &&thread_KEST_OP_NEXT_LESS_U,
        [KEST_OP_SCRATCH] = &&thread_KEST_OP_SCRATCH,
        [KEST_OP_UNSCRATCH] = &&thread_KEST_OP_UNSCRATCH,
        [KEST_OP_CALL] = &&thread_KEST_OP_CALL,
        [KEST_OP_CALL_VALUE] = &&thread_KEST_OP_CALL_VALUE,
        [KEST_OP_CALL_HOST] = &&thread_KEST_OP_CALL_HOST,
        [KEST_OP_STOP] = &&thread_KEST_OP_STOP,
        [KEST_OP_STOP + 1 ... 255] = &&thread_nothing,
        [KEST_OP_RETURN] = &&thread_KEST_OP_RETURN,
    };
#endif
    const uint8_t *instruction = ip;
    while (true) {
        instruction = ip;
#if KEST_CHECKED
        if (rt->ran_checked != NULL) {
            rt->ran_checked[*instruction]++;
            if (rt->pairs_checked != NULL) {
                rt->pairs_checked[(size_t)rt->last_checked *
                                      (KEST_OP_RETURN + 1) +
                                  *instruction]++;
                rt->last_checked = *instruction;
            }
        }
        // The compiler's count of the operand stack, held by the machine that
        // moves it. A body is given its named slots and this many above them,
        // and the machine is the only thing that knows how far it actually
        // went — so the two are put together here, in the build that checks
        // itself, once per instruction. D808 to D810 held that count while it
        // was being made; this holds it against what it was made for. See
        // D811.
        // How deep this body has actually been, which is the other half of
        // the question below: that one says a body went further than it was
        // given, and this says whether it ever went that far at all. Room
        // asked for and never used is memory a host is told to find for
        // nothing, and `needs_of` carries it up every chain of calls. The
        // chunk is the compiler's and the machine does not change it, so the
        // const is put aside for the one number written back. See D812.
        {
            uint32_t at = (uint32_t)(top - mine -
                                     frame->chunk->slot_count);
            KestChunk *seen = (KestChunk *)(uintptr_t)frame->chunk;
            if (at > seen->went) {
                seen->went = at;
            }
            // And the same of the whole machine rather than of one body: how
            // far up its stack it ever reached and how many frames were
            // standing at once. That pair is what a host is asked to find
            // room for. See D813.
            uint32_t all = (uint32_t)(top - rt->stack);
            if (all > rt->went_slots) {
                rt->went_slots = all;
            }
            if (rt->frame_count > rt->went_frames) {
                rt->went_frames = rt->frame_count;
            }
        }
        // And the verifier's own table held to what the machine moved, an
        // instruction at a time: what the one before left is what this one
        // finds. The verifier walks every path with that table before
        // anything runs, so a row of it that is wrong is a program proved
        // with the wrong numbers, and every program run in this build is a
        // test of every row it reaches. See D1239.
        {
            uint32_t now = (uint32_t)(top - mine - frame->chunk->slot_count);
            if (frame->expect != 0 && now != frame->expect - 1) {
                fail(vmp, frame, instruction, "K0655",
                     "the verifier says this body is %u deep here and it is "
                     "%u",
                     frame->expect - 1, now);
                kest_diags_fault(vmp->diags,
                                 "what the verifier says an instruction does "
                                 "to the operand stack and what the machine "
                                 "did disagree");
                return false;
            }
            uint32_t takes = 0;
            uint32_t gives = 0;
            frame->expect = 0;
            if (*instruction != KEST_OP_RETURN && *instruction != KEST_OP_STOP &&
                kest_op_stack(module, frame->chunk,
                              (uint32_t)(instruction - frame->chunk->code),
                              &takes, &gives) == NULL &&
                takes <= now) {
                frame->expect = now - takes + gives + 1;
            }
        }
        if (top > mine + frame->chunk->slot_count +
                      frame->chunk->stack_needed) {
            fail(vmp, frame, instruction, "K0655",
                 "this body was given room to work out %u slot(s) and is "
                 "%u deep",
                 frame->chunk->stack_needed,
                 (uint32_t)(top - mine - frame->chunk->slot_count));
            kest_diags_fault(vmp->diags,
                             "the compiler's count of the operand stack and "
                             "what the machine moved disagree");
            return false;
        }
#endif
        switch (READ_BYTE()) {
        case KEST_OP_CONST: THREADED(KEST_OP_CONST) {
            uint16_t which = READ_U16();
#if KEST_CHECKED
            if (!own_constants(vmp, frame, instruction, which + 1u)) {
                return false;
            }
#endif
            MOVED(moved_held, sizeof(KestValue));
            *top++ = constants[which];
            NEXT;
        }
        case KEST_OP_CONST_RUN: THREADED(KEST_OP_CONST_RUN) {
            uint16_t first = READ_U16();
            uint16_t count = READ_U16();
#if KEST_CHECKED
            if (!own_constants(vmp, frame, instruction,
                               (uint32_t)first + count)) {
                return false;
            }
#endif
            MOVED(moved_held, (uint64_t)count * sizeof(KestValue));
            memcpy(top, &constants[first], sizeof(KestValue) * count);
            top += count;
            NEXT;
        }
        case KEST_OP_CONST_AT: THREADED(KEST_OP_CONST_AT) {
            uint16_t first = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            IN_RUN(index, count);
#if KEST_CHECKED
            if (!own_constants(vmp, frame, instruction,
                               (uint32_t)first + (uint32_t)count * stride)) {
                return false;
            }
#endif
            MOVED(moved_held, (uint64_t)stride * sizeof(KestValue));
            memcpy(top,
                   &constants[first + (size_t)index * stride],
                   sizeof(KestValue) * stride);
            top += stride;
            NEXT;
        }
        case KEST_OP_LOAD: THREADED(KEST_OP_LOAD) {
            uint16_t slot = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, sizeof(KestValue));
            *top++ = mine[slot];
            NEXT;
        }
        // The two pairs this machine runs most of, each as one instruction.
        // They do what the two they replace did, in the order they did it:
        // what is saved is a dispatch and a read of the next opcode, which
        // over a frame step is a quarter of everything. See D961.
        case KEST_OP_LOAD2: THREADED(KEST_OP_LOAD2) {
            uint16_t first = READ_U16();
            uint16_t second = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, first, first + 1u) ||
                !own_slots(vmp, frame, instruction, second, second + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, 2 * sizeof(KestValue));
            *top++ = mine[first];
            *top++ = mine[second];
            NEXT;
        }
        case KEST_OP_LOADK: THREADED(KEST_OP_LOADK) {
            uint16_t slot = READ_U16();
            uint16_t which = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u) ||
                !own_constants(vmp, frame, instruction, which + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, sizeof(KestValue));
            MOVED(moved_held, sizeof(KestValue));
            *top++ = mine[slot];
            *top++ = constants[which];
            NEXT;
        }
        case KEST_OP_STORE: THREADED(KEST_OP_STORE) {
            uint16_t slot = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot] = *--top;
            NEXT;
        }
        case KEST_OP_LOADN: THREADED(KEST_OP_LOADN) {
            uint16_t slot = READ_U16();
            uint16_t count = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           (uint32_t)slot + count)) {
                return false;
            }
#endif
            // Written out rather than handed to `memcpy`. Most runs are two or
            // three slots — a struct of a few fields, or the fields of one
            // loaded one after another — and a call that decides how to copy
            // anything costs more than moving three of them. See D871.
            MOVED(moved_loaded, (uint64_t)count * sizeof(KestValue));
            for (uint16_t i = 0; i < count; i++) {
                top[i] = mine[slot + i];
            }
            top += count;
            NEXT;
        }
        case KEST_OP_STOREN: THREADED(KEST_OP_STOREN) {
            uint16_t slot = READ_U16();
            uint16_t count = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           (uint32_t)slot + count)) {
                return false;
            }
#endif
            top -= count;
            for (uint16_t i = 0; i < count; i++) {
                mine[slot + i] = top[i];
            }
            MOVED(moved_stored, (uint64_t)count * sizeof(KestValue));
            NEXT;
        }
        case KEST_OP_FIELD: THREADED(KEST_OP_FIELD) {
            uint16_t offset = READ_U16();
            uint16_t size = READ_U16();
            uint16_t total = READ_U16();
            KestValue *value = top - total;
            MOVED(moved_shuffled, (uint64_t)size * sizeof(KestValue));
            memmove(value, value + offset, sizeof(KestValue) * size);
            top = value + size;
            NEXT;
        }
        case KEST_OP_ARRAY: THREADED(KEST_OP_ARRAY) {
            uint16_t count = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            SPEND_WORK((uint64_t)count);
            // The elements first, because the header names them and a walk
            // set off by taking the header would find elements nothing names.
            // The other way round the elements are simply not reachable yet
            // and the header is not there to say they should be.
            uint32_t hands = rt->hands;
            unsigned char *bytes =
                in_hand(rt, elements_for(rt, top, layout, count));
            Array *array = bytes == NULL
                               ? NULL
                               : take(rt, top, sizeof(Array),
                                      KEST_GROUND_ARRAY);
            hands_off(rt, hands);
            if (array == NULL || bytes == NULL) {
                no_room(vmp, frame, instruction, rt);
                // What it was making, because nothing here was growing: a
                // number in the program rather than a ceiling that was nearly
                // enough.
                kest_diags_suggest(vmp->diags,
                                   "it was making an array of %u of %u bytes "
                                   "each",
                                   count, layout->size);
                return false;
            }
            array->what = KEST_IS_ARRAY;
            array->length = count;
            array->capacity = count;
            array->stride = layout->size;
            array->of = layout->type;
            array->bytes = bytes;

            top -= (size_t)count * layout->slots;
            for (uint16_t i = 0; i < count; i++) {
                MOVED(moved_packed, layout->size);
                pack(bytes + (size_t)i * layout->size, layout,
                     top + (size_t)i * layout->slots);
            }
            (top++)->object = array;
            NEXT;
        }
        case KEST_OP_MAKE_ARRAY: THREADED(KEST_OP_MAKE_ARRAY) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            top -= layout->slots;
            KestValue *fill = top;
            int64_t count = (--top)->integer;
            SPEND_WORK(count < 0 ? 0 : (uint64_t)count);
            // What fills it is above the top, because the count was taken off
            // after it: a walk has to read to the top of what is live and not
            // to the top of the stack. The making itself is the door a body
            // the host's compiler compiled goes through, so the two engines
            // make an array the same way and say the same thing when they
            // cannot. See D1099.
            frame->ip = ip;
            rt->asked_at = instruction;
            KestValue *was_top = rt->running_top;
            rt->running_top = fill + layout->slots;
            bool made = kest_array_new(rt, of_which, count, fill,
                                       KEST_WHERE_RUNNING,
                                       top);
            rt->running_top = was_top;
            if (!made) {
                return false;
            }
            top++;
            NEXT;
        }
        case KEST_OP_ROOM: THREADED(KEST_OP_ROOM) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t wanted = (--top)->integer;
            void *given = (--top)->object;
            // What was taken off the stack is still what this is about, so a
            // walk set off by making room has to read to above it rather than
            // to where the stack now ends.
            KestValue *reach = top + 2;
            SPEND_WORK(wanted < 0 ? 0 : (uint64_t)wanted);
            // The two things in this language that grow, through the one
            // instruction: a store is made with room by `store(n)` and an
            // array by `array(n, v)`, and this is the same sentence said to
            // one that is already there. A store keeps four runs rather than
            // one, so what it makes room in is four blocks at once, which is
            // `room_for`'s to know. See D913.
            if (KEST_HANDLE_IS(given, KEST_IS_STORE)) {
                Store *store = given;
                if (wanted > MAX_COUNTED) {
                    fail(vmp, frame, instruction, "K0630",
                         "this store holds %d, which is all `len` can count",
                         MAX_COUNTED);
                    return false;
                }
                if (wanted > (int64_t)store->capacity &&
                    !room_for(rt, reach, store, (uint32_t)wanted)) {
                    no_room_growing(vmp, frame, instruction, rt, "a store",
                                    store->used,
                                    sizeof(KestValue) * store->stride,
                                    (uint32_t)wanted);
                    return false;
                }
                break;
            }
            Array *array = given;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");
                return false;
            }
            if (wanted > MAX_COUNTED) {
                fail(vmp, frame, instruction, "K0630",
                     "this array holds %d, which is all `len` can count",
                     MAX_COUNTED);
                return false;
            }
            // Room for what is coming and nothing about what is there: the
            // length does not move, so a program that asks for less than it
            // already holds is asking for nothing. One block, once, where a
            // loop of pushes pays for a run of them.
            if (wanted > (int64_t)array->capacity) {
                uint32_t capacity = (uint32_t)wanted;
                unsigned char *grown =
                    elements_grown(rt, reach, array, layout, capacity);
                if (grown == NULL) {
                    no_room(vmp, frame, instruction, rt);
                    return false;
                }
                array->bytes = grown;
                array->capacity = capacity;
            }
            NEXT;
        }
        case KEST_OP_PUSH: THREADED(KEST_OP_PUSH) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            top -= layout->slots;
            KestValue *value = top;
            KestValue handle = *--top;
            // The growing itself is the door a body the host's compiler
            // compiled goes through, so an array grows one way and says one
            // thing when it cannot. See D1099.
            frame->ip = ip;
            rt->asked_at = instruction;
            KestValue *was_pushing = rt->running_top;
            rt->running_top = value + layout->slots;
            bool grew = kest_array_push(rt, handle, of_which, value,
                                        KEST_WHERE_RUNNING);
            rt->running_top = was_pushing;
            if (!grew) {
                return false;
            }
            NEXT;
        }
        // The same append with the growth taken out. Where `push` would double
        // the block this answers false and writes nothing, so a body that was
        // given room can fill it under a promise to reach no heap. See D940.
        case KEST_OP_FIT: THREADED(KEST_OP_FIT) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            top -= layout->slots;
            KestValue *value = top;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");
                return false;
            }
            if (array->length >= array->capacity ||
                array->length == MAX_COUNTED) {
                (top++)->integer = 0;
                break;
            }
            MOVED(moved_packed, layout->size);
            pack(array->bytes + (size_t)array->length * layout->size, layout,
                 value);
            array->length++;
            MOVED(moved_held, sizeof(KestValue));
            (top++)->integer = 1;
            NEXT;
        }
        // A whole piece of text onto a run of bytes. Text is its bytes
        // (D021), so this is the loop a program had to write taken into one
        // move: the same growth `push` does, once for the whole piece rather
        // than once a byte, and a `memcpy`. Building text a byte at a time was
        // eighty per cent of the instructions `bench/words.kest` ran. See
        // D1068.
        case KEST_OP_PUSH_TEXT: THREADED(KEST_OP_PUSH_TEXT) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            Said piece = TEXT_OFF();
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");
                return false;
            }
            if ((uint64_t)array->length + piece.length > (uint64_t)MAX_COUNTED) {
                fail(vmp, frame, instruction, "K0630",
                     "this array holds %d, which is all `len` can count",
                     MAX_COUNTED);
                return false;
            }
            uint32_t wanted = array->length + piece.length;
            if (wanted > array->capacity) {
                uint32_t capacity = array->capacity == 0 ? 8 : array->capacity;
                while (capacity < wanted) {
                    capacity *= 2;
                }
                // What is still live while this may walk: the array and the
                // piece, both of which are above where the stack now ends.
                unsigned char *was = array->bytes;
                unsigned char *grown =
                    elements_grown(rt, top + 3, array, layout, capacity);
                if (grown == NULL) {
                    no_room_growing(vmp, frame, instruction, rt, "an array",
                                    array->length, layout->size, capacity);
                    return false;
                }
                capacity = all_it_holds(rt, grown, layout, capacity);
                (((Elems *)(void *)grown) - 1)->places = capacity;
                if (grown != was) {
                    SPEND_WORK((uint64_t)array->length);
                }
                array->bytes = grown;
                array->capacity = capacity;
            }
            SPEND_WORK(piece.length);
            MOVED(moved_packed, piece.length);
            if (piece.length > 0) {
                memcpy(array->bytes + array->length, piece.bytes,
                       piece.length);
            }
            array->length = wanted;
            NEXT;
        }
        // The same with the growth taken out, which is what a body under a
        // promise can do: all of it fits or none of it goes in, because a
        // piece half written is a piece nobody can take back. See D940.
        case KEST_OP_FIT_TEXT: THREADED(KEST_OP_FIT_TEXT) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            (void)layout;
            Said piece = TEXT_OFF();
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");
                return false;
            }
            if ((uint64_t)array->length + piece.length > (uint64_t)array->capacity ||
                (uint64_t)array->length + piece.length > (uint64_t)MAX_COUNTED) {
                (top++)->integer = 0;
                break;
            }
            SPEND_WORK(piece.length);
            MOVED(moved_packed, piece.length);
            if (piece.length > 0) {
                memcpy(array->bytes + array->length, piece.bytes,
                       piece.length);
            }
            array->length += piece.length;
            MOVED(moved_held, sizeof(KestValue));
            (top++)->integer = 1;
            NEXT;
        }
        case KEST_OP_INDEX: THREADED(KEST_OP_INDEX) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = (--top)->integer;
            const Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            READ_INTO(top, layout,
                      array->bytes + (size_t)index * array->stride);
            top += layout->slots;
            NEXT;
        }
        // `load2` and `index` as one. The run and the index are read out of
        // the slots they are in rather than pushed to be popped. See D1155.
        case KEST_OP_INDEX_LL: THREADED(KEST_OP_INDEX_LL) {
            uint16_t holds = READ_U16();
            uint16_t at = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, holds, holds + 1u) ||
                !own_slots(vmp, frame, instruction, at, at + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, 2 * sizeof(KestValue));
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = mine[at].integer;
            const Array *array = mine[holds].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            READ_INTO(top, layout,
                      array->bytes + (size_t)index * array->stride);
            top += layout->slots;
            NEXT;
        }
        // The two above, each with the move at the other end taken into it.
        // `index` unpacks a struct onto the stack and the store that follows
        // copies it off again; this writes it where it is going. See D1012.
        case KEST_OP_INDEX_TO: THREADED(KEST_OP_INDEX_TO) {
            uint16_t of_which = READ_U16();
            uint16_t slot = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = (--top)->integer;
            const Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           slot + layout->slots)) {
                return false;
            }
#endif
            READ_INTO(mine + slot, layout,
                      array->bytes + (size_t)index * array->stride);
            NEXT;
        }
        case KEST_OP_INDEX_TO_LL: THREADED(KEST_OP_INDEX_TO_LL) {
            uint16_t of_which = READ_U16();
            uint16_t slot = READ_U16();
            uint16_t holds = READ_U16();
            uint16_t at = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           slot + layout->slots) ||
                !own_slots(vmp, frame, instruction, holds, holds + 1u) ||
                !own_slots(vmp, frame, instruction, at, at + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, 2 * sizeof(KestValue));
            int64_t index = mine[at].integer;
            const Array *array = mine[holds].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            READ_INTO(mine + slot, layout,
                      array->bytes + (size_t)index * array->stride);
            NEXT;
        }
        case KEST_OP_ELEM_FROM_LL: THREADED(KEST_OP_ELEM_FROM_LL) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            uint16_t slot = READ_U16();
            uint16_t holds = READ_U16();
            uint16_t at = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           slot + layout->slots) ||
                !own_slots(vmp, frame, instruction, holds, holds + 1u) ||
                !own_slots(vmp, frame, instruction, at, at + 1u)) {
                return false;
            }
#endif
            MOVED(moved_loaded, 2 * sizeof(KestValue));
            MOVED(moved_packed, layout->size);
            int64_t index = mine[at].integer;
            Array *array = mine[holds].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            pack(array->bytes + (size_t)index * array->stride + offset, layout,
                 mine + slot);
            NEXT;
        }
        case KEST_OP_ELEM_FROM: THREADED(KEST_OP_ELEM_FROM) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            uint16_t slot = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot,
                           slot + layout->slots)) {
                return false;
            }
#endif
            MOVED(moved_packed, layout->size);
            pack(array->bytes + (size_t)index * array->stride + offset, layout,
                 mine + slot);
            NEXT;
        }
        case KEST_OP_POP_LAST: THREADED(KEST_OP_POP_LAST) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            if (array->length == 0) {
                for (uint16_t i = 0; i < layout->slots; i++) {
                    top[i].integer = 0;
                }
                top += layout->slots;
                (top++)->integer = 0;
                break;
            }
            array->length--;
            READ_INTO(top, layout,
                      array->bytes + (size_t)array->length * array->stride);
            top += layout->slots;
            (top++)->integer = 1;
            NEXT;
        }
        case KEST_OP_TAKE: THREADED(KEST_OP_TAKE) {
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            KestValue handle = *top;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            IN_ARRAY(index, array);
            unsigned char *at = array->bytes + (size_t)index * array->stride;
            READ_INTO(top, layout, at);
            top += layout->slots;
            // And the taking away, which is the door a body the host's
            // compiler compiled goes through: one answer about what a run of
            // elements does when one is taken out of the middle. See D1099.
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_array_remove(rt, handle, index,
                                   KEST_WHERE_RUNNING)) {
                return false;
            }
            NEXT;
        }
        case KEST_OP_CLEAR: THREADED(KEST_OP_CLEAR) {
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            array->length = 0;
            NEXT;
        }
        case KEST_OP_ELEM_ADDR: THREADED(KEST_OP_ELEM_ADDR) {
            READ_U16();
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            (top++)->object = array->bytes + (size_t)index * array->stride;
            NEXT;
        }
        // One of an array at a byte into it, with the array and the index
        // taken away: what `elem.addr` and the `load.at` after it did in two.
        // Reading a field of an element is what a frame does most, and it was
        // the one read of a place that cost two dispatches. See D1044.
        case KEST_OP_ELEM_AT: THREADED(KEST_OP_ELEM_AT) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            READ_INTO(top, layout,
                      array->bytes + (size_t)index * array->stride + offset);
            top += layout->slots;
            NEXT;
        }
        case KEST_OP_LOAD_SLOTS: THREADED(KEST_OP_LOAD_SLOTS) {
            uint16_t base = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            IN_RUN(index, count);
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, base,
                           (uint32_t)base + (uint32_t)count * stride)) {
                return false;
            }
#endif
            MOVED(moved_loaded, (uint64_t)stride * sizeof(KestValue));
            memcpy(top, mine + base + (size_t)index * stride,
                   sizeof(KestValue) * stride);
            top += stride;
            NEXT;
        }
        case KEST_OP_STORE_SLOTS: THREADED(KEST_OP_STORE_SLOTS) {
            uint16_t base = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            top -= stride;
            KestValue *value = top;
            int64_t index = (--top)->integer;
            IN_RUN(index, count);
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, base,
                           (uint32_t)base + (uint32_t)count * stride)) {
                return false;
            }
#endif
            MOVED(moved_stored, (uint64_t)stride * sizeof(KestValue));
            memcpy(mine + base + (size_t)index * stride, value,
                   sizeof(KestValue) * stride);
            NEXT;
        }
        case KEST_OP_OFFSET_ADDR: THREADED(KEST_OP_OFFSET_ADDR) {
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            unsigned char *at = (--top)->object;
            IN_RUN(index, count);
            (top++)->object = at + (size_t)index * stride;
            NEXT;
        }
        case KEST_OP_LOAD_AT: THREADED(KEST_OP_LOAD_AT) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            const unsigned char *at = (--top)->object;
            READ_INTO(top, layout, at + offset);
            top += layout->slots;
            NEXT;
        }
        // A place in an array, given as the array and the index rather than as
        // an address. `load.elem` leaves the two where they are, because a
        // compound assignment reads before it writes and the write needs them
        // again; `store.elem` takes them. Between the two the program may do
        // anything at all, including growing that very array -- which used to
        // move the block while an address into it sat on the stack, and the
        // write then went into memory nothing would read again. See D931.
        case KEST_OP_LOAD_ELEM: THREADED(KEST_OP_LOAD_ELEM) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            int64_t index = top[-1].integer;
            Array *array = top[-2].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            READ_INTO(top, layout,
                      array->bytes + (size_t)index * array->stride + offset);
            top += layout->slots;
            NEXT;
        }
        case KEST_OP_STORE_ELEM: THREADED(KEST_OP_STORE_ELEM) {
            uint16_t offset = READ_U16();
            uint16_t of_which = READ_U16();
            OF_THE_MODULE(of_which, module->layout_count, "a layout");
            const KestLayout *layout = &module->layouts[of_which];
            top -= layout->slots;
            KestValue *value = top;
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            IN_ARRAY(index, array);
            MOVED(moved_packed, layout->size);
            pack(array->bytes + (size_t)index * array->stride + offset, layout,
                 value);
            NEXT;
        }
        case KEST_OP_NEW_STORE: THREADED(KEST_OP_NEW_STORE) {
            int64_t room = (--top)->integer;
            SPEND_WORK(room < 0 ? 0 : (uint64_t)room);
            // The stride is the layout's, which the door reads: what the
            // instruction carries is which layout, the same as everywhere
            // else a shape is named.
            READ_U16();
            uint16_t holds = READ_U16();
            OF_THE_MODULE(holds, module->layout_count, "a layout");
            frame->ip = ip;
            rt->asked_at = instruction;
            // The door is a host's as well, and a host's call leaves where a
            // walk reads to at the top of what it handed in. Here that is the
            // body that called in from outside, and every frame this machine
            // has built above it since -- this one included, which holds the
            // store made the instruction before -- is above that. So the
            // walk is told where the stack is now, the way the array doors
            // are. Found by walking at every allocation. See D1163.
            KestValue *was_top = rt->running_top;
            rt->running_top = top;
            bool made = kest_store_new(rt, holds, room, KEST_WHERE_RUNNING,
                                       top);
            rt->running_top = was_top;
            if (!made) {
                return false;
            }
            top++;
            NEXT;
        }
        case KEST_OP_ADD: THREADED(KEST_OP_ADD) {
            uint16_t stride = READ_U16();
            top -= stride;
            KestValue *value = top;
            Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");
            // What is being added is above where the stack now ends, and
            // until the copy below it is the only thing naming whatever it
            // holds. A store that grows takes four runs, any of which may set
            // off a walk, and a walk that read to `top` would find a freshly
            // made array named by nothing and give it away -- leaving the
            // copy to put a handle to a place something else now has into the
            // store. `set` at a position and `push` both read to above the
            // value for this reason; this one did not. See D1005.
            KestValue *reach = value + stride;

            uint32_t index;
            if (store->free_count > 0) {
                index = store->free_slots[--store->free_count];
            } else {
                if (store->used == MAX_COUNTED) {
                    fail(vmp, frame, instruction, "K0630",
                         "this store holds %d, which is all `len` can count",
                         MAX_COUNTED);
                    return false;
                }
                // What a store that has to grow copies, charged before it
                // does: four runs of what it holds. See D950.
                if (store->used == store->capacity) {
                    SPEND_WORK((uint64_t)store->used);
                }
                if (store->used == store->capacity &&
                    !grow_store(rt, reach, store)) {
                    // A store grows by four runs at once — what it holds, what
                    // each has counted, which are live and which are free —
                    // so what it was reaching for is wider than one of them.
                    no_room_growing(vmp, frame, instruction, rt, "a store",
                                    store->used,
                                    sizeof(KestValue) * store->stride,
                                    store->capacity == 0 ? 8
                                                         : store->capacity * 2);
                    return false;
                }
                index = store->used++;
                if (index >= store->high) {
                    store->high = index + 1;
                }
            }
            // The next handout number there is, taken from the count for the
            // whole process so that no two places anywhere are ever stamped
            // the same. A place handed out again gets a new one, so a
            // reference made before it was given back names a number nothing
            // carries any more; a store that has never written this number is
            // a store the reference did not come from; and a machine made
            // after another one cannot be handed the first one's numbers,
            // which is what a count of worlds could not promise. See D1033.
            uint64_t handout = next_handout(rt);
            if (handout > MOST_STAMPS) {
                fail(vmp, frame, instruction, "K0630",
                     "this process has handed out %llu places in stores, "
                     "which is all it can tell apart",
                     (unsigned long long)MOST_STAMPS);
                kest_diags_suggest(vmp->diags,
                                   "a place is stamped once for every `add`, "
                                   "and a number is never handed out twice");
                return false;
            }
            rt->stamps++;
            store->serials[index] = handout;
            mark_live(store, index, true);
            store->count++;
            MOVED(moved_payload, (uint64_t)stride * sizeof(KestValue));
            memcpy(store->elements + (size_t)index * stride, value,
                   sizeof(KestValue) * stride);
            (top++)->integer =
                pack_ref(store->serials[index], index);
            NEXT;
        }
        case KEST_OP_GET: THREADED(KEST_OP_GET) {
            uint16_t stride = READ_U16();
            int64_t which = (--top)->integer;
            KestValue held = *--top;
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_store_get(rt, held, which, stride, top,
                                KEST_WHERE_RUNNING)) {
                return false;
            }
            top += stride + 1;
            NEXT;
        }
        case KEST_OP_SET: THREADED(KEST_OP_SET) {
            uint16_t stride = READ_U16();
            top -= stride;
            KestValue *value = top;
            int64_t which = (--top)->integer;
            KestValue held = *--top;
            bool was = false;
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_store_set(rt, held, which, stride, value,
                                KEST_WHERE_RUNNING,
                                &was)) {
                return false;
            }
            (top++)->integer = was;
            NEXT;
        }
        case KEST_OP_REMOVE: THREADED(KEST_OP_REMOVE) {
            int64_t which = (--top)->integer;
            KestValue held = *--top;
            bool was = false;
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_store_remove(rt, held, which,
                                   KEST_WHERE_RUNNING,
                                   &was)) {
                return false;
            }
            (top++)->integer = was;
            NEXT;
        }
        case KEST_OP_SEEK_FROM: THREADED(KEST_OP_SEEK_FROM)
        case KEST_OP_SEEK_NEXT: THREADED(KEST_OP_SEEK_NEXT) {
            bool first = *instruction == KEST_OP_SEEK_FROM;
            uint16_t which = READ_U16();
            uint16_t at = READ_U16();
            uint16_t away = READ_U16();
            frame->ip = ip;
            rt->asked_at = instruction;
            int64_t found = 0;
            if (!kest_store_seek(rt, mine[which],
                                 mine[at].integer + (first ? 0 : 1),
                                 KEST_WHERE_RUNNING,
                                 &found)) {
                return false;
            }
            mine[at].integer = found;
            // The first one leaves when there is none and the ones after go
            // back while there is one, which is the same shape every other
            // walk has: a test above the loop and a test at the bottom.
            if (first) {
                if (found < 0) {
                    ip += away;
                }
            } else if (found >= 0) {
                SPEND();
                ip -= away;
            }
            NEXT;
        }
        case KEST_OP_STORE_REF: THREADED(KEST_OP_STORE_REF) {
            int64_t index = (--top)->integer;
            KestValue held = *--top;
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_store_ref(rt, held, index,
                                KEST_WHERE_RUNNING,
                                &top->integer)) {
                return false;
            }
            top++;
            NEXT;
        }
        case KEST_OP_COUNT: THREADED(KEST_OP_COUNT) {
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_store_count(rt, top[-1],
                                  KEST_WHERE_RUNNING,
                                  &top[-1].integer)) {
                return false;
            }
            NEXT;
        }
        case KEST_OP_TEXT_FLAGS: THREADED(KEST_OP_TEXT_FLAGS)
        case KEST_OP_TEXT_VALUE: THREADED(KEST_OP_TEXT_VALUE) {
            // Written the way it is built. Every other value's text is the
            // source that makes it and these are no different; D035 says so
            // for a set of bits and D036 for the cases of an enum.
            const KestType *type = module->layout_types[READ_U16()];
            uint16_t held = type->slots;
            top -= held;
            size_t length = format_value(NULL, 0, type, top);
            char *text = take(rt, top + held, length + 1, KEST_GROUND_PLAIN);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was writing a value as %zu bytes of "
                                   "text",
                                   length);
                return false;
            }
            format_value(text, length, type, top);
            text[length] = '\0';
            TEXT_ON(text, length);
            NEXT;
        }
        case KEST_OP_TEXT_I: THREADED(KEST_OP_TEXT_I)
        case KEST_OP_TEXT_U: THREADED(KEST_OP_TEXT_U)
        case KEST_OP_TEXT_F: THREADED(KEST_OP_TEXT_F)
        case KEST_OP_TEXT_F32: THREADED(KEST_OP_TEXT_F32)
        case KEST_OP_TEXT_B: THREADED(KEST_OP_TEXT_B) {
            char buffer[64];
            int written;
            if (instruction[0] == KEST_OP_TEXT_I ||
                instruction[0] == KEST_OP_TEXT_U) {
                written = kest_write_whole(buffer, (uint64_t)top[-1].integer,
                                           instruction[0] == KEST_OP_TEXT_I);
            } else if (instruction[0] == KEST_OP_TEXT_F ||
                       instruction[0] == KEST_OP_TEXT_F32) {
                written = kest_write_real(buffer, sizeof(buffer), top[-1].real,
                                          instruction[0] == KEST_OP_TEXT_F32);
            } else {
                written = snprintf(buffer, sizeof(buffer), "%s",
                                   top[-1].integer ? "true" : "false");
            }
            char *text =
                take(rt, top, (size_t)written + 1, KEST_GROUND_PLAIN);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was writing a number as %d bytes of "
                                   "text",
                                   written);
                return false;
            }
            MOVED(moved_text, (uint64_t)written + 1);
            memcpy(text, buffer, (size_t)written + 1);
            top--;
            TEXT_ON(text, written);
            NEXT;
        }
        case KEST_OP_CONCAT: THREADED(KEST_OP_CONCAT) {
            uint16_t count = READ_U16();
            top -= (uint32_t)count * 2;
            size_t length = 0;
            for (uint16_t i = 0; i < count; i++) {
                length += said(top + i * 2).length;
            }
            // The same ceiling an array has, and text is where a program
            // reaches it without meaning to: two of these joined is a new one
            // as long as both, so a program doubling one arrives here in
            // thirty steps. `len` counts bytes and gives back an `i32`.
            if (length > (size_t)MAX_COUNTED) {
                fail(vmp, frame, instruction, "K0630",
                     "this text would hold %zu, which is more than `len` can "
                     "count",
                     length);
                return false;
            }
            SPEND_WORK(length);
            char *text = take(rt, top + (uint32_t)count * 2, length + 1,
                              KEST_GROUND_PLAIN);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was joining text into %zu bytes",
                                   length);
                return false;
            }
            size_t used = 0;
            for (uint16_t i = 0; i < count; i++) {
                Said piece = said(top + i * 2);
                MOVED(moved_text, piece.length);
                memcpy(text + used, piece.bytes, piece.length);
                used += piece.length;
            }
            text[used] = '\0';
            TEXT_ON(text, used);
            NEXT;
        }
        case KEST_OP_TEXT_FROM: THREADED(KEST_OP_TEXT_FROM) {
            const Array *bytes = (--top)->object;
            HOLD(bytes, KEST_IS_ARRAY, "an array");
            SPEND_WORK(bytes->length);
            char *text =
                take(rt, top + 1, bytes->length + 1, KEST_GROUND_PLAIN);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was making %u bytes of text out of an "
                                   "array",
                                   bytes->length);
                return false;
            }
            // Text is UTF-8 and a run of bytes is whatever it holds, so this
            // is the door where the two meet and the one place the walk is
            // paid for. A nought is fine: it is a character. See D971.
            uint32_t bad = 0;
            if (!kest_utf8_whole((const char *)bytes->bytes, bytes->length,
                                 &bad)) {
                fail(vmp, frame, instruction, "K0604",
                     "byte %u begins no character, and text is UTF-8", bad);
                return false;
            }
            MOVED(moved_text, bytes->length);
            memcpy(text, bytes->bytes, bytes->length);
            text[bytes->length] = '\0';
            TEXT_ON(text, bytes->length);
            NEXT;
        }
        // One round of a mixer over the bits, which is what a table wants of
        // a number that is often small and often consecutive.
        case KEST_OP_HASH_I: THREADED(KEST_OP_HASH_I)
        case KEST_OP_HASH_F: THREADED(KEST_OP_HASH_F) {
            uint64_t bits = (uint64_t)top[-1].integer;
            if (instruction[0] == KEST_OP_HASH_F && top[-1].real == 0.0) {
                // Nought and minus nought are one value to `==`, so they are
                // one value here.
                bits = 0;
            } else if (instruction[0] == KEST_OP_HASH_F &&
                       top[-1].real != top[-1].real) {
                // And every value that is not a number is one value, for the
                // reason `float.bits` gives. See D1238.
                bits = UINT64_C(0x7FF8000000000000);
            }
            top[-1].integer = (int64_t)kest_mix(bits);
            NEXT;
        }
        case KEST_OP_HASH_T: THREADED(KEST_OP_HASH_T) {
            // FNV-1a over the bytes, because text is its bytes (D021) and two
            // pieces that compare equal are the same bytes.
            Said text = TEXT_OFF();
            uint64_t bits = 0xcbf29ce484222325ULL;
            for (uint32_t i = 0; i < text.length; i++) {
                bits ^= (unsigned char)text.bytes[i];
                bits *= 0x100000001b3ULL;
            }
            (top++)->integer = (int64_t)bits;
            SPEND_WORK(text.length);
            NEXT;
        }
        case KEST_OP_HASH_VALUE: THREADED(KEST_OP_HASH_VALUE) {
            const KestType *type = module->layout_types[READ_U16()];
            top -= type->slots;
            uint64_t bits = kest_hash_value(type, top);
            (top++)->integer = (int64_t)bits;
            NEXT;
        }
        case KEST_OP_EQ_VALUE: THREADED(KEST_OP_EQ_VALUE)
        case KEST_OP_NE_VALUE: THREADED(KEST_OP_NE_VALUE) {
            const KestType *type = module->layout_types[READ_U16()];
            top -= type->slots;
            const KestValue *right = top;
            top -= type->slots;
            bool same = values_equal(type, top, right);
            (top++)->integer = instruction[0] == KEST_OP_EQ_VALUE ? same : !same;
            NEXT;
        }
        case KEST_OP_TEXT_LEN: THREADED(KEST_OP_TEXT_LEN) {
            // A read rather than a walk: how many bytes there are is part of
            // what a piece of text is. See D964.
            Said text = TEXT_OFF();
            (top++)->integer = (int64_t)text.length;
            NEXT;
        }
        case KEST_OP_TEXT_AT: THREADED(KEST_OP_TEXT_AT) {
            int64_t index = (--top)->integer;
            Said text = TEXT_OFF();
            // How many bytes there are is part of the value, so a byte at a
            // place is a comparison and a read rather than a walk. What this
            // used to cost was the walk to the place, which is what made
            // reading the first byte of a line cost the whole line. See D372
            // and D964.
            if (index < 0 || (uint64_t)index >= text.length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside text of %u bytes",
                     (long long)index, text.length);
                return false;
            }
            (top++)->integer = (unsigned char)text.bytes[index];
            NEXT;
        }
        case KEST_OP_TEXT_IN: THREADED(KEST_OP_TEXT_IN) {
            const KestValue *held = &mine[READ_U16()];
            int64_t index = mine[READ_U16()].integer;
            Said text = said(held);
            if ((uint64_t)index >= text.length) {
                return walked_past(vmp, frame, instruction, index, text.length);
            }
            (top++)->integer = (unsigned char)text.bytes[index];
            NEXT;
        }
        case KEST_OP_TEXT_SLICE: THREADED(KEST_OP_TEXT_SLICE) {
            int64_t count = (--top)->integer;
            int64_t from = (--top)->integer;
            Said text = TEXT_OFF();
            // A cut is a place inside what it was cut from and how many bytes
            // of it: nothing is copied and nothing reaches the heap, which is
            // what makes a walk over text free. It cost a copy when a piece of
            // text was a pointer that had to end in a nought, and that is what
            // D955 could not pay for. See D964.
            if (from < 0 || count < 0 ||
                (uint64_t)from + (uint64_t)count > text.length) {
                fail(vmp, frame, instruction, "K0604",
                     "%lld bytes from %lld is outside text of %u bytes",
                     (long long)count, (long long)from, text.length);
                return false;
            }
            TEXT_ON(text.bytes + from, count);
            NEXT;
        }
        case KEST_OP_TEXT_REST: THREADED(KEST_OP_TEXT_REST) {
            int64_t at = (--top)->integer;
            Said text = TEXT_OFF();
            if (at < 0 || (uint64_t)at > text.length) {
                fail(vmp, frame, instruction, "K0604",
                     "the rest from %lld is outside text of %u bytes",
                     (long long)at, text.length);
                return false;
            }
            TEXT_ON(text.bytes + at, text.length - (uint32_t)at);
            NEXT;
        }
        case KEST_OP_TEXT_MATCHES: THREADED(KEST_OP_TEXT_MATCHES) {
            Said needle = TEXT_OFF();
            int64_t at = (--top)->integer;
            Said text = TEXT_OFF();
            if (at < 0 || (uint64_t)at > text.length) {
                fail(vmp, frame, instruction, "K0604",
                     "looking at %lld, which is outside text of %u bytes",
                     (long long)at, text.length);
                return false;
            }
            bool same = (uint64_t)at + needle.length <= text.length;
            uint32_t read = 0;
            while (same && read < needle.length &&
                   text.bytes[at + read] == needle.bytes[read]) {
                read++;
            }
            (top++)->integer = same && read == needle.length;
            SPEND_WORK(read);
            NEXT;
        }
        case KEST_OP_TEXT_FIND: THREADED(KEST_OP_TEXT_FIND) {
            int64_t from = (--top)->integer;
            Said needle = TEXT_OFF();
            Said haystack = TEXT_OFF();
            // Looking from the end of a text finds nothing, which is an answer
            // rather than a mistake.
            if (from < 0 || (uint64_t)from > haystack.length) {
                fail(vmp, frame, instruction, "K0604",
                     "looking from %lld, which is outside text of %u bytes",
                     (long long)from, haystack.length);
                return false;
            }
            int64_t found = found_at(haystack.bytes, haystack.length,
                                     needle.bytes, needle.length, from);
            // What it read is as far as it had to go: to the end of what it
            // found, or to the end of the text. See D1169.
            uint64_t read =
                (uint64_t)((found < 0 ? (int64_t)haystack.length
                                      : found + (int64_t)needle.length) -
                           from);
            (top++)->integer = found < 0 ? 0 : found;
            (top++)->integer = found >= 0;
            SPEND_WORK(read);
            NEXT;
        }
        case KEST_OP_LEN: THREADED(KEST_OP_LEN) {
            const Array *array = top[-1].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            top[-1].integer = array->length;
            NEXT;
        }
        case KEST_OP_TRUE: THREADED(KEST_OP_TRUE)
            (top++)->integer = 1;
            NEXT;
        case KEST_OP_FALSE: THREADED(KEST_OP_FALSE)
            MOVED(moved_held, sizeof(KestValue));
            (top++)->integer = 0;
            NEXT;
        case KEST_OP_POP: THREADED(KEST_OP_POP)
            top--;
            NEXT;
        case KEST_OP_POPN: THREADED(KEST_OP_POPN)
            top -= READ_U16();
            NEXT;
        case KEST_OP_ROTATE: THREADED(KEST_OP_ROTATE) {
            // The last slot is the tag and belongs first, so the run is
            // rolled by one rather than reversed.
            uint16_t count = READ_U16();
            KestValue tag = top[-1];
            MOVED(moved_shuffled, (uint64_t)count * sizeof(KestValue));
            memmove(top - count + 1, top - count,
                    sizeof(KestValue) * (size_t)(count - 1));
            top[-count] = tag;
            NEXT;
        }

        // Worked out unsigned and read back signed. What this language says
        // arithmetic does at the end of a width is wrap, and a signed overflow
        // in C is undefined rather than wrapping: a build told to look found
        // one the day a program in the library multiplied two large numbers on
        // purpose. The bits are the same either way on every machine this
        // targets; what changes is that the machine is now doing what it says.
        // See D667.
        case KEST_OP_ADD_I: THREADED(KEST_OP_ADD_I)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer +
                                        (uint64_t)right.integer));
            NEXT;
        case KEST_OP_SUB_I: THREADED(KEST_OP_SUB_I)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer -
                                        (uint64_t)right.integer));
            NEXT;
        case KEST_OP_MUL_I: THREADED(KEST_OP_MUL_I)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer *
                                        (uint64_t)right.integer));
            NEXT;
// What every whole-number division says about a nought, said in one place:
// the operand forms and the constant forms are one refusal. See D1170.
#define BY_NOUGHT()                                                            \
    do {                                                                       \
        fail(vmp, frame, instruction, "K0601", "division by zero");            \
        return false;                                                          \
    } while (0)
        case KEST_OP_DIV_I: THREADED(KEST_OP_DIV_I)
        case KEST_OP_MOD_I: THREADED(KEST_OP_MOD_I) {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                BY_NOUGHT();
            }
            // The one pair of operands whose quotient does not fit, which on
            // most machines traps rather than wrapping.
            if (left.integer == INT64_MIN && right.integer == -1) {
                (top++)->integer =
                    instruction[0] == KEST_OP_DIV_I ? INT64_MIN : 0;
                break;
            }
            (top++)->integer = instruction[0] == KEST_OP_DIV_I
                                   ? left.integer / right.integer
                                   : left.integer % right.integer;
            NEXT;
        }
// Whole-number arithmetic with a constant on its right, written out one case
// an instruction so that each is the arithmetic and nothing deciding which it
// is: `LEFT` is the left side, `DOES` what is made of it. See D1168.
#define BY_CONSTANT(LEFT, DOES)                                                \
    do {                                                                       \
        uint16_t which = READ_U16();                                           \
        OWN_CONSTANT(which);                                                   \
        MOVED(moved_held, sizeof(KestValue));                                  \
        int64_t left = (LEFT);                                                 \
        int64_t right = constants[which].integer;                              \
        DOES;                                                                  \
    } while (0)
#define DIVIDED(QUOTIENT)                                                      \
    do {                                                                       \
        if (right == 0) {                                                      \
            BY_NOUGHT();                                                       \
        }                                                                      \
        (top++)->integer = left == INT64_MIN && right == -1                    \
                               ? ((QUOTIENT) ? INT64_MIN : 0)                  \
                           : (QUOTIENT) ? left / right                         \
                                        : left % right;                        \
    } while (0)
#define CUT(KIND, MADE)                                                        \
    ((top++)->integer = kest_narrow_to((KIND), (int64_t)(MADE)))
        case KEST_OP_MOD_I_C: THREADED(KEST_OP_MOD_I_C)
            BY_CONSTANT((--top)->integer, DIVIDED(false));
            NEXT;
        case KEST_OP_DIV_I_C: THREADED(KEST_OP_DIV_I_C)
            BY_CONSTANT((--top)->integer, DIVIDED(true));
            NEXT;
        case KEST_OP_MOD_I_K: THREADED(KEST_OP_MOD_I_K) {
            uint16_t slot = READ_U16();
            OWN_SLOT(slot);
            MOVED(moved_loaded, sizeof(KestValue));
            BY_CONSTANT(mine[slot].integer, DIVIDED(false));
            NEXT;
        }
        case KEST_OP_DIV_I_K: THREADED(KEST_OP_DIV_I_K) {
            uint16_t slot = READ_U16();
            OWN_SLOT(slot);
            MOVED(moved_loaded, sizeof(KestValue));
            BY_CONSTANT(mine[slot].integer, DIVIDED(true));
            NEXT;
        }
        case KEST_OP_ADD_I_NARROW_C: THREADED(KEST_OP_ADD_I_NARROW_C) {
            uint16_t kind = READ_U16();
            BY_CONSTANT((--top)->integer,
                        CUT(kind, (uint64_t)left + (uint64_t)right));
            NEXT;
        }
        case KEST_OP_SUB_I_NARROW_C: THREADED(KEST_OP_SUB_I_NARROW_C) {
            uint16_t kind = READ_U16();
            BY_CONSTANT((--top)->integer,
                        CUT(kind, (uint64_t)left - (uint64_t)right));
            NEXT;
        }
        case KEST_OP_MUL_I_NARROW_C: THREADED(KEST_OP_MUL_I_NARROW_C) {
            uint16_t kind = READ_U16();
            BY_CONSTANT((--top)->integer,
                        CUT(kind, (uint64_t)left * (uint64_t)right));
            NEXT;
        }
        case KEST_OP_ADD_I_NARROW_K: THREADED(KEST_OP_ADD_I_NARROW_K) {
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            OWN_SLOT(slot);
            MOVED(moved_loaded, sizeof(KestValue));
            BY_CONSTANT(mine[slot].integer,
                        CUT(kind, (uint64_t)left + (uint64_t)right));
            NEXT;
        }
        case KEST_OP_SUB_I_NARROW_K: THREADED(KEST_OP_SUB_I_NARROW_K) {
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            OWN_SLOT(slot);
            MOVED(moved_loaded, sizeof(KestValue));
            BY_CONSTANT(mine[slot].integer,
                        CUT(kind, (uint64_t)left - (uint64_t)right));
            NEXT;
        }
        case KEST_OP_MUL_I_NARROW_K: THREADED(KEST_OP_MUL_I_NARROW_K) {
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            OWN_SLOT(slot);
            MOVED(moved_loaded, sizeof(KestValue));
            BY_CONSTANT(mine[slot].integer,
                        CUT(kind, (uint64_t)left * (uint64_t)right));
            NEXT;
        }
#undef BY_CONSTANT
#undef DIVIDED
#undef CUT
        case KEST_OP_DIV_U: THREADED(KEST_OP_DIV_U)
        case KEST_OP_MOD_U: THREADED(KEST_OP_MOD_U) {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                BY_NOUGHT();
            }
            uint64_t a = (uint64_t)left.integer;
            uint64_t b = (uint64_t)right.integer;
            (top++)->integer =
                (int64_t)(instruction[0] == KEST_OP_DIV_U ? a / b : a % b);
            NEXT;
        }
        case KEST_OP_AND_I: THREADED(KEST_OP_AND_I)
            top--;
            top[-1].integer &= top[0].integer;
            NEXT;
        case KEST_OP_OR_I: THREADED(KEST_OP_OR_I)
            top--;
            top[-1].integer |= top[0].integer;
            NEXT;
        case KEST_OP_XOR_I: THREADED(KEST_OP_XOR_I)
            top--;
            top[-1].integer ^= top[0].integer;
            NEXT;
        case KEST_OP_NOT_I: THREADED(KEST_OP_NOT_I)
            top[-1].integer = ~top[-1].integer;
            NEXT;
        // A shift is done in a slot and narrowed after, the same way every
        // other arithmetic is (D018). A count past the width of the slot has
        // no meaning in C, so it is answered here rather than left to the
        // machine: everything shifts out.
        case KEST_OP_SHL: THREADED(KEST_OP_SHL) {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            top[-1].integer =
                by >= 64 ? 0 : (int64_t)((uint64_t)top[-1].integer << by);
            NEXT;
        }
        case KEST_OP_SHR_I: THREADED(KEST_OP_SHR_I) {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            // Signed, so the sign is what shifts in and a negative number
            // stays negative however far it goes.
            int64_t value = top[-1].integer;
            top[-1].integer = by >= 64 ? (value < 0 ? -1 : 0) : value >> by;
            NEXT;
        }
        case KEST_OP_SHR_U: THREADED(KEST_OP_SHR_U) {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            uint64_t value = (uint64_t)top[-1].integer;
            top[-1].integer = by >= 64 ? 0 : (int64_t)(value >> by);
            NEXT;
        }
        case KEST_OP_NEG_I: THREADED(KEST_OP_NEG_I)
            // The smallest number negated is itself, which is what wrapping
            // says and what negating it signed would be undefined. See D667.
            top[-1].integer = (int64_t)(0 - (uint64_t)top[-1].integer);
            NEXT;
        case KEST_OP_I2F: THREADED(KEST_OP_I2F)
            top[-1].real = (double)top[-1].integer;
            NEXT;
        case KEST_OP_U2F: THREADED(KEST_OP_U2F)
            top[-1].real = (double)(uint64_t)top[-1].integer;
            NEXT;
        case KEST_OP_TO_F32: THREADED(KEST_OP_TO_F32)
            top[-1].real = (double)(float)top[-1].real;
            NEXT;
        // A float as its bits. What is not a number is one value, whatever
        // the processor that made it put in its sign and its payload: x86
        // makes `inf - inf` with the sign set and arm64 without it, and a
        // program that could read the difference would answer differently on
        // the two. See D1238.
        case KEST_OP_FLOAT_BITS: THREADED(KEST_OP_FLOAT_BITS) {
            uint16_t width = READ_U16();
            if (width == 32) {
                float narrow = (float)top[-1].real;
                uint32_t bits = UINT32_C(0x7FC00000);
                if (narrow == narrow) {
                    memcpy(&bits, &narrow, sizeof bits);
                }
                top[-1].integer = bits;
            } else if (top[-1].real != top[-1].real) {
                top[-1].integer = (int64_t)UINT64_C(0x7FF8000000000000);
            }
            NEXT;
        }
        case KEST_OP_BITS_F32: THREADED(KEST_OP_BITS_F32) {
            uint32_t bits = (uint32_t)top[-1].integer;
            float narrow;
            memcpy(&narrow, &bits, sizeof narrow);
            top[-1].real = narrow;
            NEXT;
        }
        case KEST_OP_F2I: THREADED(KEST_OP_F2I)
            // Where a number outside the width stops, which the type layer
            // works out for a constant as well: one answer, in one place. See
            // D669.
            top[-1].integer = kest_real_to_int(READ_U16(), top[-1].real);
            NEXT;
        case KEST_OP_NARROW: THREADED(KEST_OP_NARROW)
            // Every integer in a slot is kept at its declared width, sign
            // extended or zero extended, so a comparison and a division do not
            // each need to know how wide it is.
            top[-1].integer = kest_narrow_to(READ_U16(), top[-1].integer);
            NEXT;

        // The same cut, arriving with the arithmetic that needed it. Written
        // out rather than falling through a shared macro because the whole
        // point of them is that there is one dispatch and one read of the
        // width between the operands and the answer. See D868.
        case KEST_OP_ADD_I_NARROW: THREADED(KEST_OP_ADD_I_NARROW)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer +
                                        (uint64_t)right.integer));
            top[-1].integer = kest_narrow_to(READ_U16(), top[-1].integer);
            NEXT;
        case KEST_OP_SUB_I_NARROW: THREADED(KEST_OP_SUB_I_NARROW)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer -
                                        (uint64_t)right.integer));
            top[-1].integer = kest_narrow_to(READ_U16(), top[-1].integer);
            NEXT;
        case KEST_OP_MUL_I_NARROW: THREADED(KEST_OP_MUL_I_NARROW)
            BINARY_I(integer, (int64_t)((uint64_t)left.integer *
                                        (uint64_t)right.integer));
            top[-1].integer = kest_narrow_to(READ_U16(), top[-1].integer);
            NEXT;

        // The same four with the store they were feeding taken in. What goes
        // is the push and the pop between them, which is a dependency through
        // memory rather than two instructions standing beside each other.
        // See D1014.
        case KEST_OP_ADD_I_NARROW_TO: THREADED(KEST_OP_ADD_I_NARROW_TO) {
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            KestValue right = *--top;
            KestValue left = *--top;
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot].integer = kest_narrow_to(
                kind, (int64_t)((uint64_t)left.integer +
                                (uint64_t)right.integer));
            NEXT;
        }
        // A constant written into a local. See D1155.
        case KEST_OP_STORE_K: THREADED(KEST_OP_STORE_K) {
            uint16_t slot = READ_U16();
            uint16_t which = READ_U16();
            OWN_SLOT_AND_CONSTANT(slot, which);
            MOVED(moved_held, sizeof(KestValue));
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot] = constants[which];
            NEXT;
        }
        // A local moved by a constant where it is. See D1155.
        // One case for both ways round, which is measured rather than
        // preferred: the two written apart made `rules` five per cent slower
        // in cycles for fewer instructions, where the compiler put the rest
        // of the loop. See D1168.
        case KEST_OP_ADD_K_SELF: THREADED(KEST_OP_ADD_K_SELF)
        case KEST_OP_SUB_K_SELF: THREADED(KEST_OP_SUB_K_SELF) {
            uint8_t which_way = *instruction;
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            uint16_t which = READ_U16();
            OWN_SLOT_AND_CONSTANT(slot, which);
            MOVED(moved_loaded, sizeof(KestValue));
            MOVED(moved_held, sizeof(KestValue));
            MOVED(moved_stored, sizeof(KestValue));
            uint64_t by = (uint64_t)constants[which].integer;
            uint64_t was = (uint64_t)mine[slot].integer;
            mine[slot].integer = kest_narrow_to(
                kind, (int64_t)(which_way == KEST_OP_ADD_K_SELF ? was + by
                                                               : was - by));
            NEXT;
        }
        case KEST_OP_SUB_I_NARROW_TO: THREADED(KEST_OP_SUB_I_NARROW_TO) {
            uint16_t kind = READ_U16();
            uint16_t slot = READ_U16();
            KestValue right = *--top;
            KestValue left = *--top;
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot].integer = kest_narrow_to(
                kind, (int64_t)((uint64_t)left.integer -
                                (uint64_t)right.integer));
            NEXT;
        }
        case KEST_OP_ADD_F_TO: THREADED(KEST_OP_ADD_F_TO) {
            uint16_t slot = READ_U16();
            KestValue right = *--top;
            KestValue left = *--top;
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot].real = left.real + right.real;
            NEXT;
        }
// Two float locals made into a third, one case each way round. See D1166.
#define TWO_LOCALS(OP)                                                         \
    do {                                                                       \
        uint16_t slot = READ_U16();                                            \
        uint16_t first = READ_U16();                                           \
        uint16_t second = READ_U16();                                          \
        OWN_SLOT(slot);                                                        \
        OWN_SLOT(first);                                                       \
        OWN_SLOT(second);                                                      \
        MOVED(moved_loaded, 2 * sizeof(KestValue));                            \
        MOVED(moved_stored, sizeof(KestValue));                                \
        mine[slot].real = mine[first].real OP mine[second].real;               \
    } while (0)
        case KEST_OP_ADD_F_LL: THREADED(KEST_OP_ADD_F_LL)
            TWO_LOCALS(+);
            NEXT;
        case KEST_OP_SUB_F_LL: THREADED(KEST_OP_SUB_F_LL)
            TWO_LOCALS(-);
            NEXT;
#undef TWO_LOCALS
        case KEST_OP_SUB_F_TO: THREADED(KEST_OP_SUB_F_TO) {
            uint16_t slot = READ_U16();
            KestValue right = *--top;
            KestValue left = *--top;
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u)) {
                return false;
            }
#endif
            MOVED(moved_stored, sizeof(KestValue));
            mine[slot].real = left.real - right.real;
            NEXT;
        }
        case KEST_OP_ADD_F: THREADED(KEST_OP_ADD_F)
            BINARY_I(real, left.real + right.real);
            NEXT;
        case KEST_OP_SUB_F: THREADED(KEST_OP_SUB_F)
            BINARY_I(real, left.real - right.real);
            NEXT;
        case KEST_OP_MUL_F: THREADED(KEST_OP_MUL_F)
            BINARY_I(real, left.real * right.real);
            NEXT;
        case KEST_OP_DIV_F: THREADED(KEST_OP_DIV_F)
            BINARY_I(real, left.real / right.real);
            NEXT;
        case KEST_OP_MOD_F: THREADED(KEST_OP_MOD_F)
            // Nought on the right is not stopped here the way it is for a
            // whole number: the answer is what is not a number, which is what
            // `/` already gives beside it. See D776.
            BINARY_I(real, kest_left_over(left.real, right.real));
            NEXT;
        case KEST_OP_NEG_F: THREADED(KEST_OP_NEG_F)
            top[-1].real = -top[-1].real;
            NEXT;

        case KEST_OP_ADD_F32: THREADED(KEST_OP_ADD_F32)
            BINARY_I(real, (double)((float)left.real + (float)right.real));
            NEXT;
        case KEST_OP_SUB_F32: THREADED(KEST_OP_SUB_F32)
            BINARY_I(real, (double)((float)left.real - (float)right.real));
            NEXT;
        case KEST_OP_MUL_F32: THREADED(KEST_OP_MUL_F32)
            BINARY_I(real, (double)((float)left.real * (float)right.real));
            NEXT;
        case KEST_OP_DIV_F32: THREADED(KEST_OP_DIV_F32)
            BINARY_I(real, (double)((float)left.real / (float)right.real));
            NEXT;
        case KEST_OP_MOD_F32: THREADED(KEST_OP_MOD_F32)
            BINARY_I(real, (double)(float)kest_left_over(
                               (double)(float)left.real,
                               (double)(float)right.real));
            NEXT;
        case KEST_OP_NEG_F32: THREADED(KEST_OP_NEG_F32)
            top[-1].real = (double)(-(float)top[-1].real);
            NEXT;

        case KEST_OP_LT_I: THREADED(KEST_OP_LT_I)
            BINARY_I(integer, left.integer < right.integer);
            NEXT;
        case KEST_OP_LE_I: THREADED(KEST_OP_LE_I)
            BINARY_I(integer, left.integer <= right.integer);
            NEXT;
        case KEST_OP_GT_I: THREADED(KEST_OP_GT_I)
            BINARY_I(integer, left.integer > right.integer);
            NEXT;
        case KEST_OP_GE_I: THREADED(KEST_OP_GE_I)
            BINARY_I(integer, left.integer >= right.integer);
            NEXT;
        case KEST_OP_LT_U: THREADED(KEST_OP_LT_U)
            BINARY_I(integer, (uint64_t)left.integer < (uint64_t)right.integer);
            NEXT;
        case KEST_OP_LE_U: THREADED(KEST_OP_LE_U)
            BINARY_I(integer,
                     (uint64_t)left.integer <= (uint64_t)right.integer);
            NEXT;
        case KEST_OP_GT_U: THREADED(KEST_OP_GT_U)
            BINARY_I(integer, (uint64_t)left.integer > (uint64_t)right.integer);
            NEXT;
        case KEST_OP_GE_U: THREADED(KEST_OP_GE_U)
            BINARY_I(integer,
                     (uint64_t)left.integer >= (uint64_t)right.integer);
            NEXT;
        case KEST_OP_LT_F: THREADED(KEST_OP_LT_F)
            BINARY_I(integer, left.real < right.real);
            NEXT;
        case KEST_OP_LE_F: THREADED(KEST_OP_LE_F)
            BINARY_I(integer, left.real <= right.real);
            NEXT;
        case KEST_OP_GT_F: THREADED(KEST_OP_GT_F)
            BINARY_I(integer, left.real > right.real);
            NEXT;
        case KEST_OP_GE_F: THREADED(KEST_OP_GE_F)
            BINARY_I(integer, left.real >= right.real);
            NEXT;

        case KEST_OP_EQ_I: THREADED(KEST_OP_EQ_I)
            BINARY_I(integer, left.integer == right.integer);
            NEXT;
        case KEST_OP_NE_I: THREADED(KEST_OP_NE_I)
            BINARY_I(integer, left.integer != right.integer);
            NEXT;
        case KEST_OP_EQ_F: THREADED(KEST_OP_EQ_F)
            BINARY_I(integer, left.real == right.real);
            NEXT;
        case KEST_OP_NE_F: THREADED(KEST_OP_NE_F)
            BINARY_I(integer, left.real != right.real);
            NEXT;
        case KEST_OP_EQ_T: THREADED(KEST_OP_EQ_T)
            TEXT_ORDER(order == 0);
            NEXT;
        case KEST_OP_NE_T: THREADED(KEST_OP_NE_T)
            TEXT_ORDER(order != 0);
            NEXT;

        case KEST_OP_LT_T: THREADED(KEST_OP_LT_T)
            TEXT_ORDER(order < 0);
            NEXT;
        case KEST_OP_LE_T: THREADED(KEST_OP_LE_T)
            TEXT_ORDER(order <= 0);
            NEXT;
        case KEST_OP_GT_T: THREADED(KEST_OP_GT_T)
            TEXT_ORDER(order > 0);
            NEXT;
        case KEST_OP_GE_T: THREADED(KEST_OP_GE_T)
            TEXT_ORDER(order >= 0);
            NEXT;

        case KEST_OP_NOT: THREADED(KEST_OP_NOT)
            top[-1].integer = !top[-1].integer;
            NEXT;

        case KEST_OP_JUMP: THREADED(KEST_OP_JUMP) {
            // Read the distance before moving, because the read moves too.
            uint16_t distance = READ_U16();
            ip += distance;
            NEXT;
        }
        case KEST_OP_JUMP_FALSE: THREADED(KEST_OP_JUMP_FALSE) {
            uint16_t distance = READ_U16();
            if ((--top)->integer == 0) {
                ip += distance;
            }
            NEXT;
        }
        case KEST_OP_JUMP_TRUE: THREADED(KEST_OP_JUMP_TRUE) {
            uint16_t distance = READ_U16();
            if ((--top)->integer != 0) {
                ip += distance;
            }
            NEXT;
        }
        // The compare and the branch in one. The operands are whole numbers
        // because that is the only pair the compiler fuses.
#define JUMP_UNLESS(expression)                                                \
    do {                                                                       \
        uint16_t distance = READ_U16();                                        \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        if (!(expression)) {                                                   \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)

#define JUMP_IF(expression)                                                    \
    do {                                                                       \
        uint16_t distance = READ_U16();                                        \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        if (expression) {                                                      \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)

// A local against a constant, and the jump. What `load.k` would have put on
// the stack is read where it is, so nothing is pushed and nothing is popped.
// See D1154.
#define JUMP_UNLESS_K(test)                                                    \
    do {                                                                       \
        uint16_t slot = READ_U16();                                            \
        uint16_t which = READ_U16();                                           \
        uint16_t distance = READ_U16();                                        \
        OWN_SLOT_AND_CONSTANT(slot, which);                                    \
        MOVED(moved_loaded, sizeof(KestValue));                                \
        MOVED(moved_held, sizeof(KestValue));                                  \
        int64_t left = mine[slot].integer;                                     \
        int64_t right = constants[which].integer;                              \
        if (!(test)) {                                                         \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)

// What is on the stack against a constant, and the jump. See D1155.
#define JUMP_UNLESS_C(test)                                                    \
    do {                                                                       \
        uint16_t which = READ_U16();                                           \
        uint16_t distance = READ_U16();                                        \
        OWN_CONSTANT(which);                                                   \
        MOVED(moved_held, sizeof(KestValue));                                  \
        int64_t left = (--top)->integer;                                       \
        int64_t right = constants[which].integer;                              \
        if (!(test)) {                                                         \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)

// An element weighed against a constant, and the jump: what `index.ll`
// reads and `jump.false.*.c` weighs, with nothing on the stack between.
// See D1178.
#define JUMP_UNLESS_E(test)                                                    \
    do {                                                                       \
        uint16_t holds = READ_U16();                                           \
        uint16_t at = READ_U16();                                              \
        uint16_t of_which = READ_U16();                                        \
        uint16_t which = READ_U16();                                           \
        uint16_t distance = READ_U16();                                        \
        OF_THE_MODULE(of_which, module->layout_count, "a layout");             \
        OWN_ELEMENT_AND_CONSTANT(holds, at, which);                            \
        MOVED(moved_loaded, 2 * sizeof(KestValue));                            \
        MOVED(moved_held, sizeof(KestValue));                                  \
        const KestLayout *layout = &module->layouts[of_which];                 \
        int64_t index = mine[at].integer;                                      \
        const Array *array = mine[holds].object;                               \
        HOLD(array, KEST_IS_ARRAY, "an array");                                \
        IN_ARRAY(index, array);                                                \
        /* Two, because a read of a piece is written for text as well. */    \
        KestValue element[2];                                                  \
        READ_INTO(element, layout,                                             \
                  array->bytes + (size_t)index * array->stride);               \
        int64_t left = element[0].integer;                                     \
        int64_t right = constants[which].integer;                              \
        if (!(test)) {                                                         \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)

        case KEST_OP_JUMP_FALSE_LT_E: THREADED(KEST_OP_JUMP_FALSE_LT_E)
            JUMP_UNLESS_E(left < right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_E: THREADED(KEST_OP_JUMP_FALSE_LE_E)
            JUMP_UNLESS_E(left <= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_E: THREADED(KEST_OP_JUMP_FALSE_GT_E)
            JUMP_UNLESS_E(left > right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_E: THREADED(KEST_OP_JUMP_FALSE_GE_E)
            JUMP_UNLESS_E(left >= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_E: THREADED(KEST_OP_JUMP_FALSE_EQ_E)
            JUMP_UNLESS_E(left == right);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_E: THREADED(KEST_OP_JUMP_FALSE_NE_E)
            JUMP_UNLESS_E(left != right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LT_C: THREADED(KEST_OP_JUMP_FALSE_LT_C)
            JUMP_UNLESS_C(left < right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_C: THREADED(KEST_OP_JUMP_FALSE_LE_C)
            JUMP_UNLESS_C(left <= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_C: THREADED(KEST_OP_JUMP_FALSE_GT_C)
            JUMP_UNLESS_C(left > right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_C: THREADED(KEST_OP_JUMP_FALSE_GE_C)
            JUMP_UNLESS_C(left >= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_C: THREADED(KEST_OP_JUMP_FALSE_EQ_C)
            JUMP_UNLESS_C(left == right);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_C: THREADED(KEST_OP_JUMP_FALSE_NE_C)
            JUMP_UNLESS_C(left != right);
            NEXT;
// A float local against a constant and the jump, taken when the answer is
// `taken`: the float jumps are written both ways round. See D1165.
#define JUMP_ON_FK(taken, test)                                                \
    do {                                                                       \
        uint16_t slot = READ_U16();                                            \
        uint16_t which = READ_U16();                                           \
        uint16_t distance = READ_U16();                                        \
        OWN_SLOT_AND_CONSTANT(slot, which);                                    \
        MOVED(moved_loaded, sizeof(KestValue));                                \
        MOVED(moved_held, sizeof(KestValue));                                  \
        double left = mine[slot].real;                                         \
        double right = constants[which].real;                                  \
        if ((test) == (taken)) {                                               \
            ip += distance;                                                    \
        }                                                                      \
    } while (0)
        case KEST_OP_JUMP_FALSE_LT_FK: THREADED(KEST_OP_JUMP_FALSE_LT_FK)
            JUMP_ON_FK(false, left < right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_FK: THREADED(KEST_OP_JUMP_FALSE_LE_FK)
            JUMP_ON_FK(false, left <= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_FK: THREADED(KEST_OP_JUMP_FALSE_GT_FK)
            JUMP_ON_FK(false, left > right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_FK: THREADED(KEST_OP_JUMP_FALSE_GE_FK)
            JUMP_ON_FK(false, left >= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_FK: THREADED(KEST_OP_JUMP_FALSE_EQ_FK)
            JUMP_ON_FK(false, left == right);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_FK: THREADED(KEST_OP_JUMP_FALSE_NE_FK)
            JUMP_ON_FK(false, left != right);
            NEXT;
        case KEST_OP_JUMP_TRUE_LT_FK: THREADED(KEST_OP_JUMP_TRUE_LT_FK)
            JUMP_ON_FK(true, left < right);
            NEXT;
        case KEST_OP_JUMP_TRUE_LE_FK: THREADED(KEST_OP_JUMP_TRUE_LE_FK)
            JUMP_ON_FK(true, left <= right);
            NEXT;
        case KEST_OP_JUMP_TRUE_GT_FK: THREADED(KEST_OP_JUMP_TRUE_GT_FK)
            JUMP_ON_FK(true, left > right);
            NEXT;
        case KEST_OP_JUMP_TRUE_GE_FK: THREADED(KEST_OP_JUMP_TRUE_GE_FK)
            JUMP_ON_FK(true, left >= right);
            NEXT;
        case KEST_OP_JUMP_TRUE_EQ_FK: THREADED(KEST_OP_JUMP_TRUE_EQ_FK)
            JUMP_ON_FK(true, left == right);
            NEXT;
        case KEST_OP_JUMP_TRUE_NE_FK: THREADED(KEST_OP_JUMP_TRUE_NE_FK)
            JUMP_ON_FK(true, left != right);
            NEXT;
#undef JUMP_ON_FK
        case KEST_OP_JUMP_FALSE_LT_K: THREADED(KEST_OP_JUMP_FALSE_LT_K)
            JUMP_UNLESS_K(left < right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_K: THREADED(KEST_OP_JUMP_FALSE_LE_K)
            JUMP_UNLESS_K(left <= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_K: THREADED(KEST_OP_JUMP_FALSE_GT_K)
            JUMP_UNLESS_K(left > right);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_K: THREADED(KEST_OP_JUMP_FALSE_GE_K)
            JUMP_UNLESS_K(left >= right);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_K: THREADED(KEST_OP_JUMP_FALSE_EQ_K)
            JUMP_UNLESS_K(left == right);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_K: THREADED(KEST_OP_JUMP_FALSE_NE_K)
            JUMP_UNLESS_K(left != right);
            NEXT;
        case KEST_OP_JUMP_FALSE_LT_I: THREADED(KEST_OP_JUMP_FALSE_LT_I)
            JUMP_UNLESS(left.integer < right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_I: THREADED(KEST_OP_JUMP_FALSE_LE_I)
            JUMP_UNLESS(left.integer <= right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_I: THREADED(KEST_OP_JUMP_FALSE_GT_I)
            JUMP_UNLESS(left.integer > right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_I: THREADED(KEST_OP_JUMP_FALSE_GE_I)
            JUMP_UNLESS(left.integer >= right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_I: THREADED(KEST_OP_JUMP_FALSE_EQ_I)
            JUMP_UNLESS(left.integer == right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_I: THREADED(KEST_OP_JUMP_FALSE_NE_I)
            JUMP_UNLESS(left.integer != right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_LT_I: THREADED(KEST_OP_JUMP_TRUE_LT_I)
            JUMP_IF(left.integer < right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_LE_I: THREADED(KEST_OP_JUMP_TRUE_LE_I)
            JUMP_IF(left.integer <= right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_GT_I: THREADED(KEST_OP_JUMP_TRUE_GT_I)
            JUMP_IF(left.integer > right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_GE_I: THREADED(KEST_OP_JUMP_TRUE_GE_I)
            JUMP_IF(left.integer >= right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_EQ_I: THREADED(KEST_OP_JUMP_TRUE_EQ_I)
            JUMP_IF(left.integer == right.integer);
            NEXT;
        case KEST_OP_JUMP_TRUE_NE_I: THREADED(KEST_OP_JUMP_TRUE_NE_I)
            JUMP_IF(left.integer != right.integer);
            NEXT;
        case KEST_OP_JUMP_FALSE_LT_F: THREADED(KEST_OP_JUMP_FALSE_LT_F)
            JUMP_UNLESS(left.real < right.real);
            NEXT;
        case KEST_OP_JUMP_FALSE_LE_F: THREADED(KEST_OP_JUMP_FALSE_LE_F)
            JUMP_UNLESS(left.real <= right.real);
            NEXT;
        case KEST_OP_JUMP_FALSE_GT_F: THREADED(KEST_OP_JUMP_FALSE_GT_F)
            JUMP_UNLESS(left.real > right.real);
            NEXT;
        case KEST_OP_JUMP_FALSE_GE_F: THREADED(KEST_OP_JUMP_FALSE_GE_F)
            JUMP_UNLESS(left.real >= right.real);
            NEXT;
        case KEST_OP_JUMP_FALSE_EQ_F: THREADED(KEST_OP_JUMP_FALSE_EQ_F)
            JUMP_UNLESS(left.real == right.real);
            NEXT;
        case KEST_OP_JUMP_FALSE_NE_F: THREADED(KEST_OP_JUMP_FALSE_NE_F)
            JUMP_UNLESS(left.real != right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_LT_F: THREADED(KEST_OP_JUMP_TRUE_LT_F)
            JUMP_IF(left.real < right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_LE_F: THREADED(KEST_OP_JUMP_TRUE_LE_F)
            JUMP_IF(left.real <= right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_GT_F: THREADED(KEST_OP_JUMP_TRUE_GT_F)
            JUMP_IF(left.real > right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_GE_F: THREADED(KEST_OP_JUMP_TRUE_GE_F)
            JUMP_IF(left.real >= right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_EQ_F: THREADED(KEST_OP_JUMP_TRUE_EQ_F)
            JUMP_IF(left.real == right.real);
            NEXT;
        case KEST_OP_JUMP_TRUE_NE_F: THREADED(KEST_OP_JUMP_TRUE_NE_F)
            JUMP_IF(left.real != right.real);
            NEXT;
#undef JUMP_UNLESS
#undef JUMP_IF

        case KEST_OP_LOOP: THREADED(KEST_OP_LOOP) {
            uint16_t distance = READ_U16();
            SPEND();
            ip -= distance;
            NEXT;
        }

        case KEST_OP_NEXT_LESS_I: THREADED(KEST_OP_NEXT_LESS_I) {
            uint16_t slot = READ_U16();
            uint16_t limit = READ_U16();
            uint16_t distance = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u) ||
                !own_slots(vmp, frame, instruction, limit, limit + 1u)) {
                return false;
            }
#endif
            if (++mine[slot].integer < mine[limit].integer) {
                SPEND();
                ip -= distance;
            }
            NEXT;
        }

        case KEST_OP_NEXT_LESS_U: THREADED(KEST_OP_NEXT_LESS_U) {
            uint16_t slot = READ_U16();
            uint16_t limit = READ_U16();
            uint16_t distance = READ_U16();
#if KEST_CHECKED
            if (!own_slots(vmp, frame, instruction, slot, slot + 1u) ||
                !own_slots(vmp, frame, instruction, limit, limit + 1u)) {
                return false;
            }
#endif
            // Counted on as the unsigned number it is: a walk of `u64`s goes
            // past the top of the signed range, and one more than that as a
            // signed number is not a wrapped number but no number C
            // promises anything about. See D1212.
            uint64_t next = (uint64_t)mine[slot].integer + 1u;
            mine[slot].integer = (int64_t)next;
            if (next < (uint64_t)mine[limit].integer) {
                SPEND();
                ip -= distance;
            }
            NEXT;
        }

        case KEST_OP_SCRATCH: THREADED(KEST_OP_SCRATCH) {
            uint16_t where = READ_U16();
            // The door both engines go through, so working memory is opened
            // one way and says one thing when it cannot be. See D1119.
            frame->ip = ip;
            rt->asked_at = instruction;
            int64_t opened = 0;
            if (!kest_region_open(rt, KEST_WHERE_RUNNING, &opened)) {
                return false;
            }
            mine[where].integer = opened;
            NEXT;
        }
        case KEST_OP_UNSCRATCH: THREADED(KEST_OP_UNSCRATCH) {
            uint16_t where = READ_U16();
            frame->ip = ip;
            rt->asked_at = instruction;
            if (!kest_region_close(rt, mine[where].integer,
                                   KEST_WHERE_RUNNING)) {
                return false;
            }
            NEXT;
        }
        case KEST_OP_CALL: THREADED(KEST_OP_CALL) {
            SPEND();
            uint16_t index = READ_U16();
            uint16_t argument_slots = READ_U16();
            OF_THE_MODULE(index, module->count, "a function");
            const KestChunk *callee = module->functions[index];
            if (rt->entered != NULL && index < rt->entered_room) {
                rt->entered[index]++;
            }

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                what_it_needed(vmp, rt, entry);
                return false;
            }
            KestValue *base = top - argument_slots;
#if KEST_CHECKED
            // What a call hands over, held against what it is entering and
            // against what the caller had to hand. The shape of a call is the
            // checker's and this is the compiler's arithmetic: a count one
            // slot out puts the callee's names over the caller's, and every
            // slot it reads is somebody else's while the program keeps
            // running. `call.value` has been held to the first of these since
            // D058, because that is the call the promise's second proof cannot
            // see through; this is the one it can, and nothing weighed it.
            // See D901.
            rt->guarded++;
            if (argument_slots != callee->param_slots ||
                base < mine + frame->chunk->slot_count) {
                fail(vmp, frame, instruction, "K0655",
                     "this call hands over %u slot(s) from %d above its own "
                     "names, and `%s` takes %u",
                     argument_slots,
                     (int)(top - mine - frame->chunk->slot_count),
                     callee->wrote, callee->param_slots);
                kest_diags_fault(vmp->diags,
                                 "the compiler's count of the operand stack "
                                 "and what the machine moved disagree");
                return false;
            }
            // And what is in them, which the count says nothing about. A body
            // says what each argument is made of, a piece a slot, and that is
            // what a host is held to at a crossing -- a host is asked and the
            // compiler is trusted, which is the same division `handed_well`
            // draws from the other side. Turned inward: the slots the caller
            // pushed, held to the kinds the callee declares. A value with a
            // tag in it is left out, because what its slots hold is the tag's
            // to say and the pieces of one are not a run. See D902.
            {
                uint32_t slot = 0;
                rt->guarded++;
                for (uint16_t which = 0;
                     which < callee->takes_count && slot < argument_slots;
                     which++) {
                    const KestLayout *what =
                        &module->layouts[callee->takes[which]];
                    for (uint16_t piece = 0;
                         piece < what->count && slot < argument_slots;
                         piece++,
                                  slot += what->pieces[piece - 1].kind ==
                                                  KEST_L_TEXT
                                              ? 2
                                              : 1) {
                        if (what->tagged ||
                            fits_the_piece(what->pieces[piece].kind,
                                           base[slot])) {
                            continue;
                        }
                        fail(vmp, frame, instruction, "K0655",
                             "this call hands `%s` something in slot %u that "
                             "no `%s` holds",
                             callee->wrote, slot,
                             the_width_of(what->pieces[piece].kind));
                        kest_diags_fault(vmp->diags,
                                         "what the compiler put in a frame "
                                         "and what the body takes disagree");
                        return false;
                    }
                }
            }
#endif
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                // With the number, because `out of stack` on its own tells
                // a reader nothing about how much there was: the two ways to
                // answer it are fewer frames and a host that asks for more,
                // and both need the number. See D523.
                fail(vmp, frame, instruction, "K0602",
                     "this call wants more than the %u slots of stack there "
                     "are", rt->stack_slots);
                what_it_needed(vmp, rt, entry);
                return false;
            }

            // Where the caller is, written back before the callee stands on
            // top of it: a fault inside the callee reads it to say where the
            // call was written, and a `return` comes back to it. See D869.
            frame->ip = ip;
            // A body the host's compiler compiled runs here instead of the
            // instructions under it. Everything around the call is the same:
            // it is handed the frame where the caller left the arguments, it
            // writes its answer where a `return` would, and a frame is pushed
            // for it so that what is deep in a run, what a fault says it was
            // called from, and what a machine says it needs are all still
            // true of it. See D1094.
            if (callee->native != NULL && !rt->untrusted) {
                frame = &rt->frames[rt->frame_count++];
                FRESH(frame);
                frame->chunk = callee;
                frame->ip = callee->code;
                frame->base = base;
                frame->said_at = 0;
                uint16_t gave = 0;
                // Where the machine's stack has got to, for the collector: a
                // body written in C keeps what it is working on in the frame
                // it was handed, and a walk of the stack has to reach the end
                // of it. What it was is put back, because a native called
                // from inside a host call is standing on one.
                KestValue *was_top = rt->running_top;
                rt->running_top = base + callee->slot_count +
                                  callee->stack_needed;
                bool went = callee->native(rt, base, &gave);
                rt->running_top = was_top;
                rt->frame_count--;
                frame = &rt->frames[rt->frame_count - 1];
                if (!went) {
                    return false;
                }
                top = base + gave;
                break;
            }
            frame = &rt->frames[rt->frame_count++];
            FRESH(frame);
            frame->chunk = callee;
            frame->ip = callee->code;
            frame->base = base;
            frame->said_at = 0;
            ip = callee->code;
            mine = base;
            constants = callee->constants;
            top = base + callee->slot_count;
            NEXT;
        }

        case KEST_OP_CALL_VALUE: THREADED(KEST_OP_CALL_VALUE) {
            SPEND();
            uint16_t argument_slots = READ_U16();
            uint16_t coming_back = READ_U16();
            int64_t which = (--top)->integer;
            // The three questions in front of this call, asked in the one
            // place a body the host's compiler compiled asks them too. See
            // D1107.
            const char *code = NULL;
            char said[160];
            const char *suggest = NULL;
            const char *fault = NULL;
            const KestChunk *callee =
                the_function(module, frame->chunk, which, argument_slots,
                             coming_back, &code, said, sizeof said, &suggest,
                             &fault);
            if (callee == NULL) {
                fail(vmp, frame, instruction, code, "%s", said);
                if (suggest != NULL) {
                    kest_diags_suggest(vmp->diags, "%s", suggest);
                }
                if (fault != NULL) {
                    kest_diags_fault(vmp->diags, fault);
                }
                return false;
            }

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                what_it_needed(vmp, rt, entry);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                // The same sentence in front of the other call instruction,
                // which is the pair D440 is about. See D523.
                fail(vmp, frame, instruction, "K0602",
                     "this call wants more than the %u slots of stack there "
                     "are", rt->stack_slots);
                what_it_needed(vmp, rt, entry);
                return false;
            }

            frame->ip = ip;
            frame = &rt->frames[rt->frame_count++];
            FRESH(frame);
            frame->chunk = callee;
            frame->ip = callee->code;
            frame->base = base;
            ip = callee->code;
            mine = base;
            constants = callee->constants;
            top = base + callee->slot_count;
            NEXT;
        }

        case KEST_OP_CALL_HOST: THREADED(KEST_OP_CALL_HOST) {
            SPEND();
            uint16_t index = READ_U16();
            uint16_t argument_slots = READ_U16();
            uint16_t result_slots = READ_U16();
            OF_THE_MODULE(index, module->extern_count, "a door of the host");
            KestValue *base = top - argument_slots;
            // A host may call back in, and what it calls stands on frames
            // this one is under: where this frame is has to be in the frame
            // before the host runs. See D869.
            frame->ip = ip;
            rt->asked_at = instruction;
            // And the budget, which is held in a register while this body
            // runs and is nobody else's until it is put back. A host may call
            // in again from in there: what that call could see was the budget
            // minus this body's whole slice, so an outer call given three
            // hundred steps took all three hundred and the call inside it was
            // refused after fifty. Put back here and taken again below, so
            // the number a reentrant call reads is the number that is left.
            // See D929.
            if (rt->fuel_bounded) {
                rt->fuel_left += slice;
            }
            slice = 0;
            // And the crossing itself, which is the door a body the host's
            // compiler compiled goes through as well: everything the boundary
            // asks is asked in one place, and both engines cross the same
            // way. See D1108.
            if (!kest_call_host(rt, index, base, argument_slots, result_slots,
                                KEST_WHERE_RUNNING)) {
                return false;
            }
            top = base + result_slots;
            NEXT;
        }

        // A breakpoint. The machine stops where it is: the frame keeps the
        // instruction the byte was written over and where the operand stack
        // had got to, so `kest_resume` picks both up and carries on. Nothing
        // is unwound and nothing is said -- a stop is not a refusal. See D991.
        case KEST_OP_STOP: THREADED(KEST_OP_STOP) {
            // Unless the run this is in is one the host made from inside a
            // call of its own. A stop keeps the frames where they are so that
            // `kest_resume` can carry on, and there is nothing to carry on
            // into here: what this run is standing under is a C frame of the
            // host's, and by the time a host could ask, that function has
            // returned. So it is a refusal, said where it happened, rather
            // than a stop that leaves the call the host is in the middle of
            // answering a number that is nothing. See D1079.
            if (rt->running_top != NULL) {
                fail(vmp, frame, instruction, "K0708",
                     "this machine cannot stop in a call the host made back "
                     "in");
                kest_diags_suggest(rt->diags,
                                   "take the breakpoint out of what a bound "
                                   "function calls, or write one in the call "
                                   "the host makes from outside");
                return false;
            }
            frame->ip = instruction;
            rt->stopped_top = top;
            rt->stopped_at = instruction;
            rt->running_frames = 0;
            rt->running_top = NULL;
            return false;
        }

        case KEST_OP_RETURN: THREADED(KEST_OP_RETURN) {
            uint16_t count = READ_U16();
#if KEST_CHECKED
            // And what a body leaves behind when it goes. The guard at the top
            // of this loop says a body never went deeper than it was given;
            // this says it comes back with nothing over, which is the other
            // end of the same count and the one the machine can be sure of
            // rather than bounded by. A body that leaks an operand is a body
            // whose every statement after the leak worked at the wrong depth,
            // and nothing was wrong enough to be noticed: `return` moves the
            // result to the bottom of the frame either way, so the caller is
            // handed the right answer out of a body that lost count. D809 held
            // this while it was being made and the machine never asked. See
            // D900.
            rt->guarded++;
            if (top - count != mine + frame->chunk->slot_count) {
                fail(vmp, frame, instruction, "K0655",
                     "this body gives back %u slot(s) and has %d more than it "
                     "was given",
                     count,
                     (int)(top - count - mine - frame->chunk->slot_count));
                kest_diags_fault(vmp->diags,
                                 "the compiler's count of the operand stack "
                                 "and what the machine moved disagree");
                return false;
            }
            // And what is in what it hands back. A chunk says what it gives
            // the same way it says what it takes -- a layout, a piece a slot
            // -- and D902 held the slots a call hands over to the callee's.
            // This is the other end of the same journey, and the last of the
            // four things the boundary asks a host that the machine did not
            // ask itself. A value with a tag in it is left out for D899's
            // reason. See D906.
            if (frame->chunk->returns_value && count > 0) {
                rt->guarded++;
                const KestLayout *given =
                    &module->layouts[frame->chunk->gives];
                uint16_t slot = 0;
                for (uint16_t piece = 0;
                     !given->tagged && piece < given->count && slot < count;
                     piece++,
                              slot = (uint16_t)(slot +
                                                (given->pieces[piece - 1]
                                                         .kind == KEST_L_TEXT
                                                     ? 2
                                                     : 1))) {
                    if (fits_the_piece(given->pieces[piece].kind,
                                       (top - count)[slot])) {
                        continue;
                    }
                    fail(vmp, frame, instruction, "K0655",
                         "this gives back something in slot %u that no `%s` "
                         "holds",
                         slot, the_width_of(given->pieces[piece].kind));
                    kest_diags_fault(vmp->diags,
                                     "what the compiler put in a frame and "
                                     "what the body gives back disagree");
                    return false;
                }
            }
#endif
            // The result lands where the arguments were, which is where the
            // caller left room for it. One slot is most answers, and moved
            // without a call into the C library.
            KestValue *base = mine;
            if (count == 1) {
                base[0] = top[-1];
            } else {
                memmove(base, top - count, sizeof(KestValue) * count);
            }

            rt->frame_count--;
            if (rt->frame_count == under) {
                *returned = count;
                // What this run took and did not spend. Without this a host
                // that gives a thousand and calls something that runs ten is
                // told it has nothing left.
                if (rt->fuel_bounded) {
                    rt->fuel_left += slice;
                }
                return true;
            }
            frame = &rt->frames[rt->frame_count - 1];
            ip = frame->ip;
            mine = frame->base;
            constants = frame->chunk->constants;
            top = base + count;
            NEXT;
        }
#if KEST_THREADED
        // A byte no instruction is, which the switch would step over.
        thread_nothing:
            NEXT;
#endif
        }
    }

#undef SPEND
#undef READ_BYTE
#undef THREADED
#undef NEXT
#undef READ_U16
#undef BINARY_I
}

// A run, and what it leaves behind when it stops. Everything a `scratch { }`
// opened and did not close goes back where it was: a refusal, the fuel running
// out, a host saying no and a machine that was cancelled all stop a body where
// it stands, and a block put back only by the code that opened it is a block
// nothing puts back then. A diagnostic is written in the machine's own room
// rather than on this heap, so there is nothing here to take away with it.
// See D617 and D966.
static bool execute(KestRuntime *rt, int32_t entry, uint16_t arg_slots,
                    uint16_t *returned) {
    uint32_t held = rt->kept_count;
    // Where this run's frames begin and what the machine was holding for the
    // length of one instruction. A run that ends in a refusal comes back from
    // the middle of a body, so neither goes back on its own -- and a run a
    // host made from inside a call of its own has a run under it that carries
    // on afterwards. Frames left over from a run that failed are frames the
    // one underneath then returns through. See D1079.
    uint32_t began = rt->running_frames;
    uint32_t hands = rt->hands;
    rt->stopped_at = NULL;
    bool went = run_body(rt, entry, arg_slots, returned);
    // A machine a debugger stopped is not a machine that finished: what a
    // `scratch { }` opened is still open, because the body that opened it has
    // not got to the end of it. See D991.
    if (rt->stopped_at != NULL) {
        return went;
    }
    if (!went) {
        rt->frame_count = began;
        rt->hands = hands;
    }
    while (rt->kept_count > held) {
        rt->kept_count--;
        kest_arena_rewind(rt->heap, rt->kept[rt->kept_count]);
        kest_ground_close(rt->ground);
    }
    return went;
}

// The module is taken as something to write to rather than only to read,
// because one thing in it is: what the next place handed out in a store is
// stamped with belongs to the build, so two machines made from it are two
// worlds of one program rather than two programs counting from one.
KestRuntime *kest_runtime_new(KestArena *own, KestModule *stamped,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits,
                              const KestWalk *walked, bool untrusted) {
    // The machine's own arena, made by whoever asked for the machine: the
    // report is in it and the report is written before there is a machine to
    // hold it, so the two cannot be made in that order here. See D1071.
    if (own == NULL) {
        kest_diags_starve(diags);
        return NULL;
    }
    KestRuntime *rt = KEST_ARENA_NEW(own, KestRuntime);
    if (rt == NULL) {
        // No room for the machine itself, which is before there is anywhere to
        // write what happened: K0638 below is a host asking for more than
        // there is, and this is the host that asked for nothing and still
        // could not have it. The arena is the caller's and is given back
        // there.
        kest_diags_starve(diags);
        return NULL;
    }
    rt->own = own;
    const KestModule *module = stamped;
    rt->module = module;
    rt->diags = diags;
    rt->said_before = diags->count;
    rt->reported = diags->count;
    // What a walk of the whole program says, which the build works out once
    // and hands over: what it needs, where it calls into the host and which
    // function that is. A machine used to walk it here, in its own room and
    // handed back after, so a host that makes a machine a frame did the same
    // walk every frame — 1596 bytes of scratch for `examples/embed.kest`
    // against the 600 a machine is made of. The module does not change after
    // it is compiled, so neither does the answer. See D607.
    rt->host_measured = walked->measured;
    rt->host_slots = walked->host_slots;
    rt->host_frames = walked->host_frames;
    rt->host_where = walked->measured ? walked->why.where : NULL;
    uint32_t reached = walked->slots;
    uint32_t deep = walked->frames;

    // And what a host that says nothing gets, which is what the program asked
    // for: the worst any function needs, plus the worst call back in from
    // inside a host function — a machine does not know which function a host
    // will call, and a host that binds one may be called from inside it. A
    // program with no deepest call has no number to give, and then the usual
    // ones are what there is. Half a megabyte of stack for a program that
    // wants sixteen slots is what saying nothing used to cost. See D575.
    uint32_t wants_frames = MAX_FRAMES;
    if (rt->host_measured && reached + rt->host_slots > 0) {
        wants_frames = deep + rt->host_frames;
    }
    rt->call_depth = limits == NULL || limits->call_depth == 0
                         ? wants_frames
                         : limits->call_depth;

    uint32_t wants_slots = STACK_SLOTS;
#if KEST_CHECKED
    rt->had_least = rt->host_measured && reached + rt->host_slots > 0;
    // Room for the counts, taken only where somebody has asked to read them:
    // no room, no counting, and a machine nobody asked stays the size it was.
    // Nothing is said about a machine that could not have it — what fails is
    // the reading, and the reading was somebody's question rather than the
    // program's. See D870.
    if (getenv("KEST_DEEP") != NULL) {
        rt->ran_checked = KEST_ARENA_ARRAY(own, uint64_t, KEST_OP_RETURN + 1);
        rt->pairs_checked =
            KEST_ARENA_ARRAY(own, uint64_t,
                             ((size_t)KEST_OP_RETURN + 1) *
                                 ((size_t)KEST_OP_RETURN + 1));
    }
#endif
    if (rt->host_measured && reached + rt->host_slots > 0) {
        wants_slots = reached + rt->host_slots;
    } else if (walked->widest > 0) {
        // A program with no least still has a ceiling on frames, and a frame
        // is at most the widest body this program has. So the slots are that
        // many a frame rather than the usual number — which is what a program
        // that reaches itself used to be given whatever its shape, and what a
        // host that names a small depth was being charged sixty-five thousand
        // slots for. The usual number when even this is more, because that is
        // what it was before and no worse. The frames are settled first,
        // because the slots are worked out from them. See D815.
        uint64_t a_frame_each = (uint64_t)walked->widest * rt->call_depth;
        // And the same chain read the other way round: the frames that go
        // round cost the widest body that does, and the ones that do not can
        // each stand in the chain once, because twice would be a run of calls
        // coming back round through them. So the whole of those, once, plus a
        // turn's width for every frame. Neither bound is the other's: a
        // program whose loop is its widest body is smaller by the first, and
        // one whose loop is narrow and whose bodies are many is smaller by
        // the second. Both are true, so the smaller is. See D816.
        uint64_t a_turn_each =
            (uint64_t)walked->off_the_turns +
            (uint64_t)walked->in_a_turn * rt->call_depth;
        if (walked->in_a_turn > 0 && a_turn_each < a_frame_each) {
            a_frame_each = a_turn_each;
        }
        wants_slots = a_frame_each < (uint64_t)STACK_SLOTS
                          ? (uint32_t)a_frame_each
                          : STACK_SLOTS;
    }
    rt->stack_slots = limits == NULL || limits->stack_slots == 0
                          ? wants_slots
                          : limits->stack_slots;
    rt->stack = KEST_ARENA_ARRAY(own, KestValue, rt->stack_slots);
    rt->frames = KEST_ARENA_ARRAY(own, Frame, rt->call_depth);
    rt->natives =
        KEST_ARENA_ARRAY(own, KestNative, module->extern_count + 1);
    rt->contexts = KEST_ARENA_ARRAY(own, void *, module->extern_count + 1);
    rt->said_extern = KEST_ARENA_ARRAY(own, uint8_t, module->extern_count + 1);
    rt->said_copy = KEST_ARENA_ARRAY(own, uint8_t, module->count + 1);
    rt->said_layout = KEST_ARENA_ARRAY(own, uint8_t, module->layout_count + 1);
    rt->heap = kest_arena_new();
    rt->ground = kest_ground_new();
    rt->walk_at = WALK_FLOOR;
    rt->walk_after = 1;
    rt->walk_every = getenv("KEST_WALK_EVERY") != NULL;
    rt->heap_bytes = limits == NULL ? 0 : limits->heap_bytes;
    if (rt->heap != NULL) {
        kest_arena_cap(rt->heap, rt->heap_bytes);
    }
    // A machine with no budget is one whose counter never reaches nought.
    // Every bit set is five hundred years of instructions at one a nanosecond,
    // so the hot path tests the same word either way and a host that gave no
    // number pays one comparison rather than a branch on whether there is one.
    rt->fuel_given = limits == NULL ? KEST_FUEL_UNLIMITED : limits->fuel;
    rt->fuel_bounded = rt->fuel_given != KEST_FUEL_UNLIMITED;
    rt->fuel_left = rt->fuel_bounded ? rt->fuel_given : UINT64_MAX;
    atomic_init(&rt->cancel_asked, 0);
    if (rt->stack == NULL || rt->frames == NULL || rt->natives == NULL ||
        rt->contexts == NULL || rt->said_extern == NULL ||
        rt->said_copy == NULL || rt->said_layout == NULL || rt->heap == NULL) {
        // A host says how much stack and how deep the calls may go, and both
        // are taken before anything runs. Asking for more than the machine
        // this is on can give came back as nothing at all: a host with a
        // number too big for the machine and a host with a program that would
        // not compile got the same nothing, and only one of them is about the
        // program. Which of them could not be had is said, because a host that
        // halves the wrong number is a host halving it forever.
        KestSpan nowhere = {0, 0};
        kest_diags_in(diags, NULL);
        if (rt->stack == NULL) {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this host asked for %u slots of stack and this "
                           "machine cannot have that much",
                           rt->stack_slots);
        } else if (rt->frames == NULL) {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this host asked for calls %u deep and this machine "
                           "cannot have that many",
                           rt->call_depth);
        } else {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this machine has nowhere to put what a program "
                           "needs before it runs");
        }
        kest_diags_suggest(diags,
                           "`kest_needs` says what the program wants; a "
                           "number a host picks over that is a number this "
                           "machine has to be able to take");
        kest_ground_free(rt->ground);
        kest_arena_free(rt->heap);
        return NULL;
    }
    rt->limit = rt->stack + rt->stack_slots;
    // This machine's own, not the build's. It was the build's, so two machines
    // started from one build wrote the same counter from whatever threads they
    // were on -- a data race, and the one piece of mutable state a build had
    // that two runtimes shared. What made it safe to move is the world number
    // below: a place is told apart from a place in another machine by which
    // world it is in, so the count no longer has to be unique across them.
    // See D934 and D936.
    rt->stamps = 0;
    (void)stamped;
    // And what says this machine is standing on the build, counted where the
    // machines are rather than where the builds are: freeing the build while
    // one of these is up takes the program out from under it.
    rt->standing = &stamped->machines;

    // What the program declared against what the host provides, settled by
    // name and reported by name, before anything runs.
    bool unbound = false;
    rt->untrusted = untrusted;
    for (uint32_t i = 0; i < module->extern_count; i++) {
        rt->natives[i] =
            host == NULL ? NULL
                         : kest_host_find(host, module->externs[i].name,
                                          &rt->contexts[i]);
        if (rt->natives[i] == NULL) {
            kest_diags_in(diags, module->externs[i].source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0606",
                           module->externs[i].span,
                           "the host does not provide `%s`",
                           module->externs[i].name);
            unbound = true;
        } else if (untrusted &&
                   !kest_host_opened(host, module->externs[i].name)) {
            // Bound, and not for this: the host keeps doors for code it
            // wrote that code nobody trusts is not handed. See D1246.
            kest_diags_in(diags, module->externs[i].source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0663",
                           module->externs[i].span,
                           "`%s` is not a door the host opened to code "
                           "nobody trusts",
                           module->externs[i].name);
            kest_diags_suggest(diags, "open it with `kest_host_open`, or "
                                      "start the machine with `kest_start`");
            unbound = true;
        }
    }
    if (unbound) {
        // Both of them, because a machine that never started is a machine
        // nobody can free: what it took is the machine's own since D574, and
        // the last door out is the one that has to put it back.
        kest_ground_free(rt->ground);
        kest_arena_free(rt->heap);
        return NULL;
    }
    // Counted here rather than where machines are asked for, so that what
    // counts one up is beside what counts it down and a machine that was never
    // made was never counted.
    atomic_fetch_add_explicit(rt->standing, 1u, memory_order_relaxed);
    // What this machine says has been written in its own room since it was
    // asked for: `kest_start` makes the arena and the report together, because
    // the report is the first thing in it (D1071). What used to stand here was
    // a line moving the report out of the build's arena at this point, which
    // is what D574 and D617 needed when a machine's words began life in the
    // build's. There is nothing to move now — and a machine that never starts
    // still gives its words to the build, because `kest_diags_absorb` copies
    // them there and the arena goes back. See D1074.
    //
    // And what it keeps of what nobody asks for. A machine does not end, so
    // this is the one list in this project with a ceiling. See D618.
    diags->most = KEST_MOST_UNREAD;
    rt->after_said = kest_arena_mark(own);
    return rt;
}

// Whether the program is in the middle of running, which it is exactly when a
// function the host bound is on the stack. Anything that would take the heap
// or the machine out from under it is refused there. See D073.
static bool is_running(const KestRuntime *runtime) {
    return runtime != NULL && runtime->running_top != NULL;
}

// And whether a debugger has it stopped, which is the middle of a call with
// nothing of the host's on the machine's stack to show it. The question above
// answers no for one of these, and what the frames are standing on is the heap
// all the same. See D1078.
static bool is_stopped(const KestRuntime *runtime) {
    return runtime != NULL && runtime->stopped_at != NULL;
}

bool kest_runtime_free(KestRuntime *runtime) {
    if (runtime == NULL) {
        // Nothing to free is not a refusal: what a host asked for is that
        // there be no machine, and there is none.
        return true;
    }
    if (is_running(runtime)) {
        // The frames and the stack are the machine's own and the program is
        // standing on them. Saying so and doing nothing leaves the heap until
        // the build is freed, which is a leak rather than a read of what was
        // freed.
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the machine cannot be freed while the program is "
                       "running");
        kest_diags_suggest(runtime->diags,
                           "free it after the call it was made for returns");
        return false;
    }
#if KEST_CHECKED
    // What every body that ran actually reached, against what it asked for.
    // The build that checks itself is the only one that counts it, and it says
    // so only when asked, because a machine that wrote this every time would
    // be a machine whose answer differs from the one the release build gives.
    // Read by `tools/check-costs.sh`. See D812.
    if (runtime->module != NULL && getenv("KEST_DEEP") != NULL) {
        for (uint32_t i = 0; i < runtime->module->count; i++) {
            const KestChunk *one = runtime->module->functions[i];
            if (one != NULL && one->went > 0) {
                fprintf(stderr, "deep %s asked %u went %u fused %u\n",
                        one->name, one->stack_needed, one->went,
                        one->fused_slots);
            }
        }
    }
    if (getenv("KEST_DEEP") != NULL && runtime->went_slots > 0) {
        fprintf(stderr, "run %s asked %u slots %u frames went %u %u\n",
                runtime->had_least ? "least" : "bound", runtime->stack_slots,
                runtime->call_depth, runtime->went_slots,
                runtime->went_frames);
        // And what it ran on the way, the ones it ran at all and in the order
        // the machine holds them, because an order chosen here would be a
        // second thing to keep in step with the list. Whoever reads this can
        // sort it. See D870.
        fprintf(stderr, "guards %llu reentered %llu\n",
                (unsigned long long)runtime->guarded,
                (unsigned long long)runtime->reentered);
        // What the heap handed out, split by the width it was cut from and by
        // what the place holds, which a total cannot say. See D1032.
        {
            uint32_t widths[32];
            uint64_t taken[32];
            uint64_t kinds[4];
            uint32_t rungs = kest_ground_widths(runtime->ground, widths, taken,
                                                32, kinds);
            fprintf(stderr,
                    "took plain %llu array %llu elems %llu store %llu\n",
                    (unsigned long long)kinds[0], (unsigned long long)kinds[1],
                    (unsigned long long)kinds[2], (unsigned long long)kinds[3]);
            for (uint32_t rung = 0; rung < rungs; rung++) {
                if (taken[rung] > 0) {
                    fprintf(stderr, "width %u %llu\n", widths[rung],
                            (unsigned long long)taken[rung]);
                }
            }
        }
        // And the bytes it moved, beside the instructions it ran. Each of
        // these is memory copied from somewhere to somewhere; a stack pointer
        // stepped or a length read is not movement and is not here. See
        // D1023.
        fprintf(stderr,
                "moved loaded %llu stored %llu held %llu unpacked %llu "
                "packed %llu shifted %llu payload %llu text %llu "
                "shuffled %llu\n",
                (unsigned long long)runtime->moved_loaded,
                (unsigned long long)runtime->moved_stored,
                (unsigned long long)runtime->moved_held,
                (unsigned long long)runtime->moved_unpacked,
                (unsigned long long)runtime->moved_packed,
                (unsigned long long)runtime->moved_shifted,
                (unsigned long long)runtime->moved_payload,
                (unsigned long long)runtime->moved_text,
                (unsigned long long)runtime->moved_shuffled);
        for (uint32_t op = 0; runtime->ran_checked != NULL &&
                              op <= KEST_OP_RETURN;
             op++) {
            if (runtime->ran_checked[op] > 0) {
                fprintf(stderr, "ran %s %llu\n", kest_op_name((uint8_t)op),
                        (unsigned long long)runtime->ran_checked[op]);
            }
        }
        // And every pair that happened, so that whoever is reading this can
        // sort them: what is worth one instruction is a pair that happens
        // often, and which those are is a measurement rather than a guess.
        for (uint32_t first = 0;
             runtime->pairs_checked != NULL && first <= KEST_OP_RETURN;
             first++) {
            for (uint32_t then = 0; then <= KEST_OP_RETURN; then++) {
                uint64_t many =
                    runtime->pairs_checked[(size_t)first *
                                               (KEST_OP_RETURN + 1) +
                                           then];
                if (many > 0) {
                    fprintf(stderr, "pair %s %s %llu\n",
                            kest_op_name((uint8_t)first),
                            kest_op_name((uint8_t)then),
                            (unsigned long long)many);
                }
            }
        }
    }
#endif

    // Read before the arena this machine is in goes, because this struct is in
    // it: what is being freed here is the thing holding the pointers to what
    // is being freed.
    KestArena *own = runtime->own;
    kest_ground_free(runtime->ground);
    free(runtime->grey);
    free(runtime->held_by_host);
    free(runtime->handed);
    kest_arena_free(runtime->heap);
    // Released, because a build freed on another thread has to see everything
    // this machine did to the program before it counts itself off.
    atomic_fetch_sub_explicit(runtime->standing, 1u, memory_order_release);
    kest_arena_free(own);
    return true;
}

KestDiags *kest_runtime_said(KestRuntime *runtime) {
    return runtime->diags;
}

void kest_fuel_set(KestRuntime *runtime, uint64_t instructions) {
    // A machine that is not there has no budget, the same as every other door
    // here that answers rather than refuses.
    if (runtime == NULL) {
        return;
    }
    runtime->fuel_given = instructions;
    runtime->fuel_bounded = instructions != KEST_FUEL_UNLIMITED;
    runtime->fuel_left =
        runtime->fuel_bounded ? instructions : UINT64_MAX;
    // Giving fuel is what takes a cancel back. The two are one counter and a
    // flag, so a machine given fuel with the flag still set would run one
    // instruction and stop again saying somebody had asked it to.
    atomic_store_explicit(&runtime->cancel_asked, 0, memory_order_relaxed);
}

void kest_fuel_spend(KestRuntime *runtime, uint64_t work) {
    if (runtime == NULL || !runtime->fuel_bounded) {
        return;
    }
    // The same rate the machine charges itself at, so a host and a program are
    // spending one currency: a unit for the crossing, and one for every
    // sixty-four bytes or elements of what the door did. See D951.
    uint64_t owed = 1 + work / FUEL_PER_UNIT;
    runtime->fuel_left = runtime->fuel_left > owed ? runtime->fuel_left - owed
                                                  : 0;
}

uint64_t kest_fuel_left(const KestRuntime *runtime) {
    if (runtime == NULL) {
        return KEST_FUEL_UNLIMITED;
    }
    // A machine with no budget answers with what it is counting down from
    // rather than with nought, because nought is the answer for one that has
    // run out and those are opposite things.
    return runtime->fuel_bounded ? runtime->fuel_left : UINT64_MAX;
}

void kest_native_failed(KestRuntime *runtime, const char *why) {
    if (runtime == NULL) {
        return;
    }
    // Copied onto the machine's own arena rather than kept: the host's string
    // may be on its stack, and what reads this is the refusal after the call
    // has returned. A host with nothing to say still fails, and says so.
    const char *said = why == NULL ? "" : why;
    size_t length = strlen(said);
    char *kept = kest_arena_alloc(runtime->own, length + 1, 1);
    if (kept == NULL) {
        runtime->native_failed = "the host could not say why";
        return;
    }
    memcpy(kept, said, length + 1);
    runtime->native_failed = kept;
}

void kest_cancel(KestRuntime *runtime) {
    if (runtime == NULL) {
        return;
    }
    // The order matters and is the whole of what makes this safe from another
    // thread: the flag says why before the counter says stop, so a machine
    // that reads nought has already been told which of the two it is. Released
    // rather than relaxed for the same reason -- everything the host did
    // before asking is visible to the machine that stops.
    atomic_store_explicit(&runtime->cancel_asked, 1, memory_order_release);
    runtime->fuel_left = 0;
}

bool kest_cancelled(const KestRuntime *runtime) {
    if (runtime == NULL) {
        return false;
    }
    // Cast away the const to read it: an atomic load takes a pointer to the
    // object and this door promises not to change it.
    atomic_int *asked = (atomic_int *)(uintptr_t)&runtime->cancel_asked;
    return atomic_load_explicit(asked, memory_order_acquire) != 0;
}

void kest_allowed(const KestRuntime *runtime, KestLimits *limits) {
    if (runtime == NULL || limits == NULL) {
        return;
    }
    limits->stack_slots = runtime->stack_slots;
    limits->call_depth = runtime->call_depth;
    limits->heap_bytes = runtime->heap_bytes;
    limits->fuel = runtime->fuel_given;
}

size_t kest_runtime_cost(const KestRuntime *runtime) {
    return runtime == NULL ? 0 : kest_arena_used(runtime->own);
}

size_t kest_heap_wanted(const KestRuntime *runtime) {
    return runtime == NULL ? 0 : was_refused(runtime);
}

KestRefusal kest_heap_refused_by(const KestRuntime *runtime) {
    // Read beside the number rather than instead of it: what the two places a
    // program's memory comes from remember is the last refusal, and until
    // there has been one there is nothing to say about who made it.
    if (runtime == NULL || was_refused(runtime) == 0) {
        return KEST_REFUSED_NOTHING;
    }
    if (kest_ground_refused(runtime->ground) != 0) {
        return kest_ground_refused_by_ceiling(runtime->ground)
                   ? KEST_REFUSED_CEILING
                   : KEST_REFUSED_MACHINE;
    }
    return kest_arena_refused_by_ceiling(runtime->heap) ? KEST_REFUSED_CEILING
                                                        : KEST_REFUSED_MACHINE;
}

int64_t kest_stopped(const KestRuntime *runtime) {
    if (runtime == NULL || runtime->stopped_at == NULL ||
        runtime->frame_count == 0) {
        return -1;
    }
    const Frame *frame = &runtime->frames[runtime->frame_count - 1];
    return (int64_t)(runtime->stopped_at - frame->chunk->code);
}

int32_t kest_stopped_in(const KestRuntime *runtime) {
    if (runtime == NULL || runtime->stopped_at == NULL ||
        runtime->frame_count == 0) {
        return -1;
    }
    const KestChunk *chunk = runtime->frames[runtime->frame_count - 1].chunk;
    for (uint32_t i = 0; i < runtime->module->count; i++) {
        if (runtime->module->functions[i] == chunk) {
            return (int32_t)i;
        }
    }
    return -1;
}

bool kest_resume(KestRuntime *runtime, KestValue *frame, uint32_t room) {
    if (runtime == NULL || runtime->stopped_at == NULL) {
        return false;
    }
    uint16_t returned = 0;
    KestValue *floor = runtime->called_floor;
    if (!execute(runtime, -1, 0, &returned)) {
        return false;
    }
    // What came back, put where the call that stopped would have put it. A
    // resume is the rest of that call, so it answers the same way.
    if (frame != NULL && floor != NULL && returned > 0) {
        uint32_t many = returned < room ? returned : room;
        memcpy(frame, floor, sizeof(KestValue) * many);
    }
    return true;
}

uint8_t *kest_code_of(KestRuntime *runtime, int32_t entry, uint32_t *count) {
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        if (count != NULL) {
            *count = 0;
        }
        return NULL;
    }
    KestChunk *chunk = runtime->module->functions[entry];
    if (count != NULL) {
        *count = chunk->code_count;
    }
    return chunk->code;
}

int64_t kest_came_from(const KestRuntime *runtime, int32_t entry,
                       uint32_t at) {
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return -1;
    }
    // Through the one walk that knows where an instruction starts, which is
    // what D751 leaves behind: one origin an instruction and not one a byte,
    // so finding the one that covers an offset is a walk of the code.
    return (int64_t)kest_chunk_origin(runtime->module->functions[entry], at);
}

bool kest_frame_at_address(const KestRuntime *runtime, uint32_t deep,
                           uint16_t slot) {
    if (runtime == NULL || deep >= runtime->frame_count) {
        return false;
    }
    bool at_address = false;
    if (kest_chunk_named(runtime->frames[deep].chunk, slot, NULL, NULL,
                         &at_address) == NULL) {
        return false;
    }
    return at_address;
}

uint32_t kest_frames_deep(const KestRuntime *runtime) {
    return runtime == NULL ? 0 : runtime->frame_count;
}

int32_t kest_frame_in(const KestRuntime *runtime, uint32_t deep) {
    if (runtime == NULL || deep >= runtime->frame_count) {
        return -1;
    }
    const KestChunk *chunk = runtime->frames[deep].chunk;
    for (uint32_t i = 0; i < runtime->module->count; i++) {
        if (runtime->module->functions[i] == chunk) {
            return (int32_t)i;
        }
    }
    return -1;
}

int64_t kest_frame_ip(const KestRuntime *runtime, uint32_t deep) {
    if (runtime == NULL || deep >= runtime->frame_count) {
        return -1;
    }
    const Frame *frame = &runtime->frames[deep];
    return (int64_t)(frame->ip - frame->chunk->code);
}

bool kest_frame_slot(const KestRuntime *runtime, uint32_t deep, uint16_t slot,
                     KestValue *into) {
    if (runtime == NULL || into == NULL || deep >= runtime->frame_count) {
        return false;
    }
    const Frame *frame = &runtime->frames[deep];
    if (slot >= frame->chunk->slot_count) {
        return false;
    }
    *into = frame->base[slot];
    return true;
}

const char *kest_frame_name(const KestRuntime *runtime, uint32_t deep,
                            uint16_t slot, uint16_t *slots, uint8_t *kind) {
    if (runtime == NULL || deep >= runtime->frame_count) {
        return NULL;
    }
    return kest_chunk_named(runtime->frames[deep].chunk, slot, slots, kind,
                            NULL);
}

uint16_t kest_frame_wide(const KestRuntime *runtime, uint32_t deep) {
    if (runtime == NULL || deep >= runtime->frame_count) {
        return 0;
    }
    return runtime->frames[deep].chunk->slot_count;
}

bool kest_count(KestRuntime *runtime, bool on) {
    if (runtime == NULL) {
        return false;
    }
    if (!on) {
        runtime->entered = NULL;
        runtime->entered_room = 0;
        return true;
    }
    if (runtime->entered != NULL) {
        return true;
    }
    uint32_t bodies = runtime->module == NULL ? 0 : runtime->module->count;
    uint64_t *entered =
        KEST_ARENA_ARRAY(runtime->own, uint64_t, bodies == 0 ? 1 : bodies);
    if (entered == NULL) {
        return false;
    }
    runtime->entered = entered;
    runtime->entered_room = bodies;
    runtime->crossings = 0;
    // Every instruction is counted by the budget, which is a counter the
    // machine already keeps and already pays for when it has one. So a run
    // being counted is given the largest budget there is: nothing can spend
    // it, and what is gone from it at the end is what the machine ran.
    // Counting the instructions any other way is a test at the top of the
    // dispatch loop, which measured a third of the machine -- and a profiler
    // that makes a program a third slower is measuring a different program.
    // See D979.
    if (!runtime->fuel_bounded) {
        runtime->fuel_bounded = true;
        runtime->fuel_given = UINT64_MAX;
        runtime->fuel_left = UINT64_MAX;
    }
    return true;
}

bool kest_counted(const KestRuntime *runtime, KestCounted *into) {
    if (runtime == NULL || into == NULL || runtime->entered == NULL) {
        return false;
    }
    memset(into, 0, sizeof(*into));
    into->steps = runtime->fuel_given - runtime->fuel_left;
    for (uint32_t which = 0; which < runtime->entered_room; which++) {
        into->calls += runtime->entered[which];
    }
    into->crossings = runtime->crossings;
    // The budget a caller set, which is not the one counting borrowed: a run
    // given the largest there is was given none by anybody.
    into->fuel_given =
        runtime->fuel_given == UINT64_MAX ? 0 : runtime->fuel_given;
    into->fuel_left =
        runtime->fuel_given == UINT64_MAX ? 0 : runtime->fuel_left;
    into->heap = kest_heap_used(runtime);
    into->most = kest_heap_most(runtime);
    return true;
}

uint64_t kest_counted_entry(const KestRuntime *runtime, int32_t entry) {
    if (runtime == NULL || runtime->entered == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->entered_room) {
        return 0;
    }
    return runtime->entered[entry];
}

size_t kest_heap_used(const KestRuntime *runtime) {
    if (runtime == NULL) {
        return 0;
    }
    // Both, because a program's memory is in both: what lasts as long as the
    // machine is on the arena, and what the program makes stands on the
    // ground. A host that read one of them would be told a world of text
    // costs nothing.
    //
    // What is standing on the ground rather than what it asked the host for.
    // The difference is the shape of the places and the ones standing empty,
    // and neither of those is the program's -- the arena under it took blocks
    // of sixty-four kilobytes and charged nobody for the block. A plot nothing
    // is in goes back to the host, so this settles where a world settles.
    // See D996.
    return kest_arena_used(runtime->heap) + kest_ground_used(runtime->ground);
}

size_t kest_heap_taken(const KestRuntime *runtime) {
    if (runtime == NULL) {
        return 0;
    }
    return kest_arena_taken(runtime->heap) + kest_ground_taken(runtime->ground);
}

size_t kest_heap_most(const KestRuntime *runtime) {
    if (runtime == NULL) {
        return 0;
    }
    // What the machine wrote down while running, against what it is holding
    // now: a host asking between calls is asking after everything that took
    // memory without the machine running -- text it handed over, headers in
    // front of what it lent -- and the most it ever held is at least what it
    // is holding.
    size_t holding = kest_heap_used(runtime);
    return holding > runtime->most ? holding : runtime->most;
}

bool kest_heap_allow(KestRuntime *runtime, size_t bytes) {
    if (runtime == NULL) {
        return false;
    }
    if (is_running(runtime)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "how much heap this machine may have cannot be said "
                       "while the program is running");
        kest_diags_suggest(runtime->diags,
                           "say it between calls; what the program is holding "
                           "is on it");
        return false;
    }
    // And a machine stopped at a breakpoint, for the reason above said at a
    // stop: the program is standing on what it was promised, and a ceiling
    // moved while it stands there is a promise changed after it was made.
    // Nothing is taken away by this door -- a ceiling is a number -- so what it
    // would cost is the resumed call meeting a wall the call it is in the
    // middle of never had. See D1078.
    if (is_stopped(runtime)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "how much heap this machine may have cannot be said "
                       "while it is stopped at a breakpoint");
        kest_diags_suggest(runtime->diags,
                           "the program is standing on what it was promised; "
                           "say it after `kest_resume`");
        return false;
    }
    // Kept as well as told to the arena, because a heap thrown away is capped
    // again with this number and a reset that went back to the old one would
    // be a ceiling that moves when nobody moved it.
    runtime->heap_bytes = bytes;
    kest_arena_cap(runtime->heap, bytes);
    return true;
}

// What a host may not do to the heap while the program is standing on it, said
// once for the four doors that say it: what it is holding is on it.
static bool between_calls(KestRuntime *runtime, const char *doing) {
    if (is_running(runtime)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the heap cannot be %s while the program is running",
                       doing);
        kest_diags_suggest(runtime->diags,
                           "what it is holding is on it; do this between "
                           "calls rather than inside one");
        return false;
    }
    // And a machine a debugger stopped, which is neither of the two states
    // this door knew about. Nothing of the host's is on the stack, so the
    // question above answers no, and the program's frames are standing on the
    // heap all the same: a walk of a stopped machine reaches nothing, because
    // what it reads to is where the slots had got to when the host last called
    // in and that is the bottom of the stack. It gave the frames' memory back
    // and the machine read it on the way out of the breakpoint. A stop is in
    // the middle of a call, so every door that says between calls says it
    // here. See D1078.
    if (is_stopped(runtime)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the heap cannot be %s while this machine is stopped "
                       "at a breakpoint",
                       doing);
        kest_diags_suggest(runtime->diags,
                           "its frames are standing on it; let it carry on "
                           "with `kest_resume`, or free the machine");
        return false;
    }
    return true;
}

uint32_t kest_scratch_mark(KestRuntime *runtime) {
    if (runtime == NULL) {
        return 0;
    }
    if (!between_calls(runtime, "marked")) {
        return 0;
    }
    // Anything lent is a header on the heap and a place in a list beside it,
    // both of which a rewind would take. A host that ends its lends first has
    // nothing here to lose; one that does not is told rather than finding out
    // at the next read. See D957.
    if (runtime->lent_count > 0) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the heap cannot be marked while %u thing%s lent",
                       runtime->lent_count,
                       runtime->lent_count == 1 ? " is" : "s are");
        kest_diags_suggest(runtime->diags,
                           "end what is lent first: a lend is a header on the "
                           "heap and a place in a list beside it");
        return 0;
    }
    if (runtime->scratch_count == KEST_SCRATCH_DEEP) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "this machine holds %u marks at once and there is one "
                       "more",
                       (unsigned)KEST_SCRATCH_DEEP);
        kest_diags_suggest(runtime->diags,
                           "put the heap back to one of them before marking "
                           "again");
        return 0;
    }
    uint32_t at = runtime->scratch_count++;
    runtime->scratch[at] = kest_arena_mark(runtime->heap);
    runtime->scratch_given[at] = ++runtime->scratch_handed;
    return runtime->scratch_given[at];
}

bool kest_scratch_rewind(KestRuntime *runtime, uint32_t mark) {
    if (runtime == NULL || mark == 0) {
        return false;
    }
    if (!between_calls(runtime, "put back")) {
        return false;
    }
    if (runtime->lent_count > 0) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the heap cannot be put back while %u thing%s lent",
                       runtime->lent_count,
                       runtime->lent_count == 1 ? " is" : "s are");
        kest_diags_suggest(runtime->diags,
                           "end what is lent first: a lend is a header on the "
                           "heap and a place in a list beside it");
        return false;
    }
    for (uint32_t at = runtime->scratch_count; at > 0; at--) {
        if (runtime->scratch_given[at - 1] != mark) {
            continue;
        }
        kest_arena_rewind(runtime->heap, runtime->scratch[at - 1]);
        // And the ground with it. A mark is a place on the arena and the
        // ground has no places in that order -- what a program made is given
        // back when nothing can reach it, not when it was made -- so what a
        // rewind does here is the walk: between calls nothing of the
        // program's is running, so what is still wanted is what the host said
        // it keeps and what it was handed, and everything else goes. A host
        // that marks round a query gets the query's memory back, which is
        // what a mark was for. See D996.
        gather(runtime, NULL);
        // The ones above it go with it, which is what nesting is, and so does
        // what the machine itself keeps on the heap: the list of what is lent
        // and the headers it was saving for the next lend are both on it and
        // both may be above the mark.
        runtime->scratch_count = at - 1;
        runtime->spare_lends = NULL;
        runtime->lent = NULL;
        runtime->lent_count = 0;
        runtime->lent_capacity = 0;
        return true;
    }
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                   "this machine has no mark %u to put the heap back to",
                   mark);
    kest_diags_suggest(runtime->diags,
                       "a mark is answered by `kest_scratch_mark` and is used "
                       "once; a heap thrown away takes every one of them");
    return false;
}

bool kest_heap_reset(KestRuntime *runtime) {
    if (runtime == NULL) {
        return false;
    }
    if (!between_calls(runtime, "thrown away")) {
        return false;
    }
    // The same heap, emptied. It was a new one and a free of the old one,
    // which is a call to the host and back every time round a loop that
    // resets, and a host that resets is a host with a frame to fit into.
    kest_arena_reset(runtime->heap);
    // And everything standing on the ground with it, which is what a host
    // that throws the heap away is throwing away: `kest_still_holds` answers
    // false for every one of them afterwards, the same as it always did. The
    // same ground, emptied, for the reason the line above keeps the same
    // arena.
    kest_ground_empty(runtime->ground);
    runtime->walk_at = WALK_FLOOR;
    runtime->most = 0;
    runtime->held_count = 0;
    runtime->handed_count = 0;
    // Every one of those was on it, and so was the list of what is lent.
    runtime->spare_lends = NULL;
    runtime->lent = NULL;
    runtime->lent_count = 0;
    runtime->lent_capacity = 0;
    // And every mark, because a mark is a place on the heap that has gone.
    runtime->scratch_count = 0;
    runtime->kept_count = 0;
    return true;
}

void kest_report(KestRuntime *runtime, FILE *out, KestForm form) {
    if (runtime == NULL || out == NULL) {
        return;
    }
    uint32_t from = runtime->reported > runtime->said_before
                        ? runtime->reported
                        : runtime->said_before;
    bool starving = runtime->diags->starved && !runtime->starve_said;
    if (from >= runtime->diags->count && !starving) {
        return;
    }
    // A view of the tail rather than anything taken out, so the whole run is
    // still there afterwards.
    KestDiags tail = *runtime->diags;
    tail.items = runtime->diags->items + from;
    tail.count = runtime->diags->count - from;
    // The count belongs to what is being written and not to the run it came
    // from, because JSON says it out loud.
    tail.error_count = 0;
    for (uint32_t i = 0; i < tail.count; i++) {
        if (tail.items[i].severity == KEST_SEVERITY_ERROR) {
            tail.error_count++;
        }
    }
    tail.starved = starving;
    tail.error_count += starving ? 1 : 0;
    if (form == KEST_FORM_JSON) {
        kest_diags_render_json(&tail, out);
    } else {
        kest_diags_render(&tail, out);
    }
    runtime->starve_said = runtime->starve_said || starving;
    // And back where it stood. What a host has been told is the host's — it
    // is written wherever the host asked for it — and the room the words were
    // in is this machine's. A program refused every frame says the same
    // sentence every frame, and a machine that kept every one of them would
    // hold a frame's words for as long as it ran: 625 bytes a call, measured
    // on a program whose heap is full. What is handed back is exactly what
    // was said, because nothing else is written here between two readings.
    // See D617.
    runtime->diags->items = NULL;
    runtime->diags->count = 0;
    runtime->diags->capacity = 0;
    runtime->diags->error_count = 0;
    runtime->diags->not_said = 0;
    runtime->diags->held_back = false;
    runtime->reported = 0;
    runtime->said_before = 0;
    kest_arena_rewind(runtime->own, runtime->after_said);
}

// Why a name did not answer, when the program has heard of it. A name nothing
// knows is a question a host is allowed to ask and gets no answer beyond -1;
// these two are the ones where the program has the name and cannot hand over a
// function, and a host reading -1 would otherwise go looking for a typo.
static bool explain_entry(KestRuntime *runtime, const char *name) {
    const KestModule *module = runtime->module;
    for (uint32_t i = 0; i < module->extern_count; i++) {
        if (strcmp(module->externs[i].name, name) != 0) {
            continue;
        }
        if (runtime->said_extern[i] != 0) {
            return true;
        }
        runtime->said_extern[i] = 1;
        kest_diags_in(runtime->diags, module->externs[i].source);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0614",
                       module->externs[i].span,
                       "`%s` is a function the program asks the host for", name);
        kest_diags_suggest(runtime->diags,
                           "this one crosses the other way: the host binds it "
                           "and the program calls it");
        return true;
    }

    int32_t copies[4];
    uint32_t count = kest_module_copies(module, name, copies, 4);
    if (count < 2) {
        return false;
    }
    // The same again for a generic, under the first of the copies: the list
    // is what the name stands for, and the name stands for the same copies
    // however often it is asked for.
    if (runtime->said_copy[copies[0]] != 0) {
        return true;
    }
    runtime->said_copy[copies[0]] = 1;
    // The names themselves, all four of them, in the arena. This was a
    // hundred and ninety-two bytes and stopped where they ran out, so a host
    // asking about a generic — whose copies are compiled under names with
    // their types written into them — was given a list that ended mid-name
    // and said nothing about it.
    uint32_t shown = count < 4 ? count : 4;
    size_t room = 1;
    for (uint32_t i = 0; i < shown; i++) {
        room += strlen(module->functions[copies[i]]->name) + 5;
    }
    char *list = kest_arena_alloc(runtime->diags->arena, room, 1);
    if (list == NULL) {
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < shown; i++) {
        at += (size_t)snprintf(list + at, room - at, "%s`%s`",
                               at == 0 ? "" : ", ",
                               module->functions[copies[i]]->name);
    }
    // Two functions may share a name when they take different things, and a
    // generic is compiled once for each set of types it is used with. Both
    // are several functions under one name, and what a host does about it is
    // the same, so this does not guess which it was.
    //
    // Where, though, it can say now: a chunk carries the declaration it was
    // compiled from, so the refusal points at one of them and names the rest
    // of the places. Copies of a generic are all written in the one place, so
    // that is one place said once; two functions of a name are two, and the
    // difference is a thing a host writer can see rather than work out. This
    // said nothing about where anything was. See D613.
    const KestChunk *first = module->functions[copies[0]];
    kest_diags_in(runtime->diags, first->source);
    kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0615",
                   first->declared,
                   "`%s` is more than one function here: they take different "
                   "things",
                   name);
    for (uint32_t i = 1; i < shown; i++) {
        const KestChunk *other = module->functions[copies[i]];
        if (other->source == first->source &&
            other->declared.offset == first->declared.offset) {
            continue;
        }
        kest_diags_note(runtime->diags, other->source, other->declared,
                        "and one of them is written here");
    }
    kest_diags_suggest(runtime->diags, "ask for one of them: %s%s", list,
                       count > 4 ? ", and more" : "");
    return true;
}

// The `at`th function of a name in this module, or -1 past the last. An exact
// name is one function and there is no other; anything else is the copies, in
// the order they were compiled.
static int32_t nth_named(const KestModule *module, const char *name,
                         uint32_t at) {
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return at == 0 ? (int32_t)i : -1;
        }
    }
    // Counted rather than gathered. This filled sixty-four indexes and
    // answered -1 for the sixty-fifth, so a host walking the copies of a
    // generic stopped there and was told nothing: -1 is how the walk ends,
    // and a walk that ends early ends the same way one that ends says it
    // does. Nothing has to be held to find the one at a place.
    size_t length = strlen(name);
    uint32_t seen = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        const char *candidate = module->functions[i]->name;
        if (strncmp(candidate, name, length) != 0 || candidate[length] != '#') {
            continue;
        }
        if (seen == at) {
            return (int32_t)i;
        }
        seen++;
    }
    return -1;
}

int32_t kest_entry_of(KestRuntime *runtime, const char *name, uint32_t at) {
    if (runtime == NULL) {
        return -1;
    }
    int32_t found = nth_named(runtime->module, name, at);
    if (found >= 0) {
        return found;
    }
    const char *alias = runtime->module->alias;
    size_t prefix = strlen(alias);
    char qualified[256];
    if (prefix == 0 || prefix + strlen(name) + 2 > sizeof(qualified)) {
        return -1;
    }
    memcpy(qualified, alias, prefix);
    qualified[prefix] = '.';
    memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    return nth_named(runtime->module, qualified, at);
}

int32_t kest_entry(KestRuntime *runtime, const char *name) {
    if (runtime == NULL) {
        return -1;
    }
    // A host writes what the file writes, and the file registered its names
    // under itself; which of the two spellings it is is `kest_module_entry`'s
    // to know, and every part of this project asks it the same way.
    int32_t found = kest_module_entry(runtime->module, name);
    if (found >= 0) {
        return found;
    }
    const char *alias = runtime->module->alias;
    size_t prefix = alias == NULL ? 0 : strlen(alias);
    char qualified[256];
    bool composed = prefix != 0 && prefix + strlen(name) + 2 <= sizeof(qualified);
    if (composed) {
        memcpy(qualified, alias, prefix);
        qualified[prefix] = '.';
        memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    }
    if (!explain_entry(runtime, name) && composed) {
        explain_entry(runtime, qualified);
    }
    return -1;
}

// Declared here because the walk that says there is nothing at an index is the
// same walk for every one of these, and the one that has it is written below.
static const KestChunk *frame_of(KestRuntime *runtime, int32_t entry,
                                 const uint8_t *kinds, uint32_t count);

// Every question about a frame answers a host with a number or a pointer, and
// for each of them the answer that means nothing here is one a real frame can
// give: nought arguments, nought slots in, nothing past the last one, nothing
// given back. So an index that is no function reads as a function that takes
// and gives nothing, which is what `kest_frame_slots` says out loud and what
// these said in silence. `frame_of` is the one place that says it. See D436.
const char *kest_entry_name(KestRuntime *runtime, int32_t entry) {
    // Its own bounds rather than `frame_of`'s: that one says what went wrong,
    // and a walk of everything a program defines ends at the end. A machine
    // that complained there is what every host walking the list would be told
    // for reading it to the end. See D584 and D609.
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return NULL;
    }
    return runtime->module->functions[entry]->name;
}

const char *kest_entry_wrote(KestRuntime *runtime, int32_t entry) {
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return NULL;
    }
    return runtime->module->functions[entry]->wrote;
}

bool kest_entry_promises(KestRuntime *runtime, int32_t entry,
                         KestPromise which) {
    // False past the last function, the way the two above answer NULL: a host
    // walking to the end is reading the end rather than asking about a
    // function that is not there. A promise nothing made is not one to keep.
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return false;
    }
    // Written out rather than left to a `default`, so a promise added to the
    // language stops this compiling until somebody says what a host reads for
    // it. See D857.
    switch (which) {
    case KEST_PROMISE_NO_ALLOC:
        return runtime->module->functions[entry]->no_alloc;
    case KEST_PROMISE_NO_HOST:
        return runtime->module->functions[entry]->no_host;
    case KEST_PROMISE_DETERMINISTIC:
        return runtime->module->functions[entry]->deterministic;
    }
    return false;
}

uint32_t kest_frame_takes(KestRuntime *runtime, int32_t entry) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return 0;
    }
    return chunk->takes_count;
}

uint32_t kest_frame_at(KestRuntime *runtime, int32_t entry, uint32_t which) {
    if (frame_of(runtime, entry, NULL, 0) == NULL) {
        return 0;
    }
    const KestChunk *chunk = runtime->module->functions[entry];
    uint32_t at = 0;
    for (uint32_t i = 0; i < which && i < chunk->takes_count; i++) {
        // How wide the argument is, which is not how many pieces it has: a
        // piece of text is one piece and two slots. See D964.
        at += runtime->module->layouts[chunk->takes[i]].slots;
    }
    return at;
}

const KestLayout *kest_frame_gives(KestRuntime *runtime, int32_t entry) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return NULL;
    }
    // Nothing is what a function that gives nothing gives, and a layout for
    // it would be a shape for something that is not there.
    return chunk->returns_value ? &runtime->module->layouts[chunk->gives]
                                : NULL;
}

// Whether writing this value would read a piece of text that is not there.
// The same walk `format_value` makes, asked first: a frame nothing has been
// called with is noughts, and a nought where text goes is not an empty piece
// of text but the absence of one.
static bool missing_text(const KestType *type, const KestValue *slots) {
    switch (type->tag) {
    case KEST_T_TEXT:
        return slots[0].text == NULL;
    case KEST_T_ENUM: {
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return false;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (missing_text(variant->payload[p], slots + variant->offsets[p])) {
                return true;
            }
        }
        return false;
    }
    // The two that are written since D876 and so have to be asked about since
    // D876: a host hands a struct in and a field of it may be a text nobody
    // wrote, and writing that is a read through nothing.
    case KEST_T_STRUCT:
        for (uint32_t m = 0; m < type->member_count; m++) {
            if (missing_text(type->members[m].type,
                             slots + type->members[m].offset)) {
                return true;
            }
        }
        return false;
    case KEST_T_FIXED: {
        uint16_t stride = type->element->slots == 0 ? 1 : type->element->slots;
        for (uint32_t i = 0; i < type->count; i++) {
            if (missing_text(type->element, slots + (size_t)i * stride)) {
                return true;
            }
        }
        return false;
    }
    case KEST_T_OPTIONAL:
        if (slots[type->element->slots].integer == 0) {
            return false;
        }
        return missing_text(type->element, slots);
    // Everything else either holds no text or is a shape this never writes,
    // and both are written out rather than left to a `default` for the reason
    // `format_value` gives beside the same list.
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_FLAGS:
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_ARRAY:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_PARAM:
    case KEST_T_MODULE:
        return false;
    }
    return false;
}

int64_t kest_gave_text(KestRuntime *runtime, int32_t entry,
                       const KestValue *frame, char *out, size_t room) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return -1;
    }
    KestSpan nothing = {0, 0};
    const char *called = chunk->wrote;
    // Minus one used to be all three of these, said in silence: a host got a
    // number that means no and a report that said nothing, and could not tell
    // an index that is no function from a function with nothing to say.
    if (!chunk->returns_value) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives nothing back, so there is nothing to write",
                       called);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_gives` is NULL for a function that "
                           "gives nothing back, which is what to ask first");
        return -1;
    }
    const KestType *type = runtime->module->layout_types[chunk->gives];
    // `kest_type_has_text` says which type it was that has none, and this is
    // the somewhere it wanted to be said.
    const KestType *without = NULL;
    if (type == NULL || !kest_type_has_text(type, &without)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives back `%s`, which has no text of its own",
                       called,
                       kest_type_name(runtime->diags->arena,
                                      without != NULL ? without : type));
        kest_diags_suggest(runtime->diags,
                           "walk it with `kest_frame_gives` and write what is "
                           "there: what a store or a reference means is what "
                           "is behind it, which is the host's to decide");
        return -1;
    }

    // Nothing in the slot is a frame that has not been called with, which is
    // a host asking what came back before anything came back. Reading it as
    // text would be reading whatever the frame was made with, and a host that
    // made one out of nothing has a nought there.
    //
    // A frame that is not there at all is the same news, and it is what a host
    // that could not make one hands over. `kest_call` and `kest_takes_text`
    // both refuse it; this one read slot zero. See D511.
    if (frame == NULL || missing_text(type, frame)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0632", nowhere,
                       "nothing is in the frame to say, so nothing was called "
                       "with it");
        kest_diags_suggest(runtime->diags,
                           "call it with `kest_call` first; this says what is "
                           "there rather than putting something there");
        return -1;
    }

    // Text on its own is what it holds rather than the source that spells it,
    // which is the exception D035 names: a hole holding one writes the
    // content, and this is the same question asked from outside.
    size_t needed = type->tag == KEST_T_TEXT
                        ? (size_t)frame[1].integer
                        : kest_write_value(NULL, 0, type, frame);
    if (out != NULL && room > 0) {
        size_t fits = needed < room - 1 ? needed : room - 1;
        if (type->tag == KEST_T_TEXT) {
            memcpy(out, frame[0].text, fits);
        } else {
            kest_write_value(out, fits, type, frame);
        }
        out[fits] = '\0';
    }
    return (int64_t)needed;
}

const KestLayout *kest_frame_layout(KestRuntime *runtime, int32_t entry,
                                    uint32_t which) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return NULL;
    }
    // Past the last one is the walk ending, which is not the same news and is
    // said by nobody: a host walks the arguments until this answers nothing.
    if (which >= chunk->takes_count) {
        return NULL;
    }
    return &runtime->module->layouts[chunk->takes[which]];
}

// What a host says about a run of slots, against what the program says they
// are. Filling a frame and reading one back are the same disagreement in the
// two directions, so they are the same walk: the layouts are the arguments in
// one and what comes back in the other, and the words are what differ.
static bool frame_agrees(KestRuntime *runtime, const KestChunk *chunk,
                         const uint16_t *which, uint32_t layouts,
                         const uint8_t *kinds, uint32_t count,
                         const char *said, const char *ask) {
    KestSpan nowhere = {0, 0};
    const char *name = chunk->wrote;

    // How many slots there are before what is in them: a host that said too
    // few has not checked the rest, and telling it about the first slot it did
    // say would send it looking at the wrong end of its own frame.
    uint32_t slots = 0;
    for (uint32_t i = 0; i < layouts; i++) {
        slots += runtime->module->layouts[which[i]].count;
    }
    if (count != slots) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634", nowhere,
                       "`%s` %s %u slot%s and this host says what %u of them "
                       "hold",
                       name, said, slots, slots == 1 ? "" : "s", count);
        // The door that gives the number this was judged by, which is not
        // `kest_frame_slots`: that one answers how wide a frame has to be,
        // which is the wider of what a function takes and what it gives, and
        // a host judged against one of the two and sent to the larger reads
        // the number it just used. `hoard` takes nothing and gives one slot,
        // so a host that said one was told to go and ask a door that says
        // one. See D832.
        kest_diags_suggest(runtime->diags, "%s", ask);
        return false;
    }

    uint32_t at = 0;
    for (uint32_t i = 0; i < layouts; i++) {
        const KestLayout *layout = &runtime->module->layouts[which[i]];
        for (uint16_t p = 0; p < layout->count; p++) {
            if (kinds[at] != layout->pieces[p].kind) {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634",
                               nowhere,
                               "`%s` %s `%s` in slot %u and this host says "
                               "`%s`",
                               name, said,
                               kest_scalar_name(layout->pieces[p].kind), at,
                               kest_scalar_name(kinds[at]));
                // The words are the caller's and the numbers are none, which
                // is the one shape a message can have that says nothing about
                // what to put where.
                kest_diags_suggest(runtime->diags, "%s", ask);
                return false;
            }
            at++;
        }
    }
    return true;
}

// Whether a host may be asked about this at all, and which function it is.
static const KestChunk *frame_of(KestRuntime *runtime, int32_t entry,
                                 const uint8_t *kinds, uint32_t count) {
    if (runtime == NULL) {
        return NULL;
    }
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    // A host that says how many slots it is about to describe and hands
    // nothing to read them from. This was the sentence below, which is about
    // an index and says nothing about what is wrong here: the index may be a
    // function and often is. It is said every time, because it is about this
    // call rather than about the program. See D615.
    if (kinds == NULL && count > 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634", nowhere,
                       "this host says what %u slot%s hold and handed nothing "
                       "to read them from",
                       count, count == 1 ? "" : "s");
        kest_diags_suggest(runtime->diags,
                           "what a frame holds is a kind a slot, and no slots "
                           "is nought of them");
        return NULL;
    }
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        // How many there are, the way the walk of what a program asks the
        // host for says it (`K0648`): two walks past the end, and one of them
        // said how far the list went and the other left a host to find out.
        // A host walking what a program defines reads the same number. See
        // D614.
        //
        // Once, like the other statements about a program a host can ask for
        // twice: how many functions there are does not change while a machine
        // runs, and saying it again cost 648 bytes an asking to a host that
        // asked this instead of `kest_entry_name`, which answers the same
        // question for nothing and says nothing. See D608 and D615.
        if (!runtime->said_no_frame) {
            runtime->said_no_frame = true;
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634",
                           nowhere,
                           "this program defines %u function%s and there is "
                           "nothing at %d to say what a frame holds",
                           runtime->module->count,
                           runtime->module->count == 1 ? "" : "s", entry);
            kest_diags_suggest(runtime->diags,
                               "`kest_entry_name` is NULL for an index that is "
                               "no function, and says nothing about it");
        }
        return NULL;
    }
    return runtime->module->functions[entry];
}

bool kest_frame_fills(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count) {
    const KestChunk *chunk = frame_of(runtime, entry, kinds, count);
    if (chunk == NULL) {
        return false;
    }
    return frame_agrees(runtime, chunk, chunk->takes, chunk->takes_count,
                        kinds, count, "takes",
                        "`kest_frame_layout` says what each argument is made "
                        "of, a piece a slot");
}

bool kest_frame_reads(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count) {
    const KestChunk *chunk = frame_of(runtime, entry, kinds, count);
    if (chunk == NULL) {
        return false;
    }
    // A function that gives nothing back has nothing to read, and a host that
    // says a slot is read out of it is wrong about that rather than about a
    // kind.
    uint16_t gives = chunk->gives;
    return frame_agrees(runtime, chunk, &gives, chunk->returns_value ? 1 : 0,
                        kinds, count, "gives back",
                        "`kest_frame_gives` says what comes back over the "
                        "frame, a piece a slot");
}

bool kest_takes_text(KestRuntime *runtime, int32_t entry, KestValue *frame,
                     uint32_t slots, const char *const *words,
                     uint32_t count) {
    KestSpan nowhere = {0, 0};
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return false;
    }
    const char *name = chunk->wrote;
    if (count != chunk->takes_count || (words == NULL && count > 0)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635", nowhere,
                       "`%s` takes %u argument%s and this host handed over %u",
                       name, chunk->takes_count,
                       chunk->takes_count == 1 ? "" : "s", count);
        kest_diags_suggest(runtime->diags,
                           "one word an argument, and not one a slot: a "
                           "`Vec3` is three slots and no word at all");
        return false;
    }
    // The same width `kest_call` wants, refused before anything is written
    // rather than after: a frame too narrow would be written past here and
    // read past there.
    if (slots < chunk->param_slots || (frame == NULL && slots > 0)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635", nowhere,
                       "`%s` takes %u slot%s and this frame holds %u", name,
                       chunk->param_slots, chunk->param_slots == 1 ? "" : "s",
                       slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }

    uint32_t at = 0;
    for (uint32_t i = 0; i < count; i++) {
        const KestLayout *layout =
            &runtime->module->layouts[chunk->takes[i]];
        const KestType *type = layout->type;
        const char *why = NULL;
        // Text is the one of them a host cannot hand over by pointing at its
        // own bytes: what a program holds it must own, so it is copied the
        // way anything else a host hands over is copied.
        if (type != NULL && type->tag == KEST_T_TEXT) {
            if (!kest_text(runtime, words[i], (uint32_t)strlen(words[i]),
                           &frame[at])) {
                return false;
            }
        } else if (!kest_value_read(runtime->diags->arena, words[i], type,
                                    &frame[at], &why)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635",
                           nowhere, "`%s` %s, and `%s` takes it", words[i],
                           why, name);
            kest_diags_suggest(runtime->diags,
                               "a word is read as the type the declaration "
                               "says, the way the language writes one");
            return false;
        }
        at += layout->slots;
    }
    return true;
}

KestKept kest_kept_where(const KestRuntime *runtime, KestValue kept) {
    if (runtime == NULL || kept.object == NULL) {
        return KEST_KEPT_NOWHERE;
    }
    // The two places a value the host was handed can live, which are the two
    // a call in asks about: what a program made while running, and what the
    // file it came from wrote. Text and handles are the same pointer here —
    // what is being asked about is the memory and not what is written in it.
    if (ours(runtime, kept.object)) {
        // A lend is a header of the machine's in front of a block that is not,
        // and a host asking about one is asking about the block. Found by
        // address in the list of what is lent rather than by reading what is
        // at the address: what is at an address the host handed over is
        // whatever the host handed over, and a piece of text near the end of
        // a block is not four bytes long because a header is.
        for (uint32_t at = 0; at < runtime->lent_count; at++) {
            if ((const void *)runtime->lent[at] == kept.object) {
                return KEST_KEPT_LENT;
            }
        }
        return KEST_KEPT_HEAP;
    }
    if (kest_arena_holds(runtime->module->arena, kept.object)) {
        return KEST_KEPT_PROGRAM;
    }
    return KEST_KEPT_NOWHERE;
}

bool kest_still_holds(const KestRuntime *runtime, KestValue kept) {
    // Read out of the answer above rather than asked again, because two
    // readings of one thing are two things to keep in step.
    return kest_kept_where(runtime, kept) != KEST_KEPT_NOWHERE;
}

uint32_t kest_array_length(const KestRuntime *runtime, KestValue array,
                           uint32_t *room) {
    if (room != NULL) {
        *room = 0;
    }
    // A handle is a pointer and nothing in it says whose: the same question
    // every other door asks of one, which is whether this machine's heap
    // handed it out. A number a host wrote into a slot is not an array however
    // much it looks like one. See D630.
    if (runtime == NULL || array.object == NULL ||
        !ours(runtime, array.object)) {
        return 0;
    }
    const Array *held = array.object;
    if (held->what != KEST_IS_ARRAY) {
        return 0;
    }
    if (room != NULL) {
        *room = held->capacity;
    }
    return held->length;
}

bool kest_keeps(KestRuntime *runtime, KestValue kept) {
    if (runtime == NULL) {
        return false;
    }
    if (!kest_ground_holds(runtime->ground, kept.object)) {
        // Not the machine's to keep: a pointer of the host's own, or one from
        // a heap that has gone. Saying so is not an error — a host that says
        // this about everything it holds is a host that is right about all of
        // it — and there is nothing to remember.
        return true;
    }
    for (uint32_t i = 0; i < runtime->held_count; i++) {
        if (runtime->held_by_host[i].object == kept.object) {
            return true;
        }
    }
    if (runtime->held_count == runtime->held_room) {
        uint32_t bigger = runtime->held_room == 0 ? 8 : runtime->held_room * 2;
        KestValue *grown = realloc(runtime->held_by_host,
                                   (size_t)bigger * sizeof(KestValue));
        if (grown == NULL) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(runtime->diags, NULL);
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0605",
                           nowhere, "out of memory");
            return false;
        }
        runtime->held_by_host = grown;
        runtime->held_room = bigger;
    }
    runtime->held_by_host[runtime->held_count++] = kept;
    return true;
}

bool kest_lets_go(KestRuntime *runtime, KestValue kept) {
    if (runtime == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < runtime->held_count; i++) {
        if (runtime->held_by_host[i].object == kept.object) {
            runtime->held_by_host[i] =
                runtime->held_by_host[runtime->held_count - 1];
            runtime->held_count--;
            return true;
        }
    }
    return false;
}

bool kest_lend_ends(KestRuntime *runtime, KestValue lent) {
    if (runtime == NULL) {
        return false;
    }
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    // The same question a call in asks, for the same reason: what is at an
    // address the machine never handed out is whatever is there.
    if (!ours(runtime, lent.object) ||
        !KEST_HANDLE_IS(lent.object, KEST_IS_ARRAY)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0637", nowhere,
                       "this is not a lend this machine gave out");
        kest_diags_suggest(runtime->diags,
                           "`kest_borrow` answers what to hand back here, and "
                           "a machine takes back only what it lent");
        return false;
    }
    Array *array = lent.object;
    if (!array->borrowed) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0637", nowhere,
                       "this array is the program's own and not a lend");
        kest_diags_suggest(runtime->diags,
                           "what the program made is the program's for as "
                           "long as it holds it; only a host's own block is "
                           "taken back");
        return false;
    }
    // The header stays where it is and says what happened to it. Freeing it
    // would put the program back to reading whatever the heap hands out next,
    // which is the whole thing this is for.
    // Every handle over that block and not only the one handed over. A host
    // lending the same memory twice has two handles and one block, and what it
    // takes back is the block: a handle left alive over memory the host has
    // moved on from is the thing ending a lend exists to prevent.
    // What was lent is a run of bytes rather than an address: a host lending
    // the same block twice has two handles over one run, and one lending the
    // tail of a block has two runs that share their ends. Either way the
    // memory is the host's and it is taking it back, so what goes is every
    // handle over any of it.
    const unsigned char *block = array->bytes;
    const unsigned char *block_end =
        block + (size_t)array->length * array->stride;
    uint32_t at = 0;
    while (at < runtime->lent_count) {
        Array *one = runtime->lent[at];
        const unsigned char *from = one->bytes;
        const unsigned char *to = from + (size_t)one->length * one->stride;
        if (one != array && (to <= block || from >= block_end)) {
            at++;
            continue;
        }
        one->what = KEST_WAS_LENT;
        one->length = 0;
        one->capacity = 0;
        one->bytes = (unsigned char *)(void *)runtime->spare_lends;
        runtime->spare_lends = one;
        runtime->lent[at] = runtime->lent[--runtime->lent_count];
    }
    return true;
}

uint32_t kest_frame_slots(KestRuntime *runtime, int32_t entry) {
    if (runtime == NULL) {
        return 0;
    }
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        // Zero is also the honest width of a function that takes nothing and
        // gives nothing, so the number cannot say which of the two this is and
        // the report does.
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0616", nowhere,
                       "there is nothing at %d to ask the width of", entry);
        kest_diags_suggest(runtime->diags,
                           "`kest_entry` gives -1 for a name the program does "
                           "not define, and this answers 0 for it as it does "
                           "for a function that takes and gives nothing");
        return 0;
    }
    const KestChunk *chunk = runtime->module->functions[entry];
    // The arguments and the result are the same slots, so a frame has to be
    // wide enough for whichever is wider.
    return chunk->param_slots > chunk->result_slots ? chunk->param_slots
                                                    : chunk->result_slots;
}

bool kest_call(KestRuntime *runtime, int32_t entry, KestValue *frame,
               uint32_t slots) {
    if (runtime == NULL) {
        return false;
    }
#if KEST_CHECKED
    // A call in that arrives while one is already running, which is what a
    // host does when a bound function calls back into the program. It is the
    // one thing at this boundary nothing counted: the crossings the machine
    // keeps are the ones going out. Counted where the rest of what a run did
    // is counted, and for the reason D979 gives -- a release build pays for
    // nothing nobody reads. See D1032.
    if (runtime->running_top != NULL) {
        runtime->reentered++;
    }
#endif
    KestSpan nowhere = {0, 0};
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0607", nowhere,
                       "there is nothing at %d to call", entry);
        kest_diags_suggest(runtime->diags,
                           "`kest_entry` gives -1 for a name the program does "
                           "not define");
        return false;
    }
    // No frame is a frame of no slots, and the checks below are what makes
    // that true: a function that takes anything is refused, so nothing further
    // down has a null to guard against. It used to be permission to skip them,
    // and a call with no frame ran on whatever the stack floor was still
    // holding and said it had worked.
    if (frame == NULL) {
        slots = 0;
    }

    int32_t index = entry;
    const KestChunk *chunk = runtime->module->functions[index];
    const char *name = chunk->wrote;
    // What the program takes is not something a host can be trusted about:
    // the arguments go into the frame and the result comes back over them, so
    // a frame that is too narrow is read past on the way in and written past
    // on the way out.
    if (slots < chunk->param_slots) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "`%s` takes %u slot%s and this frame holds %u", name,
                       chunk->param_slots, chunk->param_slots == 1 ? "" : "s",
                       slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }
    // And what comes back is known before anything runs: a `return` never
    // gives back more than the declaration says, which `kest_module_prove`
    // holds the emitted code to. Refusing here rather than afterwards is a
    // program that has not done whatever it does and had its answer thrown
    // away for a frame it could have been told about first.
    if (slots < chunk->result_slots) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "`%s` gives %u slot%s back and this frame holds %u",
                       name, chunk->result_slots,
                       chunk->result_slots == 1 ? "" : "s", slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }

    // And what the host is handing over, where what it hands over is text, a
    // handle or a tag. A handle is a pointer and the machine reads four bytes
    // at the front of it to know what it is; any four bytes can be those four.
    // What cannot be faked is having come out of this machine's heap, which is
    // what a walk of the blocks answers — once per call rather than once per
    // instruction, at the crossing where a pointer from outside can arrive at
    // all.
    //
    // Whether an argument holds any of those is a walk of its pieces rather
    // than of its type: a word is text or a handle, a tag is a tag, and every
    // other kind is a number in a slot, which is whatever the host put there.
    // Most arguments are numbers, and they cost the walk that says so. See
    // D718.
    uint32_t at = 0;
    for (uint32_t which = 0; which < chunk->takes_count; which++) {
        const KestLayout *layout =
            &runtime->module->layouts[chunk->takes[which]];
        // Read off the layout rather than off its pieces: what has to be
        // walked is a thing about the type, and the type does not change
        // between calls. See D840.
        if (!layout->by_the_type) {
            // Nothing in it a walk would read, and one thing a look will: a
            // slot holds sixty-four bits and a `u8` holds eight, so a host
            // writing 300 into one is the one way a value this language
            // cannot make gets into a program. Read off the piece rather than
            // out of the type, because the type is what the walk above costs
            // and this is the case it was skipping. See D836.
            // A piece is not a slot: a piece of text is one piece and two,
            // so the walk counts the slots as it goes. See D964.
            uint16_t slot = 0;
            for (uint16_t p = 0; p < layout->count; p++) {
                if (!fits_the_piece(layout->pieces[p].kind,
                                    frame[at + slot])) {
                    narrower_than_that(runtime, name, at + slot,
                                       layout->pieces[p].kind,
                                       frame[at + slot]);
                    return false;
                }
                slot = (uint16_t)(slot +
                          (layout->pieces[p].kind == KEST_L_TEXT ? 2 : 1));
            }
            at += layout->slots;
            continue;
        }
        Saying door = {false, NULL, NULL, 0};
        if (!handed_well(runtime, &door, name, layout->type, frame, &at)) {
            return false;
        }
    }

    // The arguments go where the callee's slots are, which is where its result
    // will be, which is where the caller's frame already holds them.
    KestValue *floor = runtime->running_top != NULL ? runtime->running_top
                                                    : runtime->stack;
    // And whether there is anywhere to put them, asked before a byte moves.
    // `execute` asks the same thing and used to be the only one that did, so a
    // call wider than the stack copied its arguments past the end and then
    // said there was no room -- a refusal handed back after the memory it was
    // protecting had already been written over. The arithmetic is the same as
    // the one below it, and it is here because a check after a write is not a
    // check. See D928.
    if (runtime->running_frames >= runtime->call_depth ||
        floor + chunk->slot_count + chunk->stack_needed > runtime->limit) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0602", nowhere,
                       "there is no room to call in from here");
        what_it_needed(runtime, runtime, index);
        return false;
    }
    if (chunk->param_slots > 0) {
        memcpy(floor, frame, sizeof(KestValue) * chunk->param_slots);
    }

    uint16_t returned = 0;
    runtime->called_floor = floor;
    if (!execute(runtime, index, chunk->param_slots, &returned)) {
        return false;
    }
    if (returned > 0) {
        memcpy(frame, floor, sizeof(KestValue) * returned);
    }
    return true;
}

bool kest_native_at(KestRuntime *runtime, uint32_t index, const char *symbol,
                    KestNativeBody body) {
    if (runtime == NULL || runtime->module == NULL || body == NULL ||
        symbol == NULL || index >= runtime->module->count) {
        return false;
    }
    KestChunk *chunk = runtime->module->functions[index];
    // The whole name rather than the one somebody wrote: two copies of a
    // generic are two bodies and are told apart by what follows the `#`.
    if (chunk->name == NULL || strcmp(chunk->name, symbol) != 0 ||
        chunk->native != NULL) {
        return false;
    }
    chunk->native = body;
    return true;
}

// What a body written in C says when it stops, with the numbers in it. The
// code and the words are one call for the reason every other refusal here is
// written that way: what reads a refusal reads the words beside the code they
// belong to, and a check holds the two together.
static bool stopped_saying(KestRuntime *rt, uint32_t where, const char *code,
                           const char *format, ...) KEST_SAYS(4, 5);

static bool stopped_saying(KestRuntime *rt, uint32_t where, const char *code,
                           const char *format, ...) {
    char said[160];
    va_list args;
    va_start(args, format);
    vsnprintf(said, sizeof said, format, args);
    va_end(args);
    return kest_native_stopped(rt, where, code, said);
}

// What says a handle is the thing it is used as, written once for both of the
// two there are: the tag at the front of a header, and the one other thing it
// can be that a program is told about by name. It is the door's half of what
// `HOLD` does for an instruction.
static void *the_handle(KestRuntime *rt, KestValue handle, uint32_t what,
                        const char *called, uint32_t where) {
    void *at = handle.object;
    if (KEST_HANDLE_IS(at, what)) {
        return at;
    }
    if (KEST_HANDLE_IS(at, KEST_WAS_LENT)) {
        kest_native_stopped(rt, where, "K0637",
                            "the host has taken this lend back");
        return NULL;
    }
    stopped_saying(rt, where, "K0612", "this is not %s", called);
    return NULL;
}

unsigned char *kest_elem_at(KestRuntime *runtime, KestValue handle,
                            int64_t index, uint16_t offset, uint32_t where) {
    Array *array = runtime == NULL ? NULL : the_handle(runtime, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return NULL;
    }
    if (index < 0 || (uint64_t)index >= array->length) {
        // The machine's own sentence, because it is the machine's own
        // refusal: two engines that say a bounds failure differently are two
        // languages, and the check that runs both reads the words.
        stopped_saying(runtime, where, "K0604",
                       "index %lld is outside an array of length %u",
                       (long long)index, array->length);
        return NULL;
    }
    return array->bytes + (size_t)index * array->stride + offset;
}

bool kest_elem_count(KestRuntime *runtime, KestValue handle, uint32_t where,
                     int64_t *into) {
    Array *array = runtime == NULL ? NULL : the_handle(runtime, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL || into == NULL) {
        return false;
    }
    *into = array->length;
    return true;
}

bool kest_ledger(KestRuntime *rt, KestLedger *into) {
    if (rt == NULL || rt->module == NULL || into == NULL) {
        return false;
    }
    into->calls = (KestCall *)(void *)rt->frames;
    into->many = &rt->frame_count;
    into->most = rt->call_depth;
    into->limit = rt->limit;
    into->reached = &rt->running_top;
    into->chunks = (const void *const *)rt->module->functions;
    return true;
}

bool kest_native_crowded(KestRuntime *rt, uint32_t which, uint32_t where,
                         bool deep) {
    if (rt == NULL || rt->module == NULL || which >= rt->module->count) {
        return false;
    }
    if (deep) {
        stopped_saying(rt, where, "K0602", "calls nest more than %u deep",
                       rt->call_depth);
    } else {
        stopped_saying(rt, where, "K0602",
                       "this call wants more than the %u slots of stack there "
                       "are",
                       rt->stack_slots);
    }
    // And what to ask for, which is the same answer the machine gives where
    // it refuses the same call: a refusal about a number a host picked is one
    // a host can act on.
    what_it_needed(rt, rt, (int32_t)which);
    return false;
}

// Entering a body the host's compiler compiled from another one, for whoever
// cannot write the sequence out: a call through a function value, and a shim
// handing a body to the machine. A call from one compiled body to another
// writes it out rather than calling this, which is what D1122 is about.
static bool kest_native_room(KestRuntime *runtime, KestValue *base,
                             uint32_t which, uint32_t where, uint32_t *was) {
    KestLedger led;
    if (runtime == NULL || runtime->module == NULL || was == NULL ||
        which >= runtime->module->count || !kest_ledger(runtime, &led)) {
        return false;
    }
    const KestChunk *callee = runtime->module->functions[which];
    uint32_t needs =
        (uint32_t)callee->slot_count + (uint32_t)callee->stack_needed;
    uint32_t at = *led.many;
    *was = at;
    // Where the call this is about is written, on the frame making it: a body
    // the host's compiler compiled has no instruction pointer to read one
    // off, and a fault under it says `was called here` about a line either
    // way.
    if (at > 0) {
        led.calls[at - 1].said_at = where_asked(runtime, where).offset;
    }
    if (at == led.most) {
        return kest_native_crowded(runtime, which, where, true);
    }
    if (base + needs > led.limit) {
        return kest_native_crowded(runtime, which, where, false);
    }
    // The frame the ledger keeps. Nothing walks its instructions -- there are
    // none to walk -- and what it is for is everything else that reads
    // frames: what a fault says it was called from, how deep a host is told a
    // run is, and what a machine says it needs. This is the sequence a body
    // the host's compiler compiled writes for itself, written once here for
    // whoever cannot inline it. See D1122.
    led.calls[at].chunk = led.chunks[which];
    led.calls[at].ip = NULL;
    led.calls[at].base = base;
    led.calls[at].said_at = 0;
    *led.many = at + 1;
    // How far up the stack is live, for the collector. What is above it is
    // slots nothing wrote, and a walk that reads one keeps something alive a
    // little longer, which is what a conservative walk of a stack does
    // anyway.
    if (base + needs > *led.reached) {
        *led.reached = base + needs;
    }
    return true;
}

bool kest_call_body(KestRuntime *rt, uint32_t which, KestValue *base,
                    uint16_t handed, uint16_t *gave) {
    if (rt == NULL || rt->module == NULL || which >= rt->module->count ||
        rt->frame_count == 0) {
        return false;
    }
    // The frame `kest_native_room` pushed for this call is the ledger's: it
    // has the chunk and the place and nothing walking it. The machine writes
    // a real one over it rather than beside it, so a fault inside says it was
    // called once. What was asked about room, about how deep calls nest and
    // about how much stack is left was asked there, in the machine's own
    // words, so none of it is asked again here.
    KestValue *was_top = rt->running_top;
    uint32_t was_frames = rt->running_frames;
    rt->running_top = base;
    rt->running_frames = rt->frame_count - 1;
    bool went = run_body(rt, (int32_t)which, handed, gave);
    rt->running_frames = was_frames;
    rt->running_top = was_top;
    return went;
}

// A crossing into the host, which is one answer both engines go through: the
// machine's instruction is this door with the budget and the operand stack
// around it, and a body the host's compiler compiled calls it with the
// arguments where the answer goes. Everything the boundary asks is here --
// what the declaration says against what was moved, how far in a host is
// being called from against what it was measured for, whether what it bound
// is still there, whether it did what it was asked, whether it kept a promise
// made on its behalf, and what it wrote back. Seven questions, asked once.
// See D1108.
bool kest_run_at(KestRuntime *rt, int64_t index, uint32_t count,
                 uint32_t where) {
    if (rt == NULL) {
        return false;
    }
    if (index < 0 || (uint64_t)index >= count) {
        return stopped_saying(rt, where, "K0604",
                              "index %lld is outside %u of them",
                              (long long)index, count);
    }
    return true;
}

bool kest_call_host(KestRuntime *rt, uint16_t index, KestValue *base,
                    uint16_t argument_slots, uint16_t result_slots,
                    uint32_t where) {
    if (rt == NULL || rt->module == NULL || rt->frame_count == 0 ||
        base == NULL || index >= rt->module->extern_count) {
        return false;
    }
    const KestModule *module = rt->module;
    Frame *frame = &rt->frames[rt->frame_count - 1];
    KestValue *top = base + argument_slots;
    // Where the crossing is written, on the frame making it. A host may call
    // back in from inside this and what it calls may fail, and then every
    // frame under it says where it made its call -- which a body the host's
    // compiler compiled has no instruction pointer to answer with. See D1094.
    frame->said_at = where_asked(rt, where).offset;
    // Read only where the machine holds itself to the declaration, which is
    // the build that checks itself. Named here so that a release build does
    // not have to be told twice that it is a number nobody read.
    (void)result_slots;
    if (rt->entered != NULL) {
        rt->crossings++;
    }
#if KEST_CHECKED
    // The same three numbers at the crossing, held against the declaration
    // rather than against a body: what an extern takes and gives is a layout
    // for each argument and one for the answer, and how many slots those come
    // to is the number the compiler wrote here. A host is held to this from
    // its own side by `kest_frame_fills`; nothing held the machine to it.
    // See D901.
    {
        uint32_t wanted = 0;
        for (uint16_t which = 0; which < module->externs[index].takes_count;
             which++) {
            wanted +=
                module->layouts[module->externs[index].takes[which]].slots;
        }
        uint32_t answered =
            module->externs[index].gives_value
                ? module->layouts[module->externs[index].gives].slots
                : 0;
        rt->guarded++;
        if (argument_slots != wanted || result_slots != answered ||
            base < frame->base + frame->chunk->slot_count) {
            stopped_saying(rt, where, "K0655",
                           "this crossing hands over %u slot(s) and takes "
                           "back %u, and `%s` is declared to take %u and give "
                           "%u",
                           argument_slots, result_slots,
                           module->externs[index].name, wanted, answered);
            kest_diags_fault(rt->diags,
                             "the compiler's count of the operand stack and "
                             "what the machine moved disagree");
            return false;
        }
    }
#endif
    // What a host was told against what this turned out to be. A host sizes a
    // stack from `kest_needs_from` and then calls back in from here, so a
    // number that is too small is a host that runs out of room somewhere it
    // was told it would not. The floor of this run is where a host function
    // above it left the machine, which is the same place a call back in would
    // start from.
    if (rt->host_measured) {
        // Where this run began, which is the bottom of its first frame. The
        // machine could read that off `running_top`, because a host calling
        // in leaves it there and the first frame stands on it; a body the
        // host's compiler compiled cannot, because entering one raises
        // `running_top` to the top of its frame so that the collector reaches
        // what it is holding. The frame says it either way. See D1108.
        const KestValue *bottom =
            rt->frame_count > rt->running_frames
                ? rt->frames[rt->running_frames].base
            : rt->running_top != NULL ? rt->running_top
                                      : rt->stack;
        uint32_t deep = rt->frame_count - rt->running_frames;
        uint32_t wide = (uint32_t)(top - bottom);
        if (deep > rt->host_frames || wide > rt->host_slots) {
            stopped_saying(rt, where, "K0633",
                           "`%s` calls into the host %u slots and %u frames "
                           "in, where `%s` was measured at %u and %u",
                           frame->chunk->name, wide, deep,
                           rt->host_where != NULL ? rt->host_where : "nothing",
                           rt->host_slots, rt->host_frames);
            kest_diags_fault(rt->diags,
                             "what a host is told it needs to call back in "
                             "from here is that measurement");
            return false;
        }
    }
    KestValue *was_top = rt->running_top;
    uint32_t was_frames = rt->running_frames;
    rt->running_top = top;
    rt->running_frames = rt->frame_count;
    // The one promise in this language that somebody else keeps. A
    // declaration says a host function does not reach the heap, the compiler
    // lets a `no.alloc` body call it on the strength of that, and nothing but
    // this would notice a host that made text in it.
    bool promised = module->externs[index].promises;
    size_t held = promised ? kest_heap_used(rt) : 0;
#if KEST_CHECKED
    // And whether what the host bound is still there to be handed over. A
    // context is the host's own memory and the machine keeps the pointer and
    // never reads it, so a host that binds something on a frame it then
    // returns from leaves this handing a function of its own a pointer into
    // somebody else's stack. Nothing about a pointer says when it stops being
    // one, so this cannot be asked at the binding and can be asked here, in
    // the build that is told where every block a host has ends. One byte of
    // it, because the machine is not told how big a context is and does not
    // need to be: a frame that has gone and a block that has been freed are
    // both gone at their first byte. See D722.
    if (rt->contexts[index] != NULL &&
        __asan_region_is_poisoned(rt->contexts[index], 1) != NULL) {
        rt->running_top = was_top;
        rt->running_frames = was_frames;
        stopped_saying(rt, where, "K0654",
                       "`%s` was bound with something this host has since "
                       "given back",
                       module->externs[index].name);
        kest_diags_suggest(rt->diags,
                           "what a host binds a context with has to outlive "
                           "every machine started with that list");
        return false;
    }
#endif
    rt->native_failed = NULL;
    rt->natives[index](base, rt, rt->contexts[index]);
    // Whether the host could do what it was asked. It is asked first, because
    // everything below reads what the door wrote and a door that failed wrote
    // nothing worth reading. See D937.
    if (rt->native_failed != NULL) {
        const char *said = rt->native_failed;
        rt->native_failed = NULL;
        rt->running_top = was_top;
        rt->running_frames = was_frames;
        stopped_saying(rt, where, "K0662",
                       "`%s` could not do what it was asked: %s",
                       module->externs[index].name, said);
        kest_diags_suggest(rt->diags,
                           "the host said so with `kest_native_failed`, and "
                           "what it wrote into the frame was not read");
        return false;
    }
    if (promised && kest_heap_used(rt) != held) {
        rt->running_top = was_top;
        rt->running_frames = was_frames;
        stopped_saying(rt, where, "K0631",
                       "`%s` promises `no.alloc` and this host took %zu bytes "
                       "in it",
                       module->externs[index].name, kest_heap_used(rt) - held);
        return false;
    }
    // A call back in unwound to exactly where it started, so there is nothing
    // to put back but where the machine was.
    rt->running_top = was_top;
    rt->running_frames = was_frames;
    // And the tag, for a crossing that answers a value with one in it. Every
    // slot after a tag means whatever the tag says, so a host that writes a
    // number the enum has no case for hands back a value whose payload the
    // program reads as a type nobody wrote there -- and unlike every other
    // way a host can be wrong here, there is nowhere to ask about it
    // beforehand: the tag is decided inside the call. This is the one moment
    // it can be said, which is what makes it the machine's to say, the same
    // as the promise above. See D706.
    if (module->externs[index].gives_value) {
        const KestLayout *answers =
            &module->layouts[module->externs[index].gives];
        // Everything in what came back, read the way the door reads what a
        // host hands in: the same walk, over the one value a crossing answers
        // with, saying what a crossing did rather than what a function takes.
        // It used to read the top of that value and no further, so a host
        // answering with a shape that had a piece of text in a field was
        // where the door was before D718. See D719.
        Saying answering = {true, frame, NULL, where};
        uint32_t gave = 0;
        if (!handed_well(rt, &answering, module->externs[index].name,
                         answers->type, base, &gave)) {
            return false;
        }
    }
    return true;
}

static void kest_native_left(KestRuntime *runtime, uint32_t was);

bool kest_call_value(KestRuntime *rt, KestValue what, KestValue *base,
                     uint16_t handed, uint16_t coming_back, uint32_t where,
                     uint16_t *gave) {
    if (rt == NULL || rt->module == NULL || rt->frame_count == 0 ||
        gave == NULL) {
        return false;
    }
    const char *code = NULL;
    char said[160];
    const char *suggest = NULL;
    const char *fault = NULL;
    int64_t which = what.integer;
    const KestChunk *callee = the_function(
        rt->module, rt->frames[rt->frame_count - 1].chunk, which, handed,
        coming_back, &code, said, sizeof said, &suggest, &fault);
    if (callee == NULL) {
        kest_native_stopped(rt, where, code, said);
        if (suggest != NULL) {
            kest_diags_suggest(rt->diags, "%s", suggest);
        }
        if (fault != NULL) {
            kest_diags_fault(rt->diags, fault);
        }
        return false;
    }
    // Room, the ledger frame, and how deep this has got: the same door a call
    // by name goes through, because a call through a value is a call.
    uint32_t was = 0;
    if (!kest_native_room(rt, base, (uint32_t)which, where, &was)) {
        return false;
    }
    bool went;
    if (callee->native != NULL && !rt->untrusted) {
        went = callee->native(rt, base, gave);
    } else {
        went = kest_call_body(rt, (uint32_t)which, base, handed, gave);
    }
    kest_native_left(rt, was);
    return went;
}

// And back out of it: what `was` said, put back.
static void kest_native_left(KestRuntime *runtime, uint32_t was) {
    if (runtime != NULL && was <= runtime->frame_count) {
        runtime->frame_count = was;
    }
}


// The array operations a body the host's compiler compiled reaches, each one
// what the instruction of the same name does and each one called by that
// instruction as well: `push` is one answer, and two would be two the day
// either moved. What a caller hands over is where its own operands are, which
// is on the machine's stack -- a body that can reach the heap lives there
// (D1098) -- so nothing here has to be told what to keep alive: the walk that
// a collection starts with reaches it already. See D1099.
//
// `where` is where in the source the operation is, and a refusal says so at
// that line whichever engine ran it.

// The source a refusal from one of these is reported in, which is the body
// the machine is in the middle of.
static const KestSource *where_from(KestRuntime *rt) {
    return rt->frame_count > 0 ? rt->frames[rt->frame_count - 1].chunk->source
                               : NULL;
}

bool kest_region_open(KestRuntime *rt, uint32_t where, int64_t *into) {
    if (rt == NULL || into == NULL) {
        return false;
    }
    if (rt->kept_count == rt->kept_room && rt->kept_room < MAX_KEPT) {
        uint32_t room = rt->kept_room == 0 ? 8 : rt->kept_room * 2;
        KestMark *grown = KEST_ARENA_ARRAY(rt->own, KestMark, room);
        if (grown == NULL) {
            no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
            return false;
        }
        for (uint32_t i = 0; i < rt->kept_count; i++) {
            grown[i] = rt->kept[i];
        }
        rt->kept = grown;
        rt->kept_room = room;
    }
    if (rt->kept_count == MAX_KEPT) {
        stopped_saying(rt, where, "K0656",
                       "this machine holds %u working-memory blocks at once",
                       (unsigned)MAX_KEPT);
        kest_diags_suggest(rt->diags,
                           "a `scratch { }` inside a function that calls "
                           "itself opens one a call deep");
        return false;
    }
    if (!kest_ground_open(rt->ground)) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was opening a block of working memory");
        return false;
    }
    rt->kept[rt->kept_count] = kest_arena_mark(rt->heap);
    *into = (int64_t)rt->kept_count++;
    return true;
}

bool kest_region_close(KestRuntime *rt, int64_t was, uint32_t where) {
    if (rt == NULL) {
        return false;
    }
#if KEST_CHECKED
    rt->guarded++;
    if (was < 0 || (uint64_t)was >= rt->kept_count) {
        stopped_saying(rt, where, "K0655",
                       "this puts the heap back to a block that is not open");
        kest_diags_fault(rt->diags,
                         "a block the compiler opened and the machine did "
                         "not");
        return false;
    }
#else
    (void)where;
#endif
    if (was >= 0 && (uint64_t)was < rt->kept_count) {
        kest_arena_rewind(rt->heap, rt->kept[was]);
        while (rt->kept_count > (uint32_t)was) {
            kest_ground_close(rt->ground);
            rt->kept_count--;
        }
    }
    return true;
}


bool kest_array_new(KestRuntime *rt, uint16_t layout, int64_t count,
                    const KestValue *fill, uint32_t where, KestValue *into) {
    if (rt == NULL || rt->module == NULL || layout >= rt->module->layout_count ||
        into == NULL) {
        return false;
    }
    const KestLayout *what = &rt->module->layouts[layout];
    if (count < 0) {
        return stopped_saying(rt, where, "K0604",
                              "an array cannot have %lld elements",
                              (long long)count);
    }
    uint32_t hands = rt->hands;
    unsigned char *bytes =
        count > (int64_t)UINT32_MAX
            ? NULL
            : in_hand(rt, elements_for(rt, NULL, what, (uint32_t)count));
    Array *array =
        bytes == NULL ? NULL : take(rt, NULL, sizeof(Array), KEST_GROUND_ARRAY);
    hands_off(rt, hands);
    if (array == NULL || bytes == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was making an array of %lld of %u bytes each",
                           (long long)count, what->size);
        return false;
    }
    array->what = KEST_IS_ARRAY;
    array->length = (uint32_t)count;
    array->capacity = (uint32_t)count;
    array->stride = what->size;
    array->of = what->type;
    array->bytes = bytes;
    bool nothing = true;
    for (uint16_t i = 0; i < what->slots && nothing; i++) {
        nothing = fill == NULL || fill[i].integer == 0;
    }
    if (!nothing) {
        for (int64_t i = 0; i < count; i++) {
            MOVED(moved_packed, what->size);
            pack(bytes + (size_t)i * what->size, what, fill);
        }
    }
    into->object = array;
    return true;
}


// A run of elements the host lent, which a program may read and write and may
// not make longer or shorter: what a host lends is as long as the host said,
// and growing one would move the elements somewhere the host does not know
// about. Said in the one place because eight doors ask it, and eight copies
// of a sentence are eight sentences the day one of them moves. See D668.
static bool it_is_lent(KestRuntime *rt, const Array *array, uint32_t where,
                       bool growing) {
    if (!array->borrowed) {
        return false;
    }
    stopped_saying(rt, where, "K0608",
                   growing ? "this array is the host's, so it cannot grow"
                           : "this array is the host's, so it cannot shrink");
    return true;
}

bool kest_array_push(KestRuntime *rt, KestValue handle, uint16_t layout,
                     const KestValue *value, uint32_t where) {
    if (rt == NULL || rt->module == NULL || layout >= rt->module->layout_count) {
        return false;
    }
    Array *array = the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    const KestLayout *what = &rt->module->layouts[layout];
    if (it_is_lent(rt, array, where, true)) {
        return false;
    }
    // What a program can be told is what it can count to, and `len` gives
    // back an `i32`. One more than that used to double a capacity past what a
    // `uint32_t` holds, which asked for nought bytes and copied two thousand
    // million into them.
    if (array->length == MAX_COUNTED) {
        return stopped_saying(rt, where, "K0630",
                              "this array holds %d, which is all `len` can "
                              "count",
                              MAX_COUNTED);
    }
    if (array->length == array->capacity) {
        uint32_t capacity = array->capacity == 0 ? 8 : array->capacity * 2;
        // Bigger where it stands, when the place it is in has the room --
        // which is what a loop filling one array is. Then there is no copy
        // and no place left behind.
        unsigned char *was = array->bytes;
        unsigned char *grown = elements_grown(rt, NULL, array, what, capacity);
        if (grown != NULL) {
            capacity = all_it_holds(rt, grown, what, capacity);
            (((Elems *)(void *)grown) - 1)->places = capacity;
        }
        if (grown == NULL) {
            no_room_growing_at(rt, where_from(rt), where_asked(rt, where), rt, "an array",
                               array->length, what->size, capacity);
            return false;
        }
        if (grown != was) {
            // What the copy cost, charged where a budget counts work. Only
            // the push that could not grow where it stood pays it. See D950.
            kest_fuel_spend(rt, array->length);
        }
        // The handle is the header, and the header is what moved nothing, so
        // every reference to this array sees the growth.
        array->bytes = grown;
        array->capacity = capacity;
    }
    MOVED(moved_packed, what->size);
    pack(array->bytes + (size_t)array->length * what->size, what, value);
    array->length++;
    return true;
}


// Room for what is coming, in the one thing that has room or the other: a
// store is made with room by `store(n)` and an array by `array(n, v)`, and
// this is the same sentence said to one that is already there. The length
// does not move, so asking for less than it holds is asking for nothing.
bool kest_array_room(KestRuntime *rt, KestValue handle, uint16_t layout,
                     int64_t wanted, uint32_t where) {
    if (rt == NULL || rt->module == NULL ||
        layout >= rt->module->layout_count) {
        return false;
    }
    void *given = handle.object;
    if (KEST_HANDLE_IS(given, KEST_IS_STORE)) {
        Store *store = given;
        if (wanted > MAX_COUNTED) {
            return stopped_saying(rt, where, "K0630",
                                  "this store holds %d, which is all `len` "
                                  "can count",
                                  MAX_COUNTED);
        }
        if (wanted > (int64_t)store->capacity &&
            !room_for(rt, NULL, store, (uint32_t)wanted)) {
            no_room_growing_at(rt, where_from(rt), where_asked(rt, where), rt, "a store",
                               store->used,
                               (uint32_t)(sizeof(KestValue) * store->stride),
                               (uint32_t)wanted);
            return false;
        }
        return true;
    }
    Array *array = the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, true)) {
        return false;
    }
    if (wanted > MAX_COUNTED) {
        return stopped_saying(rt, where, "K0630",
                              "this array holds %d, which is all `len` can "
                              "count",
                              MAX_COUNTED);
    }
    if (wanted > (int64_t)array->capacity) {
        const KestLayout *what = &rt->module->layouts[layout];
        uint32_t capacity = (uint32_t)wanted;
        unsigned char *grown = elements_grown(rt, NULL, array, what, capacity);
        if (grown == NULL) {
            no_room_growing_at(rt, where_from(rt), where_asked(rt, where), rt, "an array",
                               array->length, what->size, capacity);
            return false;
        }
        array->bytes = grown;
        array->capacity = capacity;
    }
    return true;
}

// Everything out of it, which is the length and not the block: what it held
// is still there to be written over.
bool kest_array_clear(KestRuntime *rt, KestValue handle, uint32_t where) {
    Array *array =
        rt == NULL ? NULL
                   : the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, false)) {
        return false;
    }
    array->length = 0;
    return true;
}

// The last one off the end. Where it was comes back, which is still the run's
// own memory -- the length moved and the block did not -- or nothing where
// there was nothing to take.
bool kest_array_pop(KestRuntime *rt, KestValue handle, uint32_t where,
                    unsigned char **at) {
    Array *array =
        rt == NULL ? NULL
                   : the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL || at == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, false)) {
        return false;
    }
    if (array->length == 0) {
        *at = NULL;
        return true;
    }
    array->length--;
    *at = array->bytes + (size_t)array->length * array->stride;
    return true;
}

// A run written out in the program: that many values, already on the stack,
// becoming the one thing that names them. The elements are taken before the
// header, because a walk set off by taking the header would find elements
// nothing names.
bool kest_array_written(KestRuntime *rt, uint16_t layout, uint16_t count,
                   const KestValue *values, uint32_t where, KestValue *into) {
    if (rt == NULL || rt->module == NULL ||
        layout >= rt->module->layout_count || into == NULL) {
        return false;
    }
    const KestLayout *what = &rt->module->layouts[layout];
    uint32_t hands = rt->hands;
    unsigned char *bytes = in_hand(rt, elements_for(rt, NULL, what, count));
    Array *array =
        bytes == NULL ? NULL : take(rt, NULL, sizeof(Array), KEST_GROUND_ARRAY);
    hands_off(rt, hands);
    if (array == NULL || bytes == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was making an array of %u of %u bytes each",
                           count, what->size);
        return false;
    }
    array->what = KEST_IS_ARRAY;
    array->length = count;
    array->capacity = count;
    array->stride = what->size;
    array->of = what->type;
    array->bytes = bytes;
    for (uint16_t i = 0; i < count; i++) {
        MOVED(moved_packed, what->size);
        pack(bytes + (size_t)i * what->size, what,
             values + (size_t)i * what->slots);
    }
    into->object = array;
    return true;
}

// One more on the end when the room is already there, and nothing when it is
// not: the same append with the growth taken out, which is what a body under
// a promise to reach no heap may do (D940). Answers whether it went in.
bool kest_array_fit(KestRuntime *rt, KestValue handle, uint16_t layout,
                    const KestValue *value, uint32_t where, int64_t *put) {
    if (rt == NULL || rt->module == NULL ||
        layout >= rt->module->layout_count || put == NULL) {
        return false;
    }
    Array *array = the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, true)) {
        return false;
    }
    const KestLayout *what = &rt->module->layouts[layout];
    if (array->length >= array->capacity || array->length == MAX_COUNTED) {
        *put = 0;
        return true;
    }
    MOVED(moved_packed, what->size);
    pack(array->bytes + (size_t)array->length * what->size, what, value);
    array->length++;
    MOVED(moved_held, sizeof(KestValue));
    *put = 1;
    return true;
}

// A whole piece of text onto a run of bytes, growing for it. Text is its
// bytes (D021), so this is the loop a program had to write taken into one
// move. See D1068.
bool kest_array_push_text(KestRuntime *rt, KestValue handle, uint16_t layout,
                          const char *bytes, int64_t length, uint32_t where) {
    if (rt == NULL || rt->module == NULL ||
        layout >= rt->module->layout_count) {
        return false;
    }
    Array *array = the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    const KestLayout *what = &rt->module->layouts[layout];
    if (it_is_lent(rt, array, where, true)) {
        return false;
    }
    if ((uint64_t)array->length + (uint64_t)length > (uint64_t)MAX_COUNTED) {
        return stopped_saying(rt, where, "K0630",
                              "this array holds %d, which is all `len` can "
                              "count",
                              MAX_COUNTED);
    }
    uint32_t wanted = array->length + (uint32_t)length;
    if (wanted > array->capacity) {
        uint32_t capacity = array->capacity == 0 ? 8 : array->capacity;
        while (capacity < wanted) {
            capacity *= 2;
        }
        unsigned char *was = array->bytes;
        unsigned char *grown = elements_grown(rt, NULL, array, what, capacity);
        if (grown == NULL) {
            no_room_growing_at(rt, where_from(rt), where_asked(rt, where), rt, "an array",
                               array->length, what->size, capacity);
            return false;
        }
        capacity = all_it_holds(rt, grown, what, capacity);
        (((Elems *)(void *)grown) - 1)->places = capacity;
        if (grown != was) {
            kest_fuel_spend(rt, array->length);
        }
        array->bytes = grown;
        array->capacity = capacity;
    }
    MOVED(moved_packed, (uint64_t)length);
    if (length > 0) {
        memcpy(array->bytes + array->length, bytes, (size_t)length);
    }
    array->length = wanted;
    return true;
}

// And the same with the growth taken out: all of it fits or none of it goes
// in, because a piece half written is a piece nobody can take back.
bool kest_array_fit_text(KestRuntime *rt, KestValue handle, const char *bytes,
                         int64_t length, uint32_t where, int64_t *put) {
    if (rt == NULL || put == NULL) {
        return false;
    }
    Array *array = the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, true)) {
        return false;
    }
    if ((uint64_t)array->length + (uint64_t)length >
            (uint64_t)array->capacity ||
        (uint64_t)array->length + (uint64_t)length > (uint64_t)MAX_COUNTED) {
        *put = 0;
        return true;
    }
    MOVED(moved_packed, (uint64_t)length);
    if (length > 0) {
        memcpy(array->bytes + array->length, bytes, (size_t)length);
    }
    array->length += (uint32_t)length;
    MOVED(moved_held, sizeof(KestValue));
    *put = 1;
    return true;
}

bool kest_array_remove(KestRuntime *rt, KestValue handle, int64_t index,
                       uint32_t where) {
    Array *array = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (array == NULL) {
        return false;
    }
    if (it_is_lent(rt, array, where, false)) {
        return false;
    }
    if (index < 0 || (uint64_t)index >= array->length) {
        return stopped_saying(rt, where, "K0604",
                              "index %lld is outside an array of length %u",
                              (long long)index, array->length);
    }
    // What is after it keeps its order, which is the whole difference between
    // this and a store: a position here means something.
    unsigned char *at = array->bytes + (size_t)index * array->stride;
    MOVED(moved_shifted,
          (uint64_t)(array->length - index - 1) * array->stride);
    memmove(at, at + array->stride,
            (size_t)(array->length - index - 1) * array->stride);
    array->length--;
    return true;
}


// A store and what a program asks of one: made, added to, read, written,
// taken from, counted, walked. Each is what the instruction of that name
// does, and the instruction calls it. A store keeps what it holds as slots
// rather than as packed bytes, so none of these walks a layout: what moves is
// a run of slots the width the shape is. See D1102.

bool kest_store_new(KestRuntime *rt, uint16_t layout, int64_t room,
                    uint32_t where, KestValue *into) {
    if (rt == NULL || rt->module == NULL || into == NULL ||
        layout >= rt->module->layout_count) {
        return false;
    }
    if (room < 0) {
        return stopped_saying(rt, where, "K0604",
                              "a store cannot have room for %lld",
                              (long long)room);
    }
    uint32_t hands = rt->hands;
    Store *store = in_hand(rt, take(rt, NULL, sizeof(Store),
                                    KEST_GROUND_STORE));
    if (store == NULL) {
        hands_off(rt, hands);
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags, "it was making a store");
        return false;
    }
    store->what = KEST_IS_STORE;
    store->stride = rt->module->layouts[layout].slots;
    store->of = rt->module->layouts[layout].type;
    store->holds = &rt->module->layouts[layout];
    // Made here rather than at the first `add`, which is the whole of what a
    // count buys: the growth is where the program asked for it.
    if (room > 0 && !room_for(rt, NULL, store, (uint32_t)room)) {
        hands_off(rt, hands);
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was making a store with room for %lld",
                           (long long)room);
        return false;
    }
    into->object = store;
    hands_off(rt, hands);
    return true;
}


bool kest_store_add(KestRuntime *rt, KestValue handle, uint16_t stride,
                    const KestValue *value, uint32_t where, int64_t *into) {
    Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || into == NULL) {
        return false;
    }
    uint32_t index;
    if (store->free_count > 0) {
        index = store->free_slots[--store->free_count];
    } else {
        if (store->used == MAX_COUNTED) {
            return stopped_saying(rt, where, "K0630",
                                  "this store holds %d, which is all `len` "
                                  "can count",
                                  MAX_COUNTED);
        }
        if (store->used == store->capacity) {
            // What a store that has to grow copies, charged before it does.
            kest_fuel_spend(rt, store->used);
        }
        if (store->used == store->capacity && !grow_store(rt, NULL, store)) {
            // A store grows by four runs at once, so what it was reaching for
            // is wider than one of them.
            no_room_growing_at(rt, where_from(rt), where_asked(rt, where), rt, "a store",
                               store->used, sizeof(KestValue) * store->stride,
                               store->capacity == 0 ? 8
                                                    : store->capacity * 2);
            return false;
        }
        index = store->used++;
        if (index >= store->high) {
            store->high = index + 1;
        }
    }
    // The next handout number there is, taken from the count for the whole
    // process so that no two places anywhere are ever stamped the same. See
    // D1033.
    uint64_t handout = next_handout(rt);
    if (handout > MOST_STAMPS) {
        stopped_saying(rt, where, "K0630",
                       "this process has handed out %llu places in stores, "
                       "which is all it can tell apart",
                       (unsigned long long)MOST_STAMPS);
        kest_diags_suggest(rt->diags,
                           "a place is stamped once for every `add`, and a "
                           "number is never handed out twice");
        return false;
    }
    rt->stamps++;
    store->serials[index] = handout;
    mark_live(store, index, true);
    store->count++;
    MOVED(moved_payload, (uint64_t)stride * sizeof(KestValue));
    memcpy(store->elements + (size_t)index * stride, value,
           sizeof(KestValue) * stride);
    *into = pack_ref(store->serials[index], index);
    return true;
}

bool kest_store_get(KestRuntime *rt, KestValue handle, int64_t which,
                    uint16_t stride, KestValue *into, uint32_t where) {
    Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || into == NULL) {
        return false;
    }
    const KestValue *at = resolve_ref(store, which);
    if (at == NULL) {
        for (uint16_t i = 0; i < stride; i++) {
            into[i].integer = 0;
        }
        into[stride].integer = 0;
        return true;
    }
    MOVED(moved_payload, (uint64_t)stride * sizeof(KestValue));
    memcpy(into, at, sizeof(KestValue) * stride);
    into[stride].integer = 1;
    return true;
}

bool kest_store_set(KestRuntime *rt, KestValue handle, int64_t which,
                    uint16_t stride, const KestValue *value, uint32_t where,
                    bool *was) {
    Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || was == NULL) {
        return false;
    }
    KestValue *at = resolve_ref(store, which);
    if (at != NULL) {
        MOVED(moved_payload, (uint64_t)stride * sizeof(KestValue));
        memcpy(at, value, sizeof(KestValue) * stride);
    }
    *was = at != NULL;
    return true;
}

bool kest_store_remove(KestRuntime *rt, KestValue handle, int64_t which,
                       uint32_t where, bool *was) {
    Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || was == NULL) {
        return false;
    }
    if (resolve_ref(store, which) == NULL) {
        *was = false;
        return true;
    }
    uint32_t index = ref_place(which);
    mark_live(store, index, false);
    // The slot keeps the stamp it was handed out with, so a reference made
    // before it was given back still names that stamp and the slot is not
    // live: stale stays stale.
    store->free_slots[store->free_count++] = index;
    store->count--;
    if (store->count == 0) {
        store->used = 0;
        store->free_count = 0;
    }
    *was = true;
    return true;
}

bool kest_store_count(KestRuntime *rt, KestValue handle, uint32_t where,
                      int64_t *into) {
    const Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || into == NULL) {
        return false;
    }
    *into = store->count;
    return true;
}

bool kest_store_ref(KestRuntime *rt, KestValue handle, int64_t index,
                    uint32_t where, int64_t *into) {
    const Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || into == NULL || index < 0) {
        return false;
    }
    *into = pack_ref(store->serials[(uint32_t)index], (uint32_t)index);
    return true;
}

bool kest_store_seek(KestRuntime *rt, KestValue handle, int64_t from,
                     uint32_t where, int64_t *found) {
    const Store *store = rt == NULL ? NULL : the_handle(rt, handle, KEST_IS_STORE, "a store", where);
    if (store == NULL || found == NULL) {
        return false;
    }
    *found = live_from(store, from);
    return true;
}

int64_t kest_text_hash(const char *bytes, int64_t length) {
    return (int64_t)kest_mark_bytes(KEST_MARK_START, bytes,
                                    length < 0 ? 0 : (size_t)length);
}

int64_t kest_text_order(const char *left, int64_t left_length,
                        const char *right, int64_t right_length,
                        int64_t *read) {
    if (left_length < 0) {
        left_length = 0;
    }
    if (right_length < 0) {
        right_length = 0;
    }
    int64_t shorter = left_length < right_length ? left_length : right_length;
    int64_t far = 0;
    while (far < shorter && left[far] == right[far]) {
        far++;
    }
    if (read != NULL) {
        *read = far;
    }
    if (far == left_length || far == right_length) {
        // One ran out before they differed, so the shorter one comes first,
        // and two that ran out together are one piece of text.
        return left_length - right_length;
    }
    return (int64_t)(unsigned char)left[far] -
           (int64_t)(unsigned char)right[far];
}

bool kest_text_at(KestRuntime *runtime, const char *bytes, int64_t length,
                  int64_t index, uint32_t where, int64_t *into) {
    if (runtime == NULL || into == NULL) {
        return false;
    }
    if (index < 0 || index >= length) {
        return stopped_saying(runtime, where, "K0604",
                              "index %lld is outside text of %u bytes",
                              (long long)index, (unsigned)length);
    }
    *into = (unsigned char)bytes[index];
    return true;
}

bool kest_text_in(KestRuntime *runtime, const char *bytes, int64_t length,
                  int64_t index, uint32_t where, int64_t *into) {
    if (runtime == NULL || into == NULL) {
        return false;
    }
    if (index < 0 || index >= length) {
        stopped_saying(runtime, where, "K0645",
                       "a walk read byte %lld of text of %u bytes",
                       (long long)index, (unsigned)length);
        kest_diags_fault(runtime->diags,
                         "a walk over text takes its length before its first "
                         "turn and reads without asking");
        return false;
    }
    *into = (unsigned char)bytes[index];
    return true;
}

bool kest_text_cut(KestRuntime *runtime, const char *bytes, int64_t length,
                   int64_t from, int64_t count, uint32_t where,
                   const char **at, int64_t *many) {
    if (runtime == NULL || at == NULL || many == NULL) {
        return false;
    }
    if (from < 0 || count < 0 || from + count > length) {
        return stopped_saying(runtime, where, "K0604",
                              "%lld bytes from %lld is outside text of %u "
                              "bytes",
                              (long long)count, (long long)from,
                              (unsigned)length);
    }
    *at = bytes + from;
    *many = count;
    return true;
}

bool kest_text_rest(KestRuntime *runtime, const char *bytes, int64_t length,
                    int64_t from, uint32_t where, const char **at,
                    int64_t *many) {
    if (runtime == NULL || at == NULL || many == NULL) {
        return false;
    }
    if (from < 0 || from > length) {
        return stopped_saying(runtime, where, "K0604",
                              "the rest from %lld is outside text of %u bytes",
                              (long long)from, (unsigned)length);
    }
    *at = bytes + from;
    *many = length - from;
    return true;
}

bool kest_text_matches(KestRuntime *runtime, const char *bytes, int64_t length,
                       int64_t at, const char *needle, int64_t needle_length,
                       uint32_t where, int64_t *into) {
    if (runtime == NULL || into == NULL) {
        return false;
    }
    if (at < 0 || at > length) {
        return stopped_saying(runtime, where, "K0604",
                              "looking at %lld, which is outside text of %u "
                              "bytes",
                              (long long)at, (unsigned)length);
    }
    bool same = at + needle_length <= length;
    int64_t read = 0;
    while (same && read < needle_length &&
           bytes[at + read] == needle[read]) {
        read++;
    }
    *into = same && read == needle_length;
    return true;
}

bool kest_text_find(KestRuntime *runtime, const char *bytes, int64_t length,
                    const char *needle, int64_t needle_length, int64_t from,
                    uint32_t where, int64_t *at, int64_t *found) {
    if (runtime == NULL || at == NULL || found == NULL) {
        return false;
    }
    if (from < 0 || from > length) {
        return stopped_saying(runtime, where, "K0604",
                              "looking from %lld, which is outside text of %u "
                              "bytes",
                              (long long)from, (unsigned)length);
    }
    int64_t where_it_is = found_at(bytes, length, needle, needle_length, from);
    *at = where_it_is < 0 ? 0 : where_it_is;
    *found = where_it_is >= 0;
    return true;
}

// The shape a layout is, which is what both of the two below are about. A
// layout the module has not got is nothing, and the callers answer for that
// rather than reading past the end of the list.
static const KestType *the_shape(KestRuntime *rt, uint16_t layout) {
    if (rt == NULL || rt->module == NULL ||
        layout >= rt->module->layout_count) {
        return NULL;
    }
    return rt->module->layout_types[layout];
}

int64_t kest_value_hash(KestRuntime *runtime, uint16_t layout,
                        const KestValue *slots) {
    const KestType *type = the_shape(runtime, layout);
    if (type == NULL || slots == NULL) {
        return 0;
    }
    return (int64_t)kest_hash_value(type, slots);
}

bool kest_value_same(KestRuntime *runtime, uint16_t layout,
                     const KestValue *left, const KestValue *right) {
    const KestType *type = the_shape(runtime, layout);
    if (type == NULL || left == NULL || right == NULL) {
        return false;
    }
    return values_equal(type, left, right);
}

// Two slots made out of bytes the arena handed back, which is the last thing
// each of the four below does.
static void text_lands(KestValue *into, const char *bytes, size_t length) {
    into[0].text = bytes;
    into[1].integer = (int64_t)length;
}

bool kest_text_of(KestRuntime *rt, uint8_t how, KestValue value,
                  uint32_t where, KestValue *into) {
    if (rt == NULL || into == NULL) {
        return false;
    }
    char buffer[64];
    int written;
    switch ((KestTextOf)how) {
    case KEST_TEXT_OF_UNSIGNED:
        written = kest_write_whole(buffer, (uint64_t)value.integer, false);
        break;
    case KEST_TEXT_OF_REAL:
    case KEST_TEXT_OF_NARROW:
        written = kest_write_real(buffer, sizeof(buffer), value.real,
                                  (KestTextOf)how == KEST_TEXT_OF_NARROW);
        break;
    case KEST_TEXT_OF_BOOL:
        written = snprintf(buffer, sizeof(buffer), "%s",
                           value.integer ? "true" : "false");
        break;
    case KEST_TEXT_OF_INT:
    default:
        written = kest_write_whole(buffer, (uint64_t)value.integer, true);
        break;
    }
    char *text = take(rt, NULL, (size_t)written + 1, KEST_GROUND_PLAIN);
    if (text == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was writing a number as %d bytes of text",
                           written);
        return false;
    }
    MOVED(moved_text, (uint64_t)written + 1);
    memcpy(text, buffer, (size_t)written + 1);
    text_lands(into, text, (size_t)written);
    return true;
}

bool kest_text_of_value(KestRuntime *rt, uint16_t layout,
                        const KestValue *slots, uint32_t where,
                        KestValue *into) {
    const KestType *type = the_shape(rt, layout);
    if (type == NULL || slots == NULL || into == NULL) {
        return false;
    }
    size_t length = format_value(NULL, 0, type, slots);
    char *text = take(rt, NULL, length + 1, KEST_GROUND_PLAIN);
    if (text == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was writing a value as %zu bytes of text",
                           length);
        return false;
    }
    format_value(text, length, type, slots);
    text[length] = '\0';
    text_lands(into, text, length);
    return true;
}

bool kest_text_join(KestRuntime *rt, const KestValue *pieces, uint16_t count,
                    uint32_t where, KestValue *into) {
    if (rt == NULL || (pieces == NULL && count != 0) || into == NULL) {
        return false;
    }
    size_t length = 0;
    for (uint16_t i = 0; i < count; i++) {
        length += (size_t)pieces[(uint32_t)i * 2 + 1].integer;
    }
    // The same ceiling an array has, and text is where a program reaches it
    // without meaning to: two of these joined is a new one as long as both.
    if (length > (size_t)MAX_COUNTED) {
        return stopped_saying(rt, where, "K0630",
                              "this text would hold %zu, which is more than "
                              "`len` can count",
                              length);
    }
    char *text = take(rt, NULL, length + 1, KEST_GROUND_PLAIN);
    if (text == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags, "it was joining text into %zu bytes",
                           length);
        return false;
    }
    size_t used = 0;
    for (uint16_t i = 0; i < count; i++) {
        size_t many = (size_t)pieces[(uint32_t)i * 2 + 1].integer;
        MOVED(moved_text, many);
        memcpy(text + used, pieces[(uint32_t)i * 2].text, many);
        used += many;
    }
    text[used] = '\0';
    text_lands(into, text, used);
    return true;
}

bool kest_text_from(KestRuntime *rt, KestValue handle, uint32_t where,
                    KestValue *into) {
    Array *bytes =
        rt == NULL ? NULL
                   : the_handle(rt, handle, KEST_IS_ARRAY, "an array", where);
    if (bytes == NULL || into == NULL) {
        return false;
    }
    uint32_t many = bytes->length;
    char *text = take(rt, NULL, (size_t)many + 1, KEST_GROUND_PLAIN);
    if (text == NULL) {
        no_room_at(rt, where_from(rt), where_asked(rt, where), rt);
        kest_diags_suggest(rt->diags,
                           "it was making %u bytes of text out of an array",
                           many);
        return false;
    }
    // Text is UTF-8 and a run of bytes is whatever it holds, so this is the
    // door where the two meet and the one place the walk is paid for. A
    // nought is fine: it is a character. See D971.
    uint32_t bad = 0;
    if (!kest_utf8_whole((const char *)bytes->bytes, many, &bad)) {
        return stopped_saying(rt, where, "K0604",
                              "byte %u begins no character, and text is UTF-8",
                              bad);
    }
    MOVED(moved_text, many);
    memcpy(text, bytes->bytes, many);
    text[many] = '\0';
    text_lands(into, text, many);
    return true;
}

bool kest_native_stopped(KestRuntime *runtime, uint32_t offset,
                         const char *code, const char *message) {
    if (runtime == NULL) {
        return false;
    }
    KestSpan span = where_asked(runtime, offset);
    const KestSource *source =
        runtime->frame_count > 0
            ? runtime->frames[runtime->frame_count - 1].chunk->source
            : NULL;
    // The words are the body's, already put together: what a refusal says is
    // the machine's sentence and a compiled body says the same one.
    said_here(runtime, source, span, code, "%s", message);
    return false;
}

void kest_collected(KestRuntime *runtime,
                    void (*told)(const KestPause *pause, void *context),
                    void *context) {
    if (runtime == NULL) {
        return;
    }
    runtime->collected = told;
    runtime->collected_context = context;
}

void kest_clock(KestRuntime *runtime, uint64_t (*now)(void *), void *context) {
    if (runtime == NULL) {
        return;
    }
    runtime->clock = now;
    runtime->clock_context = context;
}

bool kest_telemetry(const KestRuntime *runtime, KestTelemetry *into) {
    if (runtime == NULL || into == NULL) {
        return false;
    }
    memset(into, 0, sizeof *into);
    KestGroundCounts ground = {0};
    kest_ground_counted(runtime->ground, &ground);
    into->allocations = ground.allocations;
    into->asked = ground.asked;
    into->given = ground.given;
    into->grown = ground.grown;
    into->sweeps = ground.sweeps;
    into->reclaimed = ground.reclaimed;
    into->plots_made = ground.plots_made;
    into->plots_freed = ground.plots_freed;
    into->blocks = ground.blocks;
    into->walked = runtime->walked;
    into->worst_walk = runtime->worst_walk;
    into->marking = runtime->marking;
    into->sweeping = runtime->sweeping;
    into->roots = runtime->roots;
    into->lends = runtime->lends;
    into->lent_elements = runtime->lent_elements;
    into->copied = runtime->copied;
    return true;
}

bool kest_collect(KestRuntime *runtime) {
    if (runtime == NULL) {
        return false;
    }
    if (!between_calls(runtime, "walked")) {
        return false;
    }
    return gather(runtime, NULL);
}

void kest_collect_after(KestRuntime *runtime, uint32_t times) {
    if (runtime == NULL) {
        return;
    }
    // Nought and one are the same thing: a walk after being handed what it
    // holds. It takes effect at the next sweep, because what it multiplies is
    // what that sweep leaves standing. See D1045.
    runtime->walk_after = times == 0 ? 1 : times;
}

uint8_t kest_break_byte(void) {
    return (uint8_t)KEST_OP_STOP;
}
