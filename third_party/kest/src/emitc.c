#include "emitc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// How many arguments and how much of a frame this will write. A body wider
// than these is one the machine runs: what stops it is nothing in C, and a
// ceiling written down is a ceiling a reader can see, where a backend that
// wrote a function of nine hundred arguments would be found out by somebody
// else's compiler.
#define MOST_PARAMS 32

// Text being built. The arena is the caller's and the buffer is grown in
// place, because what this writes is one file made of one string per body and
// nothing frees a piece of it on its own.
typedef struct {
    char *bytes;
    size_t count;
    size_t room;
} Text;

// One body, as far as this backend got with it. A body it could not write
// keeps its name and why: that is what the file says at the top, and what a
// reader asking why something is slow reads first.
typedef struct {
    const char *symbol;
    Text wrote;
    // What has to stand at the top of the file for this body to read: a run
    // of constants written out where an index can reach it. A body reads one
    // and cannot hold one, because what it reads is decided while it runs.
    // See D1119.
    Text ahead;
    // Which functions of the module it calls, so that a body calling one this
    // backend did not write can be found and left out as well. A call is a
    // call to a C function here; there is no dispatch to fall back through.
    uint32_t *calls;
    uint32_t call_count;
    uint32_t call_room;
    uint16_t params;
    uint16_t results;
    bool written;
    // Whether it reads its arguments in the frame it was handed rather than
    // out of what it was called with, which every caller then leaves there.
    // See D1198.
    bool reads_frame;
    // Which of its arguments, a bit each, it reads there -- and so is not
    // handed as a value at all. See D1199.
    uint32_t in_frame;
    // Whether a body calling it carries it, the way the machine writes a
    // small body into the one that calls it; then its prototype asks the
    // host's compiler to do the same. See D1202.
    bool carried_in;
    // And, for one this backend did not write, whether anything it did write
    // calls it: such a body gets a C function of its own all the same, which
    // hands the call to the machine. See D1105.
    bool handed_over;
    const char *why;
} Body;

struct KestEmitC {
    KestArena *arena;
    // What the module says about itself, which is two things this backend
    // asks: how a value is laid out where memory is shared, and what a body
    // it is about to call promised. Both are known before any body is
    // compiled, so a call forward is answered the same as a call back. See
    // D1095.
    const KestModule *module;
    // Where what is read while one body is written goes: how deep the stack
    // is at each operation, and which of them a branch lands on. It is put
    // back between bodies rather than kept, because a program is as many of
    // these as it has bodies and one of them is alive at a time.
    KestArena *scratch;
    Body *bodies;
    uint32_t count;
    uint32_t room;
    // Whether anything reached for `kest_real_to_int` or `kest_left_over`,
    // which are the two answers this backend does not write out for itself.
    // What a number outside a width becomes and what is left over from
    // dividing two floats are the machine's answers and the folder's alike
    // (D668, D669): a third copy here would be a third answer the day one of
    // them moves, so the file calls the library's.
    bool wants_library;
    bool out_of_memory;
};

// Room for something that is growing. The arena makes the last thing it
// handed out bigger and answers nothing for anything else, which is every
// buffer here as soon as a second one is growing beside it: what this adds is
// the other half, which is a new one and a copy. A doubling, so the copy
// happens a logarithmic number of times rather than every time something is
// written.
static void *grow(KestArena *arena, void *last, size_t was, size_t want,
                  size_t align) {
    void *bigger = last == NULL ? NULL
                                : kest_arena_extend(arena, last, was, want);
    if (bigger != NULL) {
        return bigger;
    }
    bigger = kest_arena_alloc(arena, want, align);
    if (bigger == NULL) {
        return NULL;
    }
    if (last != NULL) {
        memcpy(bigger, last, was);
    }
    return bigger;
}

static void say(KestEmitC *c, Text *text, const char *format, ...)
    KEST_SAYS(3, 4);

static void say(KestEmitC *c, Text *text, const char *format, ...) {
    for (int round = 0; round < 2; round++) {
        va_list args;
        va_start(args, format);
        size_t left = text->room - text->count;
        // Nothing rather than nowhere plus nought: a buffer that has not been
        // taken yet is a null pointer, and a null pointer with an offset added
        // to it is undefined however small the offset -- which the build that
        // checks itself says out loud.
        char *end = text->bytes == NULL ? NULL : text->bytes + text->count;
        int wrote = vsnprintf(end, left, format, args);
        va_end(args);
        if (wrote < 0) {
            c->out_of_memory = true;
            return;
        }
        if ((size_t)wrote < left) {
            text->count += (size_t)wrote;
            return;
        }
        size_t want = text->room == 0 ? 512 : text->room * 2;
        while (want < text->count + (size_t)wrote + 1) {
            want *= 2;
        }
        char *grown = grow(c->arena, text->bytes, text->room, want, 1);
        if (grown == NULL) {
            c->out_of_memory = true;
            return;
        }
        text->bytes = grown;
        text->room = want;
    }
    // Grown to fit and still not fitting is the one way out of that loop that
    // is not a return, and what it leaves is half a line of C.
    c->out_of_memory = true;
}

// What one body is being written as, kept while it is written and let go with
// it. The depth of the operand stack at each operation is worked out before
// anything is written: an operand is a place in a C array here rather than
// somewhere a pointer has got to, so where each one sits has to be known at
// every operation, including the ones two ways of getting there arrive at.
typedef struct {
    KestEmitC *c;
    const KestIrBody *body;
    Body *into;
    // Whether this body may hold a handle in a local, which is whether
    // nothing it does can reach the heap. Worked out once before anything is
    // written, and its opposite is where the body lives: one that can reach
    // the heap keeps its slots and its operands on the machine's stack, where
    // the collector walks. See D1098.
    bool no_heap;
    bool on_the_stack;
    // For a body on the machine's stack, which of its slots are kept in a C
    // array of its own rather than on the machine's stack: `SLOT_LOOSE` or
    // `SLOT_FRAME`. NULL for a body not on the machine's stack, whose slots
    // are all C already. See D1162.
    uint8_t *slot_modes;
    uint32_t *depth;
    bool *known;
    bool *landed;
    // Whether the operation being written stands on the machine's stack
    // rather than in the body's own operands, and which operands the machine's
    // stack holds the same value of already. See `in_memory` below.
    bool in_memory;
    bool *clean;
    uint32_t stack;
    uint32_t deepest;
    // Why this body is not being written, or NULL. The first reason is kept:
    // a body with two things in it this backend has no C for is one body.
    const char *why;
    // And the same reason with the numbers in it, where there are numbers
    // worth reading. It is copied into the file's own memory afterwards,
    // because what is written about a body is read long after the body is
    // gone.
    char said[96];
} Walk;

// Where a slot of a body on the machine's stack is kept; see `at_frame` below
// and D1162.
enum { SLOT_FRAME, SLOT_LOOSE };

// Whether the body being written had the one at `which` carried into it by
// the lowering rather than calling it. See D1156.
// Where each slot of a body on the machine's stack is kept. A slot is loose
// -- a C variable and nothing else -- only when something says what it holds,
// a name the body declared or a place it reads or writes, and nothing that
// says so holds a pointer; and not when its place is handed out: a name that
// holds where a value is, a run the body indexes while it runs, and the slots
// a door writes through. NULL for no memory. See D1162.
static void mark_frame(uint8_t *modes, uint32_t slots, uint32_t from,
                       uint32_t many) {
    for (uint32_t k = from; k < from + many && k < slots; k++) {
        modes[k] = SLOT_FRAME;
    }
}

static uint8_t *slot_modes(KestArena *scratch, const KestIrBody *body) {
    uint32_t slots = body->slot_count;
    uint8_t *modes = KEST_ARENA_ARRAY(scratch, uint8_t, slots == 0 ? 1 : slots);
    bool *named = KEST_ARENA_ARRAY(scratch, bool, slots == 0 ? 1 : slots);
    bool *held = KEST_ARENA_ARRAY(scratch, bool, slots == 0 ? 1 : slots);
    if (modes == NULL || named == NULL || held == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < slots; i++) {
        named[i] = false;
        held[i] = false;
    }
    const KestType *which = NULL;
    for (uint32_t n = 0; n < body->name_count; n++) {
        const KestIrName *name = &body->names[n];
        bool own = kest_type_holds_own(name->type, &which);
        for (uint32_t k = name->slot;
             k < (uint32_t)name->slot + name->slots && k < slots; k++) {
            named[k] = true;
            held[k] = held[k] || own;
        }
    }
    for (uint32_t p = 0; p < body->place_count; p++) {
        const KestIrPlace *place = &body->places[p];
        if (place->kind != KEST_IR_PLACE_SLOT) {
            continue;
        }
        bool own = kest_type_holds_own(place->type, &which);
        for (uint32_t k = place->slot;
             k < (uint32_t)place->slot + place->slots && k < slots; k++) {
            named[k] = true;
            held[k] = held[k] || own;
        }
    }
    for (uint32_t i = 0; i < slots; i++) {
        modes[i] = named[i] && !held[i] ? SLOT_LOOSE : SLOT_FRAME;
    }
    for (uint32_t n = 0; n < body->name_count; n++) {
        if (body->names[n].by_address) {
            mark_frame(modes, slots, body->names[n].slot,
                       body->names[n].slots);
        }
    }
    for (uint32_t p = 0; p < body->place_count; p++) {
        const KestIrPlace *place = &body->places[p];
        if (place->kind == KEST_IR_PLACE_RUN) {
            mark_frame(modes, slots, place->slot,
                       (uint32_t)place->count * place->stride);
        }
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind == KEST_IR_SEEK_FROM || op->kind == KEST_IR_SEEK_NEXT) {
            mark_frame(modes, slots, op->imm[1], 1);
        } else if (op->kind == KEST_IR_REGION_OPEN ||
                   op->kind == KEST_IR_REGION_CLOSE) {
            mark_frame(modes, slots, op->imm[0], 1);
        }
    }
    return modes;
}

static bool carried_here(const Walk *walk, uint32_t which) {
    const KestModule *module = walk->c->module;
    uint32_t caller = (uint32_t)(walk->into - walk->c->bodies);
    // One this backend did not write is handed to the machine, which gives it
    // a frame of its own. A carried body is written before its caller.
    if (module == NULL || caller >= module->count || which >= caller ||
        !walk->c->bodies[which].written) {
        return false;
    }
    const KestChunk *chunk = module->functions[caller];
    for (uint16_t i = 0; chunk != NULL && i < chunk->carried_count; i++) {
        if (chunk->carried[i] == which) {
            return true;
        }
    }
    return false;
}

static void cannot(Walk *walk, const char *why) {
    if (walk->why == NULL) {
        walk->why = why;
    }
}

// How much of the stack an operation reads and leaves. The resolved form makes
// a value once and reads it once, innermost first, so what an operation reads
// is what is on top: this is that rule read off the operation rather than
// worked out again. `meet` is the one that does not follow it -- two ways of
// arriving at one value read two values and leave one, and only one of the two
// ever ran -- so it is answered here rather than counted.
static void moves(const KestIrBody *body, const KestIrOp *op, uint32_t *reads,
                  uint32_t *leaves) {
    uint32_t wide =
        op->dest == KEST_IR_NONE ? 0 : body->values[op->dest].slots;
    if (op->kind == KEST_IR_MEET) {
        *reads = wide;
        *leaves = wide;
        return;
    }
    uint32_t taken = 0;
    for (uint16_t a = 0; a < op->arg_count; a++) {
        taken += body->values[body->args[op->first_arg + a]].slots;
    }
    *reads = taken;
    *leaves = wide;
}

// One way of getting to an operation, with what it leaves behind it. Every
// operand is a named place in C here, so where a value sits has to be the same
// whichever way the program arrived -- which the machine does not need, an
// instruction working from the top of the stack wherever it is.
//
// A way that arrives with less than another way leaves is a way the program
// cannot take. It is the guard of the last arm of a `match`: the checker
// proved the arms cover every case, so the edge where none of them matched is
// one nothing reaches, and the machine would read a slot nothing wrote if it
// ever did. That is allowed where the arms meet and nowhere else, and what
// falls short is written as a stop rather than as a jump into a value that is
// not there.
static bool arrives(Walk *walk, uint32_t at, uint32_t depth) {
    if (walk->known[at] && walk->depth[at] != depth) {
        // And only where two ways of arriving at one value meet, which is the
        // one place the walk that wrote the body puts an edge nothing takes.
        // Anywhere else, two depths are this backend and that walk
        // disagreeing about what an operation does, which is worth saying
        // rather than working around.
        if (walk->body->ops[at].kind != KEST_IR_MEET) {
            snprintf(walk->said, sizeof(walk->said),
                     "operation %u is reached with %u and with %u on the "
                     "stack",
                     at, walk->depth[at], depth);
            cannot(walk, walk->said);
            return false;
        }
        // The deepest way there is what the operation is written for, and the
        // ways that fall short of it are written as stops.
        if (depth > walk->depth[at]) {
            walk->depth[at] = depth;
        }
        return true;
    }
    walk->known[at] = true;
    walk->depth[at] = depth;
    return true;
}

// Which operations a branch lands on, and how deep the stack is at each. A
// body whose two ways to one operation leave different amounts behind is one
// this backend refuses rather than guesses about: every operand is a named
// place in C, and two arms that disagree about where a value sits are two arms
// writing to two places.
static bool depths(Walk *walk) {
    const KestIrBody *body = walk->body;
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target < body->op_count) {
            walk->landed[op->target] = true;
        }
    }
    walk->known[0] = body->op_count > 0;
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (!walk->known[i]) {
            continue;
        }
        const KestIrOp *op = &body->ops[i];
        uint32_t reads = 0;
        uint32_t leaves = 0;
        moves(body, op, &reads, &leaves);
        if (reads > walk->depth[i]) {
            cannot(walk, "an operation reading more than the body has made");
            return false;
        }
        uint32_t after = walk->depth[i] - reads + leaves;
        if (after > walk->deepest) {
            walk->deepest = after;
        }
        uint32_t next = i + 1;
        uint32_t lands = op->target;
        bool goes_on = op->kind != KEST_IR_GO && op->kind != KEST_IR_GIVE;
        bool branches = op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
                        op->kind == KEST_IR_NEXT;
        if (goes_on && next < body->op_count && !arrives(walk, next, after)) {
            return false;
        }
        if (branches && lands < body->op_count) {
            // A branch backwards lands where the walk has been, which is a
            // loop: what it leaves has to be what was there, or the stack
            // grows a little every time round.
            if (lands <= i && walk->depth[lands] != after) {
                snprintf(walk->said, sizeof(walk->said),
                         "a loop landing on operation %u with %u rather than "
                         "%u on the stack",
                         lands, after, walk->depth[lands]);
                cannot(walk, walk->said);
                return false;
            }
            if (!arrives(walk, lands, after)) {
                return false;
            }
        }
    }
    // What is landed on is what a branch the walk reached lands on. A branch
    // after a `return` is one nothing reaches -- every arm of a `match` whose
    // arms all return has one -- and what only such branches land on is not
    // written, and neither are they, so it is not a label; one that was would
    // be a label nothing jumps to. See D1206.
    for (uint32_t i = 0; i < body->op_count; i++) {
        walk->landed[i] = false;
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (walk->known[i] &&
            (op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target < body->op_count) {
            walk->landed[op->target] = true;
        }
    }
    // A branch landing where nothing arrives from above is a label this cannot
    // write, because what the stack holds there was never worked out.
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (walk->landed[i] && !walk->known[i]) {
            cannot(walk, "a branch landing where nothing reaches");
            return false;
        }
    }
    return true;
}

// Whether nothing this body does can reach the heap. It is what says a handle
// may sit in a C local: the collector walks the machine's stack for roots and
// a local is not on it, so a body holding a handle across anything that can
// allocate is a body whose handle can go out from under it. A body that
// cannot allocate at all cannot be in the middle of a collection, so there is
// nothing to see. What a call reaches is read off the callee's declaration,
// which the module carries before any body is compiled. See D1095.
static bool reaches_no_heap(const KestEmitC *c, const KestIrBody *body) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->effects & (KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_HOST |
                            KEST_IR_EFFECT_MOVES)) != 0) {
            return false;
        }
        if (op->kind == KEST_IR_CALL_VALUE || op->kind == KEST_IR_CALL_HOST) {
            return false;
        }
        if (op->kind != KEST_IR_CALL) {
            continue;
        }
        if (c->module == NULL || op->imm[0] >= c->module->count ||
            !c->module->functions[op->imm[0]]->no_alloc) {
            return false;
        }
    }
    return true;
}

// Whether a body makes a call at all, which is whether it wants the ledger
// read at the top of it. Asked before anything is written, because the
// reading goes in the preamble and what is in the body is not known until
// after. See D1122.
static bool calls_anything(const KestIrBody *body) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (body->ops[i].kind == KEST_IR_CALL) {
            return true;
        }
    }
    return false;
}

// Where an operand sits, written the way the file writes it. Small buffers
// rather than one string built up, because every line below names two or three
// of these and a shared one would name the last of them three times.
typedef char Where[24];

static void at_stack(const Walk *walk, Where into, uint32_t slot) {
    snprintf(into, sizeof(Where), walk->in_memory ? "sf[%u]" : "s[%u]", slot);
}

// Where a body on the machine's stack keeps a slot. The collector walks the
// machine's stack and nothing else, so a slot that may hold something it has
// to see is kept there -- but a byte written anywhere through a pointer may,
// as far as the host's compiler can tell, be one of the machine's slots, so a
// slot kept there is read again after every element written. A slot that
// holds nothing the collector looks for is kept in an array of the body's
// own, which the host's compiler may hold in registers. Keeping every slot in
// both, written through, was tried and bought nothing: what the reads saved,
// the writes cost. See D1162.
static void at_frame(const Walk *walk, Where into, uint32_t slot) {
    snprintf(into, sizeof(Where),
             walk->slot_modes != NULL && walk->slot_modes[slot] == SLOT_LOOSE
                 ? "g[%u]"
                 : "f[%u]",
             slot);
}

// One scalar moved between memory and slots: the same switch the machine runs
// over a piece, written out at the width the piece is, which a C compiler
// turns into one load or one store.
static void move_one(Walk *walk, uint8_t kind, uint32_t slot, uint32_t byte,
                     bool reading) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    Where held;
    Where beside;
    at_stack(walk, held, slot);
    const char *width = NULL;
    bool real = false;
    switch (kind) {
    case KEST_L_TEXT:
        // Two slots: what it is made of and how many bytes that is. The bytes
        // are the heap's and are carried rather than copied.
        at_stack(walk, beside, slot + 1);
        if (reading) {
            say(c, out,
                "        memcpy(&%s, at + %u, 8);\n"
                "        {\n            uint64_t many;\n"
                "            memcpy(&many, at + %u, 8);\n"
                "            %s.integer = (int64_t)many;\n        }\n",
                held, byte, byte + 8, beside);
            return;
        }
        say(c, out,
            "        memcpy(at + %u, &%s, 8);\n"
            "        {\n            uint64_t many = (uint64_t)%s.integer;\n"
            "            memcpy(at + %u, &many, 8);\n        }\n",
            byte, held, beside, byte + 8);
        return;
    case KEST_L_I8:
        width = "int8_t";
        break;
    case KEST_L_I16:
        width = "int16_t";
        break;
    case KEST_L_I32:
        width = "int32_t";
        break;
    case KEST_L_U8:
    case KEST_L_FLAGS8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
        width = "uint8_t";
        break;
    case KEST_L_U16:
    case KEST_L_FLAGS16:
        width = "uint16_t";
        break;
    case KEST_L_U32:
    case KEST_L_FLAGS32:
        width = "uint32_t";
        break;
    case KEST_L_F32:
        width = "float";
        real = true;
        break;
    case KEST_L_F64:
        width = "double";
        real = true;
        break;
    default:
        break;
    }
    if (width == NULL) {
        // A whole slot either way, which is what the machine moves for
        // everything it has no narrower name for.
        if (reading) {
            say(c, out, "        memcpy(&%s, at + %u, 8);\n", held, byte);
        } else {
            say(c, out, "        memcpy(at + %u, &%s, 8);\n", byte, held);
        }
        return;
    }
    if (reading) {
        say(c, out,
            "        {\n            %s piece;\n"
            "            memcpy(&piece, at + %u, sizeof piece);\n"
            "            %s.%s = piece;\n        }\n",
            width, byte, held, real ? "real" : "integer");
        return;
    }
    say(c, out,
        "        {\n            %s piece = (%s)%s.%s;\n"
        "            memcpy(at + %u, &piece, sizeof piece);\n        }\n",
        width, width, held, real ? "real" : "integer", byte);
}

// A whole value moved between memory and slots, by walking the type rather
// than the flat list of pieces a layout carries. The machine has both walks
// and picks between them -- a run of pieces for anything with no tag in it,
// and the type itself for anything with one, because which slots a payload
// fills is what the tag says (D710). Here there is one walk, because a walk
// done while compiling costs nothing at either end and a tag is a `switch`
// the host's compiler can see through. What the machine does with a loop over
// a layout for every element is a run of moves here, and that loop is a third
// of `bench/rules.kest`. See D1028 and D1096.
//
// Answers how many slots it moved. `where` is where in the source this is,
// for the one thing reading a value can refuse: a tag that names no case.
static uint16_t move_value(Walk *walk, const KestType *type, uint32_t slot,
                           uint32_t byte, bool reading, uint32_t where) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    if (type == NULL) {
        move_one(walk, KEST_L_WORD, slot, byte, reading);
        return 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->member_count; i++) {
            used = (uint16_t)(used +
                              move_value(walk, type->members[i].type,
                                         slot + used,
                                         byte + type->members[i].byte_offset,
                                         reading, where));
        }
        return used;
    }
    if (type->tag == KEST_T_FIXED) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->count; i++) {
            used = (uint16_t)(used +
                              move_value(walk, type->element, slot + used,
                                         byte + i * type->element->byte_size,
                                         reading, where));
        }
        return used;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        uint16_t used = move_value(walk, type->element, slot, byte, reading,
                                   where);
        move_one(walk, KEST_L_HELD, slot + used,
                 byte + type->element->byte_size, reading);
        return (uint16_t)(used + 1);
    }
    if (type->tag != KEST_T_ENUM) {
        move_one(walk, kest_scalar_of(type), slot, byte, reading);
        return type->tag == KEST_T_TEXT ? 2 : 1;
    }
    Where tag;
    at_stack(walk, tag, slot);
    // Every case that carries something carrying the same pieces in the same
    // places, each one slot wide: the tag's `Task` and `Kind` in
    // `bench/rules.kest`, whose cases are a number or nothing. Then the pieces
    // are moved whatever the tag, and which cases carry them is a mask the tag
    // reads rather than a `switch` it jumps through -- a jump nothing can
    // guess where the tag is different from one element to the next. What a
    // case does not carry is nought either way, and a tag with no case behind
    // it is refused reading and writes nothing but itself, as the `switch`
    // did. See D1232.
    const KestVariantType *carrying = NULL;
    uint64_t carries = 0;
    bool uniform = type->case_count > 0 && type->case_count <= 63;
    for (uint32_t which = 0; uniform && which < type->case_count; which++) {
        const KestVariantType *variant = &type->cases[which];
        if (variant->payload_count == 0) {
            continue;
        }
        carries |= UINT64_C(1) << which;
        if (carrying == NULL) {
            carrying = variant;
            for (uint32_t piece = 0; piece < variant->payload_count; piece++) {
                const KestType *held = variant->payload[piece];
                uniform = uniform && held != NULL &&
                          held->tag != KEST_T_STRUCT &&
                          held->tag != KEST_T_FIXED &&
                          held->tag != KEST_T_OPTIONAL &&
                          held->tag != KEST_T_ENUM &&
                          held->tag != KEST_T_TEXT && held->slots <= 1;
            }
            continue;
        }
        uniform = uniform && variant->payload_count == carrying->payload_count;
        for (uint32_t piece = 0; uniform && piece < variant->payload_count;
             piece++) {
            uniform = variant->payload[piece] == carrying->payload[piece] &&
                      variant->offsets[piece] == carrying->offsets[piece] &&
                      variant->byte_offsets[piece] ==
                          carrying->byte_offsets[piece];
        }
    }
    if (uniform && carrying != NULL) {
        const char *written = kest_type_written(type);
        if (reading) {
            say(c, out,
                "        {\n            int32_t tag;\n"
                "            memcpy(&tag, at + %u, 4);\n"
                "            %s.integer = tag;\n"
                "            if ((uint32_t)tag >= %uu) {\n"
                "                char said[96];\n"
                "                snprintf(said, sizeof said,\n"
                "                         \"`%s` here holds tag %%lld and has "
                "no such case\",\n"
                "                         (long long)tag);\n"
                "                return kest_native_stopped(rt, %u, "
                "\"K0651\", said);\n"
                "            }\n",
                byte, tag, (unsigned)type->case_count,
                written == NULL ? "a value with a tag in it" : written,
                where);
        } else {
            say(c, out,
                "        memset(at + %u, 0, %u);\n"
                "        {\n            int32_t tag = (int32_t)%s.integer;\n"
                "            memcpy(at + %u, &tag, 4);\n",
                byte, (unsigned)type->byte_size, tag, byte);
        }
        say(c, out,
            "            bool carried = (uint32_t)tag < %uu && "
            "((UINT64_C(0x%llx) >> tag) & 1u) != 0;\n",
            (unsigned)type->case_count, (unsigned long long)carries);
        bool filled[256] = {false};
        for (uint32_t piece = 0; piece < carrying->payload_count; piece++) {
            uint32_t at = slot + carrying->offsets[piece];
            Where held;
            at_stack(walk, held, at);
            if (!reading) {
                say(c, out, "            %s.integer = carried ? %s.integer : 0;\n",
                    held, held);
            }
            move_one(walk, kest_scalar_of(carrying->payload[piece]), at,
                     byte + carrying->byte_offsets[piece], reading);
            if (reading) {
                say(c, out, "            %s.integer = carried ? %s.integer : 0;\n",
                    held, held);
            }
            if (carrying->offsets[piece] < 256) {
                filled[carrying->offsets[piece]] = true;
            }
        }
        if (reading) {
            for (uint16_t piece = 1; piece < type->slots; piece++) {
                if (piece < 256 && filled[piece]) {
                    continue;
                }
                Where empty;
                at_stack(walk, empty, slot + piece);
                say(c, out, "            %s.integer = 0;\n", empty);
            }
        }
        say(c, out, "        }\n");
        return type->slots;
    }
    if (!reading) {
        // What the case does not carry is written as nought, because a tag
        // says which reading the bytes beside it have and a case written over
        // a wider one would otherwise leave the wider one's fields under the
        // new tag. See D711.
        say(c, out,
            "        memset(at + %u, 0, %u);\n"
            "        {\n            int32_t tag = (int32_t)%s.integer;\n"
            "            memcpy(at + %u, &tag, 4);\n"
            "            switch (tag) {\n",
            byte, (unsigned)type->byte_size, tag, byte);
    } else {
        say(c, out,
            "        {\n            int32_t tag;\n"
            "            memcpy(&tag, at + %u, 4);\n"
            "            %s.integer = tag;\n",
            byte, tag);
        for (uint16_t piece = 1; piece < type->slots; piece++) {
            Where empty;
            at_stack(walk, empty, slot + piece);
            say(c, out, "            %s.integer = 0;\n", empty);
        }
        say(c, out, "            switch (tag) {\n");
    }
    for (uint32_t which = 0; which < type->case_count; which++) {
        const KestVariantType *variant = &type->cases[which];
        say(c, out, "            case %u:\n", which);
        for (uint32_t piece = 0; piece < variant->payload_count; piece++) {
            move_value(walk, variant->payload[piece],
                       slot + variant->offsets[piece],
                       byte + variant->byte_offsets[piece], reading, where);
        }
        say(c, out, "                break;\n");
    }
    if (!reading) {
        // Writing one does not refuse a tag with no case behind it: the tag
        // goes down and nothing else does, which is what the machine writes.
        say(c, out,
            "            default:\n                break;\n"
            "            }\n        }\n");
        return type->slots;
    }
    const char *written = kest_type_written(type);
    say(c, out,
        "            default: {\n                char said[96];\n"
        "                snprintf(said, sizeof said,\n"
        "                         \"`%s` here holds tag %%lld and has no such "
        "case\",\n"
        "                         (long long)tag);\n"
        "                return kest_native_stopped(rt, %u, \"K0651\", "
        "said);\n"
        "            }\n            }\n        }\n",
        written == NULL ? "a value with a tag in it" : written, where);
    return type->slots;
}


// What arithmetic is in C, by what it answers. Three `%s`: where the answer
// goes and the two it is worked out from, in that order, and the answer's
// place is the left operand's -- which is what makes a binary operation one
// line whatever it is.
//
// Every one of these is what `vm.c` runs, written the same way round: whole
// numbers wrap through unsigned because signed overflow in C is undefined
// (D667), and a narrow float is worked out at its own width and kept in a
// slot as a double.
static const char *binary_c(uint16_t kind, const KestType *type) {
    bool real = kest_is_float(type);
    bool narrow = kest_is_narrow(type);
    bool without_sign = kest_is_unsigned(type);
    switch (kind) {
    case KEST_IR_ADD:
        return real ? (narrow ? "%s.real = (double)((float)%s.real + (float)%s.real);"
                              : "%s.real = %s.real + %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer + (uint64_t)%s.integer);";
    case KEST_IR_SUB:
        return real ? (narrow ? "%s.real = (double)((float)%s.real - (float)%s.real);"
                              : "%s.real = %s.real - %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer - (uint64_t)%s.integer);";
    case KEST_IR_MUL:
        return real ? (narrow ? "%s.real = (double)((float)%s.real * (float)%s.real);"
                              : "%s.real = %s.real * %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer * (uint64_t)%s.integer);";
    case KEST_IR_DIV:
        if (real) {
            return narrow ? "%s.real = (double)((float)%s.real / (float)%s.real);"
                          : "%s.real = %s.real / %s.real;";
        }
        return without_sign
                   ? "%s.integer = (int64_t)((uint64_t)%s.integer / (uint64_t)%s.integer);"
                   : NULL;
    case KEST_IR_MOD:
        if (real) {
            return NULL;
        }
        return without_sign
                   ? "%s.integer = (int64_t)((uint64_t)%s.integer %% (uint64_t)%s.integer);"
                   : NULL;
    case KEST_IR_AND:
        return "%s.integer = %s.integer & %s.integer;";
    case KEST_IR_OR:
        return "%s.integer = %s.integer | %s.integer;";
    case KEST_IR_XOR:
        return "%s.integer = %s.integer ^ %s.integer;";
    case KEST_IR_LT:
        return real            ? "%s.integer = (%s.real < %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer < (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer < %s.integer);";
    case KEST_IR_LE:
        return real            ? "%s.integer = (%s.real <= %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer <= (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer <= %s.integer);";
    case KEST_IR_GT:
        return real            ? "%s.integer = (%s.real > %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer > (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer > %s.integer);";
    case KEST_IR_GE:
        return real            ? "%s.integer = (%s.real >= %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer >= (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer >= %s.integer);";
    // Equality asks nothing about a sign: the same bits are the same bits
    // either way, which is what `lower` reads out of its own table.
    case KEST_IR_EQ:
        return real ? "%s.integer = (%s.real == %s.real);" : "%s.integer = (%s.integer == %s.integer);";
    case KEST_IR_NE:
        return real ? "%s.integer = (%s.real != %s.real);" : "%s.integer = (%s.integer != %s.integer);";
    default:
        return NULL;
    }
}

// The cut to a declared width, which is how every whole number is kept in a
// slot. The same six `kest_narrow_to` makes, written as the casts they are so
// that the host's compiler can see through them.
static const char *narrow_c(uint16_t scalar) {
    switch (scalar) {
    case KEST_L_I8:
        return "%s.integer = (int64_t)(int8_t)%s.integer;";
    case KEST_L_I16:
        return "%s.integer = (int64_t)(int16_t)%s.integer;";
    case KEST_L_I32:
        return "%s.integer = (int64_t)(int32_t)%s.integer;";
    case KEST_L_BOOL:
    case KEST_L_U8:
        return "%s.integer = (int64_t)(uint8_t)%s.integer;";
    case KEST_L_U16:
        return "%s.integer = (int64_t)(uint16_t)%s.integer;";
    case KEST_L_U32:
        return "%s.integer = (int64_t)(uint32_t)%s.integer;";
    default:
        return NULL;
    }
}

// A run of constants written into the program, read at an index worked out
// while it runs: a table of tiers, a curve, a list of names. The machine
// keeps them in the chunk and reads them at the index; this writes them out
// once at the top of the file and reads them the same way. See D1119.
static void write_const_at(Walk *walk, const KestIrOp *op, uint32_t index) {
    KestEmitC *c = walk->c;
    const KestIrBody *body = walk->body;
    uint32_t first = op->imm[0];
    uint32_t stride = op->imm[1];
    uint32_t count = op->imm[2];
    uint32_t many = stride * count;
    if (stride == 0 || count == 0 ||
        (size_t)first + many > body->constant_count) {
        cannot(walk, "a run of values the body has not got");
        return;
    }
    Text *out = &walk->into->ahead;
    say(c, out, "static const KV kr_%u_%u[] = {\n", c->count - 1, index);
    for (uint32_t k = 0; k < many; k++) {
        KestValue value = body->constants[first + k];
        uint8_t class = body->constant_classes[first + k];
        if (class == KEST_CONST_TEXT) {
            if (k + 1 >= many ||
                body->constant_classes[first + k + 1] != KEST_CONST_INT) {
                cannot(walk, "a piece of text with no length beside it");
                return;
            }
            int64_t wide = body->constants[first + k + 1].integer;
            if (value.text == NULL || wide < 0) {
                cannot(walk, "a piece of text that is nowhere");
                return;
            }
            say(c, out, "    { .text = \"");
            for (int64_t byte = 0; byte < wide; byte++) {
                say(c, out, "\\x%02x",
                    (unsigned)(unsigned char)value.text[byte]);
            }
            say(c, out, "\" },\n");
            continue;
        }
        if (class == KEST_CONST_FLOAT) {
            // The bits trick the single values use cannot be written into an
            // initialiser, and an infinity has no literal, so a run holding
            // one is a run this leaves alone.
            if (value.real != value.real ||
                value.real - value.real != 0.0) {
                cannot(walk, "a number with no spelling in C");
                return;
            }
            say(c, out, "    { .real = %a },\n", value.real);
            continue;
        }
        if (class != KEST_CONST_INT) {
            cannot(walk, "a value this backend has no spelling for");
            return;
        }
        say(c, out, "    { .integer = (int64_t)UINT64_C(0x%016llx) },\n",
            (unsigned long long)(uint64_t)value.integer);
    }
    say(c, out, "};\n");
    Where held;
    at_stack(walk, held, walk->stack - 1);
    say(c, &walk->into->wrote,
        "    {\n        int64_t which = %s.integer;\n"
        "        if (!kest_run_at(rt, which, %u, %u)) {\n"
        "            return false;\n        }\n",
        held, count, op->span.offset);
    for (uint32_t k = 0; k < stride; k++) {
        Where into;
        at_stack(walk, into, walk->stack - 1 + k);
        say(c, &walk->into->wrote,
            "        %s = kr_%u_%u[which * %u + %u];\n", into, c->count - 1,
            index, stride, k);
    }
    say(c, &walk->into->wrote, "    }\n");
}

static void write_const(Walk *walk, const KestIrOp *op) {
    const KestIrBody *body = walk->body;
    uint32_t first = op->imm[0];
    uint32_t count = op->imm[1];
    if ((size_t)first + count > body->constant_count) {
        cannot(walk, "a value the body has not got");
        return;
    }
    for (uint32_t k = 0; k < count; k++) {
        KestValue value = body->constants[first + k];
        // What the slot is, which is what a chunk says about its own
        // constants rather than what a layout says about a type: three
        // things, because what a reader of a chunk needs is to tell a number
        // from a float from the bytes of a piece of text.
        uint8_t class = body->constant_classes[first + k];
        Where into;
        at_stack(walk, into, walk->stack + k);
        // The bytes of a piece of text, written down as bytes. What the
        // constant holds is where they are in the process that compiled it,
        // which means nothing in another process -- so what goes into the
        // file is the bytes themselves, and the file's own copy of them lasts
        // as long as the program does, which is what a constant is. Every one
        // written as its number, because a byte that reads as the beginning
        // of the next escape is how a string ends up meaning something else.
        if (class == KEST_CONST_TEXT) {
            if (k + 1 >= count ||
                body->constant_classes[first + k + 1] != KEST_CONST_INT) {
                cannot(walk, "a piece of text with no length beside it");
                return;
            }
            int64_t many = body->constants[first + k + 1].integer;
            if (value.text == NULL || many < 0) {
                cannot(walk, "a piece of text that is nowhere");
                return;
            }
            say(walk->c, &walk->into->wrote, "    %s.text = \"", into);
            for (int64_t byte = 0; byte < many; byte++) {
                say(walk->c, &walk->into->wrote, "\\x%02x",
                    (unsigned)(unsigned char)value.text[byte]);
            }
            say(walk->c, &walk->into->wrote, "\";\n");
            continue;
        }
        if (class == KEST_CONST_FLOAT) {
            // Written as hexadecimal, which is the one spelling of a double
            // that reads back as the bits it was written from. What has no
            // such spelling -- an infinity, or what is not a number -- is a
            // body this backend leaves alone rather than one it writes a
            // number for that means something else.
            if (value.real != value.real ||
                value.real - value.real != 0.0) {
                // An infinity and a not-a-number have no spelling C reads
                // back -- `%a` writes `inf` and `nan`, which are not
                // literals -- so what is written is the bits and a copy into
                // the slot. Every double has those, and the copy is what the
                // machine does with one anyway. See D1119.
                uint64_t bits = 0;
                memcpy(&bits, &value.real, sizeof bits);
                say(walk->c, &walk->into->wrote,
                    "    {\n        uint64_t bits = UINT64_C(0x%016llx);\n"
                    "        memcpy(&%s.real, &bits, sizeof bits);\n    }\n",
                    (unsigned long long)bits, into);
                continue;
            }
            say(walk->c, &walk->into->wrote, "    %s.real = %a;\n", into,
                value.real);
            continue;
        }
        if (class != KEST_CONST_INT) {
            cannot(walk, "a value this backend has no spelling for");
            return;
        }
        say(walk->c, &walk->into->wrote,
            "    %s.integer = (int64_t)UINT64_C(0x%016llx);\n", into,
            (unsigned long long)(uint64_t)value.integer);
    }
}

// One of an array, read into slots or written out of them. The two things
// the machine asks before it touches one -- that the handle is an array and
// that the index is inside it -- are a call, because they are the same two
// questions however wide an element is; the moving is written out, because
// which piece sits where is known while compiling. See D1095.
static bool write_elem(Walk *walk, const KestIrOp *op,
                       const KestIrPlace *place, uint32_t handle,
                       uint32_t value, bool reading) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    if (c->module == NULL || place->layout >= c->module->layout_count) {
        cannot(walk, "an element of a shape this module has not laid out");
        return false;
    }
    const KestLayout *layout = &c->module->layouts[place->layout];
    if (layout->type == NULL || layout->slots != place->slots) {
        cannot(walk, "an element read at a width the layout does not have");
        return false;
    }
    Where held;
    Where index;
    at_stack(walk, held, handle);
    at_stack(walk, index, handle + 1);
    // Where the element is, worked out here rather than asked for. A run of
    // elements is a shape the header says (D1112), so the three things that
    // have to be true -- that the handle is a run, that the index is one of
    // them, and where one of them sits -- are four lines of C rather than a
    // call. Anything the test does not like goes through `kest_elem_at`,
    // which is the machine's own answer and the machine's own words, so a
    // refusal here is the refusal there.
    //
    // This used to say the host's compiler sees through it: that it hoists
    // the length out of a loop and keeps the block in a register. Measured,
    // it does not, and the reason is in the shape of a frame rather than in
    // gcc: a loop that reads an element, calls a body and writes one back has
    // an opaque call between the reads, and a call may allocate, so nothing
    // about the run survives it as far as the C compiler is concerned. On
    // `bench/control.kest` the whole guard is 49.1 instructions a decision of
    // the 141.7 between this backend and `g++` -- 25.1 of it the two tests
    // asking what the handle is and 14.6 the bounds.
    //
    // The two that ask what the handle is are invariant and could be hoisted
    // by something that knew more than gcc does: this heap is non-moving, so
    // a header that was a run stays a run at the same address for as long as
    // the slot holds it. What would have to be proved is that the slot is not
    // written between the uses. The bounds are not invariant -- a `push` may
    // move the bytes and change the length -- and are the part that is
    // genuinely per-access. See D1141 and D1142.
    //
    // Where the compiler proved the element is inside the array -- one a
    // walk counts through, where nothing in the walk makes the array shorter
    // or names another -- neither question is asked. See D1187.
    // And where it is inside the array whenever the array is as long as the
    // walk's limit, which was asked where the walk began: one flag held in a
    // register rather than three things read out of memory. See D1189.
    if (place->guarded && !place->in_bounds) {
        say(c, out,
            "    {\n        const KestRun *run = (const KestRun *)%s.object;\n"
            "        int64_t which = %s.integer;\n"
            "        unsigned char *at;\n"
            "        if (fast_%u_%u_%u) {\n"
            "            at = run->bytes + (size_t)which * run->stride + %u;\n"
            "        } else if (run != NULL && run->what == KEST_RUN_IS &&\n"
            "            (uint64_t)which < (uint64_t)run->length) {\n"
            "            at = run->bytes + (size_t)which * run->stride + %u;\n"
            "        } else {\n"
            "            at = kest_elem_at(rt, %s, which, %u, %u);\n"
            "            if (at == NULL) {\n                return false;\n"
            "            }\n        }\n",
            held, index, (unsigned)place->guard_held,
            (unsigned)place->guard_counter, (unsigned)place->guard_limit,
            (unsigned)place->offset, (unsigned)place->offset, held,
            (unsigned)place->offset, op->span.offset);
    } else if (place->in_bounds) {
        say(c, out,
            "    {\n        const KestRun *run = (const KestRun *)%s.object;\n"
            "        unsigned char *at = run->bytes +\n"
            "            (size_t)%s.integer * run->stride + %u;\n",
            held, index, (unsigned)place->offset);
    } else {
        say(c, out,
            "    {\n        const KestRun *run = (const KestRun *)%s.object;\n"
            "        int64_t which = %s.integer;\n"
            "        unsigned char *at;\n"
            "        if (run != NULL && run->what == KEST_RUN_IS &&\n"
            "            (uint64_t)which < (uint64_t)run->length) {\n"
            "            at = run->bytes + (size_t)which * run->stride + %u;\n"
            "        } else {\n"
            "            at = kest_elem_at(rt, %s, which, %u, %u);\n"
            "            if (at == NULL) {\n                return false;\n"
            "            }\n        }\n",
            held, index, (unsigned)place->offset, held,
            (unsigned)place->offset, op->span.offset);
    }
    uint16_t moved = move_value(walk, layout->type, value, 0, reading,
                                op->span.offset);
    say(c, out, "    }\n");
    if (moved != layout->slots) {
        cannot(walk, "an element whose type and layout say different widths");
        return false;
    }
    return true;
}

// A field read through an address that was worked out before it. The address
// is the program's to hold -- an element of an array, and nothing moves under
// it while it is held, which is what D931 and D996 make true -- so there is
// nothing to check here and nothing to call.
static bool write_at(Walk *walk, const KestIrPlace *place, uint32_t value,
                     uint32_t address) {
    KestEmitC *c = walk->c;
    if (c->module == NULL || place->layout >= c->module->layout_count) {
        cannot(walk, "a field of a shape this module has not laid out");
        return false;
    }
    const KestLayout *layout = &c->module->layouts[place->layout];
    if (layout->type == NULL || layout->slots != place->slots) {
        cannot(walk, "a field read at a width the layout does not have");
        return false;
    }
    Where held;
    at_stack(walk, held, address);
    say(c, &walk->into->wrote,
        "    {\n        unsigned char *at = (unsigned char *)%s.object + %u;\n",
        held, (unsigned)place->offset);
    uint16_t moved = move_value(walk, layout->type, value, 0, true,
                                walk->body->ops[0].span.offset);
    say(c, &walk->into->wrote, "    }\n");
    if (moved != layout->slots) {
        cannot(walk, "a field whose type and layout say different widths");
        return false;
    }
    return true;
}

// Where a branch goes: the operation it lands on, or nowhere when this way
// there leaves less than the operation is written for. The second is the edge
// no program takes, which `arrives` explains.
static void write_branch(Walk *walk, uint32_t target, uint32_t leaving,
                         uint32_t where) {
    if (target < walk->body->op_count && walk->known[target] &&
        walk->depth[target] > leaving) {
        // Said in the machine's own words and with the machine's own code for
        // it: what got here is this project being wrong about its own
        // program, which is what `K0655` is.
        say(walk->c, &walk->into->wrote,
            "return kest_native_stopped(rt, %u, \"K0655\",\n"
            "            \"no way to this operation left a value for it\");\n",
            where);
        return;
    }
    say(walk->c, &walk->into->wrote, "goto L%u;\n", target);
}

// One operation, as the C it does. `walk->stack` is where the top of the
// operand stack is before it and is moved by it, which is the whole of the
// bookkeeping: every operand is a place in one array with a number worked out
// while compiling, so the host's compiler sees plain locals rather than a
// stack it has to follow.
// Whether an operation is written on the machine's stack rather than on the
// body's own operands. A body that can reach the heap keeps its operands in a
// C array of its own, which the host's compiler may hold in registers because
// nothing else can reach it, and writes them where the collector walks -- the
// machine's stack, at the same place the machine would keep them -- before an
// operation that can call into the library with one of their addresses or
// reach the heap, and reads them back after it. The rest are arithmetic,
// comparisons of numbers, locals, elements read and written where they are,
// and branches, none of which does either. Operands kept on the machine's
// stack throughout were stored and read again around every element written,
// because a byte written through a pointer may be one of them as far as the
// host's compiler can tell. See D1179.
// The walks' own questions, asked once where each walk begins: for every
// array an element of which is guarded by a walk's limit (D1189), whether the
// array held in that slot is a run at least as long as the limit. Declared at
// the top of the body, because two walks after one another may count in the
// same slots and ask the same question, and set before the label a walk goes
// back to, so it is asked on the way in and not at every turn.
static bool same_guard(const KestIrPlace *a, const KestIrPlace *b) {
    return a->guard_held == b->guard_held &&
           a->guard_counter == b->guard_counter &&
           a->guard_limit == b->guard_limit;
}

static bool guard_seen_before(const KestIrBody *body, uint32_t which) {
    for (uint32_t p = 0; p < which; p++) {
        if (body->places[p].guarded && !body->places[p].in_bounds &&
            same_guard(&body->places[p], &body->places[which])) {
            return true;
        }
    }
    return false;
}

static void declare_guards(Walk *walk) {
    const KestIrBody *body = walk->body;
    for (uint32_t p = 0; p < body->place_count; p++) {
        const KestIrPlace *place = &body->places[p];
        if (!place->guarded || place->in_bounds || guard_seen_before(body, p)) {
            continue;
        }
        say(walk->c, &walk->into->wrote,
            "    bool fast_%u_%u_%u = false;\n    (void)fast_%u_%u_%u;\n",
            (unsigned)place->guard_held, (unsigned)place->guard_counter,
            (unsigned)place->guard_limit, (unsigned)place->guard_held,
            (unsigned)place->guard_counter, (unsigned)place->guard_limit);
    }
}

static void ask_guards(Walk *walk, uint32_t head) {
    const KestIrBody *body = walk->body;
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *next = &body->ops[i];
        if (next->kind != KEST_IR_NEXT || next->target != head) {
            continue;
        }
        for (uint32_t p = 0; p < body->place_count; p++) {
            const KestIrPlace *place = &body->places[p];
            if (!place->guarded || place->in_bounds ||
                place->guard_counter != next->imm[0] ||
                place->guard_limit != next->imm[1] ||
                guard_seen_before(body, p)) {
                continue;
            }
            Where held;
            Where limit;
            at_frame(walk, held, place->guard_held);
            at_frame(walk, limit, place->guard_limit);
            say(walk->c, &walk->into->wrote,
                "    {\n        const KestRun *run = (const KestRun *)%s.object;\n"
                "        fast_%u_%u_%u = run != NULL && run->what == "
                "KEST_RUN_IS &&\n"
                "            (uint64_t)%s.integer <= (uint64_t)run->length;\n"
                "    }\n",
                held, (unsigned)place->guard_held,
                (unsigned)place->guard_counter, (unsigned)place->guard_limit,
                limit);
        }
    }
}

static bool in_memory(const Walk *walk, const KestIrOp *op) {
    if (!walk->on_the_stack) {
        return false;
    }
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
    case KEST_IR_CONST_AT:
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
    case KEST_IR_LOAD:
    case KEST_IR_PUT:
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
    case KEST_IR_PART:
    case KEST_IR_TURN:
    case KEST_IR_DROP:
    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_DIV:
    case KEST_IR_MOD:
    case KEST_IR_NEG:
    case KEST_IR_FLIP:
    case KEST_IR_SHL:
    case KEST_IR_SHR:
    case KEST_IR_NARROW:
    case KEST_IR_TO_FLOAT:
    case KEST_IR_TO_WHOLE:
    case KEST_IR_BITS:
    case KEST_IR_NOT:
    case KEST_IR_ADDR:
    case KEST_IR_LEN:
    case KEST_IR_TEXT_LEN:
    case KEST_IR_GO:
    case KEST_IR_ASK:
    case KEST_IR_NEXT:
    case KEST_IR_GIVE:
        return false;
    // Two runs of slots and two pieces of text are weighed by the library,
    // which is handed where they are.
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE:
        return kest_is_a_run(op->type) ||
               (op->type != NULL && op->type->tag == KEST_T_TEXT);
    default:
        return true;
    }
}

// The arguments of a call, left in the frame it hands over where the callee
// reads them there. A body on the machine's stack has them there already:
// its operands are the slots above its own, and the frame it hands over
// starts at the first argument. One keeping its operands in locals hands
// over room above its slots and writes them in, when `KA_` says the callee
// reads them there -- a body in locals reads them out of what it was called
// with, and writing them for it is a store nothing reads. See D1198.
static void leave_arguments(const Walk *walk, Text *out, uint32_t which,
                            uint32_t base, uint32_t reads) {
    if (walk->on_the_stack || reads == 0) {
        return;
    }
    say(walk->c, out, "        if (KA_%u) {\n", which);
    for (uint32_t k = 0; k < reads; k++) {
        Where one;
        at_stack(walk, one, base + k);
        say(walk->c, out, "            stands[%u] = %s;\n", k, one);
    }
    say(walk->c, out, "        }\n");
}

static void write_op(Walk *walk, uint32_t index, const KestIrOp *op) {
    const KestIrBody *body = walk->body;
    Text *out = &walk->into->wrote;
    KestEmitC *c = walk->c;
    uint32_t reads = 0;
    uint32_t leaves = 0;
    moves(body, op, &reads, &leaves);
    uint32_t base = walk->stack - reads;
    Where first;
    Where second;
    Where third;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
        write_const(walk, op);
        break;
    case KEST_IR_CONST_AT:
        // The index is read away and the run it names is left where it was,
        // which is what the machine does with the slot under it.
        if (reads != 1 || leaves != op->imm[1]) {
            cannot(walk, "a run of values read at something other than an "
                         "index");
            break;
        }
        write_const_at(walk, op, index);
        break;
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
        at_stack(walk, first, base);
        say(c, out, "    %s.integer = %d;\n", first,
            op->kind == KEST_IR_TRUE ? 1 : 0);
        break;
    case KEST_IR_LOAD: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_ELEM) {
            // The handle and the index are the two slots under what this
            // leaves, and whether they are read away is which of the two
            // element reads it is: one that consumes them is an index, and
            // one that does not is a place a write is coming to.
            uint32_t handle = reads == 0 ? walk->stack - 2 : base;
            if (!write_elem(walk, op, place, handle, base, true)) {
                break;
            }
            break;
        }
        if (place->kind == KEST_IR_PLACE_AT) {
            // An address worked out before this, and how far into what it
            // points at the field sits. The machine has an instruction that
            // does this and the element read before it in one (D1044); here
            // the two are two lines the host's compiler puts together itself.
            if (!write_at(walk, place, base, reads == 0 ? walk->stack - 1
                                                        : base)) {
                break;
            }
            break;
        }
        if (place->kind == KEST_IR_PLACE_RUN) {
            // One of a fixed run in the frame, at an index worked out while
            // it runs. The index is held aside because what comes back sits
            // where it was, and the machine's own sentence answers for an
            // index that is not one of them. See D1109.
            if (reads != 1 || leaves != place->stride) {
                cannot(walk, "one of a run read at something other than an "
                             "index");
                break;
            }
            at_stack(walk, first, base);
            say(c, out,
                "    {\n        int64_t which = %s.integer;\n"
                "        if (!kest_run_at(rt, which, %u, %u)) {\n"
                "            return false;\n        }\n",
                first, (unsigned)place->count, op->span.offset);
            for (uint16_t k = 0; k < place->stride; k++) {
                at_stack(walk, first, base + k);
                say(c, out,
                    "        %s = f[%u + which * %u];\n", first,
                    (unsigned)place->slot + k, (unsigned)place->stride);
            }
            say(c, out, "    }\n");
            break;
        }
        if (place->kind != KEST_IR_PLACE_SLOT) {
            cannot(walk, "a place that is not a run of the frame");
            break;
        }
        for (uint16_t k = 0; k < place->slots; k++) {
            at_stack(walk, first, base + k);
            at_frame(walk, second, (uint32_t)(place->slot + k));
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    case KEST_IR_PUT: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_ELEM) {
            // The handle, the index, and then what is being written, which is
            // what the machine pops in that order.
            if (reads < 3) {
                cannot(walk, "a write of an element with nothing to write");
                break;
            }
            write_elem(walk, op, place, base, base + 2, false);
            break;
        }
        if (place->kind == KEST_IR_PLACE_RUN) {
            // And the same the other way round: the index under what is being
            // written, which is what the machine pops in that order.
            if (reads != (uint32_t)place->stride + 1 || leaves != 0) {
                cannot(walk, "one of a run written at something other than an "
                             "index");
                break;
            }
            at_stack(walk, first, base);
            say(c, out,
                "    {\n        int64_t which = %s.integer;\n"
                "        if (!kest_run_at(rt, which, %u, %u)) {\n"
                "            return false;\n        }\n",
                first, (unsigned)place->count, op->span.offset);
            for (uint16_t k = 0; k < place->stride; k++) {
                at_stack(walk, second, base + 1 + k);
                say(c, out, "        f[%u + which * %u] = %s;\n",
                    (unsigned)place->slot + k, (unsigned)place->stride,
                    second);
            }
            say(c, out, "    }\n");
            break;
        }
        if (place->kind != KEST_IR_PLACE_SLOT) {
            cannot(walk, "a place that is not a run of the frame");
            break;
        }
        if (place->slots != reads) {
            cannot(walk, "a write of a different width than what it writes");
            break;
        }
        for (uint16_t k = 0; k < place->slots; k++) {
            at_frame(walk, first, (uint32_t)(place->slot + k));
            at_stack(walk, second, base + k);
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    // A value out of its parts is already its parts, laid out where they were
    // made, and two ways of arriving at one value are one value. Neither is
    // anything here, which is the same answer `lower` gives.
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
        break;
    case KEST_IR_PART: {
        uint32_t offset = op->imm[0];
        uint32_t wide = op->imm[1];
        uint32_t whole = op->imm[2];
        if (whole != reads || wide != leaves ||
            (size_t)offset + wide > whole) {
            cannot(walk, "a field that is not part of what it is a field of");
            break;
        }
        for (uint32_t k = 0; k < wide && offset > 0; k++) {
            at_stack(walk, first, base + k);
            at_stack(walk, second, base + offset + k);
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    case KEST_IR_TURN: {
        uint32_t count = op->imm[0];
        if (count != reads || count != leaves || count == 0) {
            cannot(walk, "a rotation of something other than what it reads");
            break;
        }
        // The last slot is the tag and belongs first, so the run is rolled by
        // one rather than reversed. The machine does it with a move; here it
        // is the same move written out, because the host's compiler can see
        // through assignments and cannot see through `memmove`.
        at_stack(walk, first, base + count - 1);
        say(c, out, "    {\n        KV turned = %s;\n", first);
        for (uint32_t k = count - 1; k > 0; k--) {
            at_stack(walk, first, base + k);
            at_stack(walk, second, base + k - 1);
            say(c, out, "        %s = %s;\n", first, second);
        }
        at_stack(walk, first, base);
        say(c, out, "        %s = turned;\n    }\n", first);
        break;
    }
    case KEST_IR_DROP:
        break;
    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE: {
        if (kest_is_a_run(op->type)) {
            // Two runs of slots, compared over the same parts that decide
            // what one of them hashes to. Only `==` and `!=` are asked of a
            // shape; the rest are refused by the checker.
            if (op->kind != KEST_IR_EQ && op->kind != KEST_IR_NE) {
                cannot(walk, "an ordering of something wider than a number");
                break;
            }
            at_stack(walk, first, base);
            at_stack(walk, second, base + reads / 2);
            say(c, out,
                "    %s.integer = %skest_value_same(rt, %u, &%s, &%s);\n",
                first, op->kind == KEST_IR_NE ? "!" : "",
                (unsigned)op->imm[0], first, second);
            break;
        }
        if (op->type != NULL && op->type->tag == KEST_T_TEXT) {
            // Two pieces of text, each two slots: what they are made of and
            // how many bytes that is. Where they stand to one another is the
            // door the machine goes through too, and the answer the operation
            // wants is where that number stands to nought. Nothing is charged
            // for how far it read: a body written in C has no budget to
            // charge, which is what D1093 says compiling gives up.
            Where fourth;
            if (reads != 4 || leaves != 1) {
                cannot(walk, "an answer about a piece of text");
                break;
            }
            const char *stands = op->kind == KEST_IR_EQ   ? "=="
                                 : op->kind == KEST_IR_NE ? "!="
                                 : op->kind == KEST_IR_LT ? "<"
                                 : op->kind == KEST_IR_LE ? "<="
                                 : op->kind == KEST_IR_GT ? ">"
                                                          : ">=";
            at_stack(walk, first, base);
            at_stack(walk, second, base + 1);
            at_stack(walk, third, base + 2);
            at_stack(walk, fourth, base + 3);
            say(c, out,
                "    %s.integer = kest_text_order(%s.text, %s.integer,\n"
                "                                 %s.text, %s.integer,\n"
                "                                 NULL) %s 0;\n",
                first, first, second, third, fourth, stands);
            break;
        }
        const char *how = binary_c(op->kind, op->type);
        if (how == NULL || reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        at_stack(walk, third, base + 1);
        say(c, out, "    ");
        say(c, out, how, first, second, third);
        say(c, out, "\n");
        break;
    }
    case KEST_IR_DIV:
    case KEST_IR_MOD: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        at_stack(walk, third, base + 1);
        if (kest_is_float(op->type)) {
            if (op->kind == KEST_IR_DIV) {
                say(c, out, "    ");
                say(c, out, binary_c(op->kind, op->type), first, second,
                    third);
                say(c, out, "\n");
                break;
            }
            // What is left over from dividing two floats is the library's
            // answer rather than one written again here. See D970.
            c->wants_library = true;
            say(c, out, "    %s.real = %skest_left_over(%s.real, %s.real);\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second,
                third);
            break;
        }
        // Dividing by nought stops the program where the machine stops it,
        // and the one pair whose quotient does not fit answers what wrapping
        // says rather than what the host's machine traps on. See D667.
        say(c, out, "    if (%s.integer == 0) {\n", third);
        say(c, out,
            "        return kest_native_stopped(rt, %u, \"K0601\",\n"
            "            \"division by zero\");\n    }\n",
            op->span.offset);
        if (kest_is_unsigned(op->type)) {
            say(c, out, "    ");
            say(c, out, binary_c(op->kind, op->type), first, second, third);
            say(c, out, "\n");
            break;
        }
        say(c, out,
            "    %s.integer = (%s.integer == INT64_MIN && %s.integer == -1) ? %s : (%s.integer %s "
            "%s.integer);\n",
            first, second, third,
            op->kind == KEST_IR_DIV ? "INT64_MIN" : "0", second,
            op->kind == KEST_IR_DIV ? "/" : "%", third);
        break;
    }
    case KEST_IR_NEG:
    case KEST_IR_FLIP: {
        if (reads != 1 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        if (op->kind == KEST_IR_FLIP) {
            say(c, out, "    %s.integer = ~%s.integer;\n", first, second);
            break;
        }
        if (kest_is_float(op->type)) {
            say(c, out, "    %s.real = %s-%s.real;\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second);
            break;
        }
        // The smallest number negated is itself, which is what wrapping says
        // and what negating it signed would leave undefined.
        say(c, out, "    %s.integer = (int64_t)(0 - (uint64_t)%s.integer);\n", first,
            second);
        break;
    }
    case KEST_IR_SHL:
    case KEST_IR_SHR: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        at_stack(walk, third, base + 1);
        // The machine says how far it was asked to shift, in those words,
        // and so does this: two engines that refuse the same program with
        // two sentences are two languages, and what holds them to one is a
        // check that reads both.
        say(c, out, "    if (%s.integer < 0) {\n", third);
        say(c, out,
            "        char said[64];\n"
            "        snprintf(said, sizeof said, \"a shift of %%lld is not "
            "a count\",\n"
            "                 (long long)%s.integer);\n"
            "        return kest_native_stopped(rt, %u, \"K0604\", said);\n"
            "    }\n",
            third, op->span.offset);
        // A count past the width of a slot has no meaning in C, so it is
        // answered here rather than left to the machine: everything shifts
        // out, and a signed number keeps its sign.
        if (op->kind == KEST_IR_SHL) {
            say(c, out,
                "    %s.integer = %s.integer >= 64 ? 0 : (int64_t)((uint64_t)%s.integer << "
                "%s.integer);\n",
                first, third, second, third);
            break;
        }
        if (kest_is_unsigned(op->type)) {
            say(c, out,
                "    %s.integer = %s.integer >= 64 ? 0 : (int64_t)((uint64_t)%s.integer >> "
                "%s.integer);\n",
                first, third, second, third);
            break;
        }
        say(c, out,
            "    %s.integer = %s.integer >= 64 ? (%s.integer < 0 ? -1 : 0) : (%s.integer >> %s.integer);\n",
            first, third, second, second, third);
        break;
    }
    case KEST_IR_NARROW: {
        const char *how = narrow_c(op->imm[0]);
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a cut of something other than a number");
            break;
        }
        if (how == NULL) {
            // A width a slot already holds, which is no cut at all.
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        say(c, out, "    ");
        say(c, out, how, first, second);
        say(c, out, "\n");
        break;
    }
    case KEST_IR_TO_FLOAT:
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        say(c, out, "    %s.real = (double)%s%s.integer;\n", first,
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        break;
    case KEST_IR_TO_WHOLE:
        // Where a number outside the width stops is the library's answer, for
        // the reason the one above it is. See D669.
        c->wants_library = true;
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        say(c, out, "    %s.integer = kest_real_to_int(%u, %s.real);\n", first,
            (unsigned)op->imm[0], second);
        break;
    case KEST_IR_TO_F32:
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        say(c, out, "    %s.real = (double)(float)%s.real;\n", first, second);
        break;
    case KEST_IR_BITS:
        // The machine's two instructions, written out; an `f64` and its bits
        // are one slot read two ways. See D1171.
        if (!kest_is_narrow(op->type)) {
            break;
        }
        at_stack(walk, first, base);
        if (op->imm[0] == 0) {
            say(c, out,
                "    {\n        float narrow = (float)%s.real;\n"
                "        uint32_t bits;\n"
                "        memcpy(&bits, &narrow, sizeof bits);\n"
                "        %s.integer = bits;\n    }\n",
                first, first);
        } else {
            say(c, out,
                "    {\n        uint32_t bits = (uint32_t)%s.integer;\n"
                "        float narrow;\n"
                "        memcpy(&narrow, &bits, sizeof narrow);\n"
                "        %s.real = narrow;\n    }\n",
                first, first);
        }
        break;
    case KEST_IR_NOT:
        at_stack(walk, first, base);
        at_stack(walk, second, base);
        say(c, out, "    %s.integer = !%s.integer;\n", first, second);
        break;
    case KEST_IR_ADDR: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_AT) {
            // An address already worked out, stepped by an index worked out
            // while it runs: one of a fixed run inside memory the program
            // holds an address into. The machine's own sentence answers for
            // an index that is not one of them. See D430 and D1119.
            if (reads != 2 || leaves != 1) {
                cannot(walk, "an address stepped by something other than an "
                             "index");
                break;
            }
            at_stack(walk, first, base);
            at_stack(walk, second, base + 1);
            say(c, out,
                "    {\n        int64_t which = %s.integer;\n"
                "        if (!kest_run_at(rt, which, %u, %u)) {\n"
                "            return false;\n        }\n"
                "        %s.object = (unsigned char *)%s.object +\n"
                "                    (size_t)which * %u;\n    }\n",
                second, (unsigned)place->count, op->span.offset, first, first,
                (unsigned)place->stride);
            break;
        }
        if (place->kind != KEST_IR_PLACE_ELEM) {
            cannot(walk, "the address of a place this does not take one of");
            break;
        }
        if (reads != 2 || leaves != 1) {
            cannot(walk, "an address of something other than one of a run");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        // The same four lines the read uses, for the same reason. See D1112.
        say(c, out,
            "    {\n        const KestRun *run = (const KestRun *)%s.object;\n"
            "        int64_t which = %s.integer;\n"
            "        if (run != NULL && run->what == KEST_RUN_IS &&\n"
            "            (uint64_t)which < (uint64_t)run->length) {\n"
            "            %s.object = run->bytes + (size_t)which * "
            "run->stride;\n"
            "        } else {\n"
            "            %s.object = kest_elem_at(rt, %s, which, 0, %u);\n"
            "            if (%s.object == NULL) {\n"
            "                return false;\n            }\n"
            "        }\n    }\n",
            first, second, first, first, first, op->span.offset, first);
        break;
    }
    case KEST_IR_TEXT_LEN:
        // How many bytes there are is part of what a piece of text is: the
        // second of its two slots. A read rather than a walk, the same as the
        // machine's. See D964.
        if (reads != 2 || leaves != 1) {
            cannot(walk, "a length of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out, "    %s.integer = %s.integer;\n", first, second);
        break;
    case KEST_IR_TEXT_AT: {
        // The byte at a place: what it is made of, how many bytes that is,
        // and where to look. A comparison and a read rather than a walk,
        // which is what D964 bought.
        if (reads != 3 || leaves != 1) {
            cannot(walk, "a byte of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        say(c, out,
            "    if (!kest_text_at(rt, %s.text, %s.integer, %s.integer, %u,\n"
            "                      &%s.integer)) {\n        return false;\n"
            "    }\n",
            first, second, third, op->span.offset, first);
        break;
    }
    case KEST_IR_TEXT_IN: {
        // The one read in this language that does not ask, because the walk
        // took the length before its first turn and the place is there. The
        // machine asks anyway in the build that checks itself, where nothing
        // else can; a file this writes is compiled by somebody else's
        // compiler and has no such build, so what it writes is the read.
        Where held;
        if (reads != 0 || leaves != 1) {
            cannot(walk, "a walk over something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_frame(walk, held, op->imm[0]);
        at_frame(walk, second, op->imm[1]);
        say(c, out,
            "    %s.integer = (unsigned char)%s.text[%s.integer];\n",
            first, held, second);
        break;
    }
    case KEST_IR_TEXT_SLICE:
    case KEST_IR_TEXT_REST: {
        // A cut, and the rest from a place. Both leave a piece of text that
        // is a place inside the one they were cut from and a length: nothing
        // is copied and nothing reaches the heap.
        bool whole = op->kind == KEST_IR_TEXT_REST;
        if (reads != (whole ? 3u : 4u) || leaves != 2) {
            cannot(walk, "a cut of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        if (whole) {
            say(c, out,
                "    if (!kest_text_rest(rt, %s.text, %s.integer, "
                "%s.integer,\n"
                "                        %u, &%s.text, &%s.integer)) {\n"
                "        return false;\n    }\n",
                first, second, third, op->span.offset, first, second);
            break;
        }
        Where fourth;
        at_stack(walk, fourth, base + 3);
        say(c, out,
            "    if (!kest_text_cut(rt, %s.text, %s.integer, %s.integer,\n"
            "                       %s.integer, %u, &%s.text, "
            "&%s.integer)) {\n"
            "        return false;\n    }\n",
            first, second, third, fourth, op->span.offset, first, second);
        break;
    }
    case KEST_IR_TEXT_MATCHES: {
        // Whether a needle sits at a place, which is five slots: the text,
        // where to look, and the needle.
        Where fourth;
        Where fifth;
        if (reads != 5 || leaves != 1) {
            cannot(walk, "a match of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        at_stack(walk, fourth, base + 3);
        at_stack(walk, fifth, base + 4);
        say(c, out,
            "    if (!kest_text_matches(rt, %s.text, %s.integer, %s.integer,"
            "\n"
            "                           %s.text, %s.integer, %u,\n"
            "                           &%s.integer)) {\n"
            "        return false;\n    }\n",
            first, second, third, fourth, fifth, op->span.offset, first);
        break;
    }
    case KEST_IR_TEXT_FIND: {
        // Where a needle is first found, and whether it was: the place comes
        // back under the answer, which is the pair a `match` reads.
        Where fourth;
        Where fifth;
        if (reads != 5 || leaves != 2) {
            cannot(walk, "a search of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        at_stack(walk, fourth, base + 3);
        at_stack(walk, fifth, base + 4);
        say(c, out,
            "    if (!kest_text_find(rt, %s.text, %s.integer, %s.text,\n"
            "                        %s.integer, %s.integer, %u,\n"
            "                        &%s.integer, &%s.integer)) {\n"
            "        return false;\n    }\n",
            first, second, third, fourth, fifth, op->span.offset, first,
            second);
        break;
    }
    case KEST_IR_TEXT_OF: {
        // A value written out as text. Which of the five ways is read off the
        // type here the way the machine reads it off the instruction, and a
        // shape, an enum, an optional or a set of bits goes through the walk
        // that knows what a value is made of -- which is one answer, in
        // `format_value`, shared by both engines.
        if (leaves != 2) {
            cannot(walk, "text made of something that leaves something else");
            break;
        }
        const KestType *of = op->type;
        at_stack(walk, first, base);
        if (of != NULL &&
            (of->tag == KEST_T_FLAGS || of->tag == KEST_T_ENUM ||
             of->tag == KEST_T_OPTIONAL || of->tag == KEST_T_STRUCT ||
             of->tag == KEST_T_FIXED)) {
            say(c, out,
                "    if (!kest_text_of_value(rt, %u, &%s, %u, &%s)) {\n"
                "        return false;\n    }\n",
                (unsigned)op->imm[0], first, op->span.offset, first);
            break;
        }
        if (reads != 1) {
            cannot(walk, "text made of something wider than a number");
            break;
        }
        const char *how =
            of == NULL                ? "KEST_TEXT_OF_INT"
            : of->tag == KEST_T_FLOAT ? (kest_is_narrow(of)
                                             ? "KEST_TEXT_OF_NARROW"
                                             : "KEST_TEXT_OF_REAL")
            : of->tag == KEST_T_BOOL  ? "KEST_TEXT_OF_BOOL"
            : kest_is_unsigned(of)    ? "KEST_TEXT_OF_UNSIGNED"
                                      : "KEST_TEXT_OF_INT";
        say(c, out,
            "    if (!kest_text_of(rt, %s, %s, %u, &%s)) {\n"
            "        return false;\n    }\n",
            how, first, op->span.offset, first);
        break;
    }
    case KEST_IR_TEXT_JOIN: {
        // Pieces joined into one. Two slots each, in the order they are
        // written, and what comes back sits where the first of them was.
        if (reads != (uint32_t)op->imm[0] * 2 || leaves != 2) {
            cannot(walk, "text joined out of something other than pieces");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_text_join(rt, &%s, %u, %u, &%s)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)op->imm[0], op->span.offset, first);
        break;
    }
    case KEST_IR_TEXT_FROM: {
        // A run of bytes become text, which is the one place the walk that
        // says it is UTF-8 is paid for.
        if (reads != 1 || leaves != 2) {
            cannot(walk, "text made of something other than a run of bytes");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_text_from(rt, %s, %u, &%s)) {\n"
            "        return false;\n    }\n",
            first, op->span.offset, first);
        break;
    }
    case KEST_IR_HASH: {
        if (leaves != 1) {
            cannot(walk, "a hash that leaves something other than a number");
            break;
        }
        at_stack(walk, first, base);
        // A shape or a reference goes through the walk that knows what a
        // value is made of, because only part of a reference is hashed: the
        // place is the program's and the number above it is the process's
        // (D1054). Everything else is one slot, or two for text.
        if (kest_is_a_run(op->type) ||
            (op->type != NULL && op->type->tag == KEST_T_REF)) {
            say(c, out, "    %s.integer = kest_value_hash(rt, %u, &%s);\n",
                first, (unsigned)op->imm[0], first);
            break;
        }
        if (op->type != NULL && op->type->tag == KEST_T_TEXT) {
            at_stack(walk, second, base + 1);
            say(c, out, "    %s.integer = kest_text_hash(%s.text, "
                        "%s.integer);\n",
                first, first, second);
            break;
        }
        if (kest_is_float(op->type)) {
            // Nought and minus nought are one value to `==`, so they are one
            // value here.
            say(c, out,
                "    %s.integer = (int64_t)kest_mix(%s.real == 0.0 ? 0 :\n"
                "                                   (uint64_t)%s.integer);\n",
                first, first, first);
            break;
        }
        say(c, out,
            "    %s.integer = (int64_t)kest_mix((uint64_t)%s.integer);\n",
            first, first);
        break;
    }
    case KEST_IR_ARRAY_NEW: {
        // `array(n)` and `array(n, v)`: how many, and what each one starts
        // as. The count is under the fill, which is where the machine has
        // them too.
        if (leaves != 1) {
            cannot(walk, "a run of elements that leaves something else");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_array_new(rt, %u, %s.integer, %s%s, %u, &%s)) {\n"
            "        return false;\n    }\n",
            (unsigned)op->imm[0], first, reads > 1 ? "&" : "",
            reads > 1 ? second : "NULL", op->span.offset, first);
        break;
    }
    case KEST_IR_APPEND: {
        // One more on the end: the handle, and then what is being put there.
        if (reads < 2 || leaves != 0) {
            cannot(walk, "an append of something other than one thing");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_array_push(rt, %s, %u, &%s, %u)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)op->imm[0], second, op->span.offset);
        break;
    }
    case KEST_IR_FIT: {
        // One more on the end when the room is already there, and an answer
        // saying so when it is not: the append a body under a promise may do.
        if (reads < 2 || leaves != 1) {
            cannot(walk, "a fit of something other than one thing");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_array_fit(rt, %s, %u, &%s, %u, &%s.integer)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)op->imm[0], second, op->span.offset, first);
        break;
    }
    case KEST_IR_APPEND_TEXT:
    case KEST_IR_FIT_TEXT: {
        // A whole piece of text onto a run of bytes: the handle, and then
        // what it is made of and how many bytes that is.
        bool fitting = op->kind == KEST_IR_FIT_TEXT;
        if (reads != 3 || leaves != (fitting ? 1u : 0u)) {
            cannot(walk, "an append of something other than a piece of text");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        if (fitting) {
            say(c, out,
                "    if (!kest_array_fit_text(rt, %s, %s.text, %s.integer, "
                "%u,\n"
                "                             &%s.integer)) {\n"
                "        return false;\n    }\n",
                first, second, third, op->span.offset, first);
            break;
        }
        say(c, out,
            "    if (!kest_array_push_text(rt, %s, %u, %s.text, %s.integer,\n"
            "                              %u)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)op->imm[0], second, third, op->span.offset);
        break;
    }
    case KEST_IR_ROOM: {
        // Room for what is coming, in the one thing that has room or the
        // other. Which of the two it is about is read off what it was handed,
        // the way the instruction reads it.
        if (reads != 2 || leaves != 0) {
            cannot(walk, "room made in something other than one thing");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_array_room(rt, %s, %u, %s.integer, %u)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)op->imm[0], second, op->span.offset);
        break;
    }
    case KEST_IR_CLEAR: {
        if (reads != 1 || leaves != 0) {
            cannot(walk, "everything taken out of something other than a run");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_array_clear(rt, %s, %u)) {\n"
            "        return false;\n    }\n",
            first, op->span.offset);
        break;
    }
    case KEST_IR_POP_LAST: {
        // The last one off the end, and whether there was one. Where it was
        // is still the run's own memory, so the move reads it after the
        // length has already moved -- which is what the instruction does too.
        const KestLayout *layout =
            c->module == NULL || op->imm[0] >= c->module->layout_count
                ? NULL
                : &c->module->layouts[op->imm[0]];
        if (layout == NULL || layout->type == NULL || reads != 1 ||
            leaves != (uint32_t)layout->slots + 1) {
            cannot(walk, "a last one taken off something other than a run");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + layout->slots);
        say(c, out,
            "    {\n        unsigned char *at = NULL;\n"
            "        if (!kest_array_pop(rt, %s, %u, &at)) {\n"
            "            return false;\n        }\n"
            "        if (at == NULL) {\n",
            first, op->span.offset);
        for (uint16_t i = 0; i < layout->slots; i++) {
            Where empty;
            at_stack(walk, empty, base + i);
            say(c, out, "            %s.integer = 0;\n", empty);
        }
        say(c, out,
            "            %s.integer = 0;\n        } else {\n", second);
        uint16_t moved = move_value(walk, layout->type, base, 0, true,
                                    op->span.offset);
        say(c, out, "            %s.integer = 1;\n        }\n    }\n",
            second);
        if (moved != layout->slots) {
            cannot(walk, "a last one whose type and layout say different "
                         "widths");
            break;
        }
        break;
    }
    case KEST_IR_ARRAY: {
        // A run written out in the program: that many values, already where
        // they are read from, becoming the one thing that names them.
        const KestLayout *layout =
            c->module == NULL || op->imm[1] >= c->module->layout_count
                ? NULL
                : &c->module->layouts[op->imm[1]];
        if (layout == NULL ||
            reads != (uint32_t)op->imm[0] * layout->slots || leaves != 1) {
            cannot(walk, "a run written out of something else");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_array_written(rt, %u, %u, &%s, %u, &%s)) {\n"
            "        return false;\n    }\n",
            (unsigned)op->imm[1], (unsigned)op->imm[0], first,
            op->span.offset, first);
        break;
    }
    case KEST_IR_TAKE: {
        // One out of the middle: read where it is, move it into slots, and
        // then take it away. Two doors and the moves between them, which is
        // the same three things the instruction does.
        const KestLayout *layout =
            c->module == NULL || op->imm[0] >= c->module->layout_count
                ? NULL
                : &c->module->layouts[op->imm[0]];
        if (layout == NULL || layout->type == NULL || reads != 2 ||
            leaves != layout->slots) {
            cannot(walk, "a take of something other than one of a run");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        // The element goes where the handle was, so the handle and the index
        // are held aside first: what takes it away needs both after the read.
        // Nothing here can reach the heap, so holding them is holding them.
        say(c, out,
            "    {\n        KV held = %s;\n        KV which = %s;\n"
            "        unsigned char *at = kest_elem_at(rt, held, "
            "which.integer, 0, %u);\n"
            "        if (at == NULL) {\n            return false;\n"
            "        }\n",
            first, second, op->span.offset);
        uint16_t moved = move_value(walk, layout->type, base, 0, true,
                                    op->span.offset);
        say(c, out,
            "        if (!kest_array_remove(rt, held, which.integer, %u)) {\n"
            "            return false;\n        }\n    }\n",
            op->span.offset);
        if (moved != layout->slots) {
            cannot(walk, "a take whose type and layout say different widths");
            break;
        }
        break;
    }
    case KEST_IR_STORE_NEW: {
        // How much room, and what it holds.
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a store made of something other than a count");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_store_new(rt, %u, %s.integer, %u, &%s)) {\n"
            "        return false;\n    }\n",
            (unsigned)op->imm[1], first, op->span.offset, first);
        break;
    }
    case KEST_IR_STORE_ADD: {
        // The store, and then what is being put in it. What comes back is
        // where it went, as a place and the stamp it was handed out with.
        if (reads < 2 || leaves != 1) {
            cannot(walk, "an add of something other than one thing");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_store_add(rt, %s, %u, &%s, %u, &%s.integer)) {\n"
            "        return false;\n    }\n",
            first, (unsigned)(reads - 1), second, op->span.offset, first);
        break;
    }
    case KEST_IR_STORE_GET: {
        // What is there, and a byte beside it that says whether there was
        // anything: a reference to a place that has been given back reads as
        // nothing rather than as whatever is there now.
        if (reads != 2 || leaves < 1) {
            cannot(walk, "a read of something other than one place");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_store_get(rt, %s, %s.integer, %u, &%s, %u)) {\n"
            "        return false;\n    }\n",
            first, second, (unsigned)(leaves - 1), first, op->span.offset);
        break;
    }
    case KEST_IR_STORE_SET: {
        if (reads < 3 || leaves != 1) {
            cannot(walk, "a write of something other than one place");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        at_stack(walk, third, base + 2);
        say(c, out,
            "    {\n        bool was = false;\n"
            "        if (!kest_store_set(rt, %s, %s.integer, %u, &%s, %u, "
            "&was)) {\n"
            "            return false;\n        }\n"
            "        %s.integer = was;\n    }\n",
            first, second, (unsigned)(reads - 2), third, op->span.offset,
            first);
        break;
    }
    case KEST_IR_STORE_REMOVE: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "a removal of something other than one place");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    {\n        bool was = false;\n"
            "        if (!kest_store_remove(rt, %s, %s.integer, %u, &was)) {\n"
            "            return false;\n        }\n"
            "        %s.integer = was;\n    }\n",
            first, second, op->span.offset, first);
        break;
    }
    case KEST_IR_STORE_COUNT: {
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a count of something other than one store");
            break;
        }
        at_stack(walk, first, base);
        say(c, out,
            "    if (!kest_store_count(rt, %s, %u, &%s.integer)) {\n"
            "        return false;\n    }\n",
            first, op->span.offset, first);
        break;
    }
    case KEST_IR_STORE_REF: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "a reference to something other than one place");
            break;
        }
        at_stack(walk, first, base);
        at_stack(walk, second, base + 1);
        say(c, out,
            "    if (!kest_store_ref(rt, %s, %s.integer, %u, &%s.integer)) "
            "{\n        return false;\n    }\n",
            first, second, op->span.offset, first);
        break;
    }
    case KEST_IR_SEEK_FROM:
    case KEST_IR_SEEK_NEXT: {
        // A walk of a store: the first one leaves when there is none and the
        // ones after go back while there is one, which is the same shape
        // every other walk has.
        bool first_one = op->kind == KEST_IR_SEEK_FROM;
        at_frame(walk, first, op->imm[0]);
        at_frame(walk, second, op->imm[1]);
        say(c, out,
            "    if (!kest_store_seek(rt, %s, %s.integer + %d, %u, "
            "&%s.integer)) {\n        return false;\n    }\n",
            first, second, first_one ? 0 : 1, op->span.offset, second);
        say(c, out, "    if (%s.integer %s 0) {\n        ", second,
            first_one ? "<" : ">=");
        write_branch(walk, op->target, base + leaves, op->span.offset);
        say(c, out, "    }\n");
        break;
    }
    case KEST_IR_LEN: {
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a length of something other than one thing");
            break;
        }
        at_stack(walk, first, base);
        // Through a number of its own rather than the operand's address, so
        // the operands stay the body's own (D1179). A handle that is a run
        // says how long it is where it stands, the same test an element read
        // makes; anything else goes to the door, which says what the machine
        // says about it. See D1186.
        say(c, out,
            "    {\n        int64_t many;\n"
            "        const KestRun *run = (const KestRun *)%s.object;\n"
            "        if (run != NULL && run->what == KEST_RUN_IS) {\n"
            "            many = run->length;\n"
            "        } else if (!kest_elem_count(rt, %s, %u, &many)) {\n"
            "            return false;\n        }\n"
            "        %s.integer = many;\n    }\n",
            first, first, op->span.offset, first);
        break;
    }
    case KEST_IR_CALL: {
        uint32_t which = op->imm[0];
        if (op->imm[1] != reads) {
            cannot(walk, "a call handing over something other than what it "
                         "read");
            break;
        }
        if (walk->into->call_count == walk->into->call_room) {
            uint32_t room = walk->into->call_room == 0
                                ? 8
                                : walk->into->call_room * 2;
            uint32_t *grown =
                grow(c->arena, walk->into->calls,
                     sizeof(uint32_t) * walk->into->call_room,
                     sizeof(uint32_t) * room, sizeof(uint32_t));
            if (grown == NULL) {
                c->out_of_memory = true;
                break;
            }
            walk->into->calls = grown;
            walk->into->call_room = room;
        }
        walk->into->calls[walk->into->call_count++] = which;
        // What it answers is whether it ran. A body that stopped has already
        // said so through the machine, so the caller gives back what it gave
        // back and nothing here writes a second message about it.
        at_stack(walk, first, base);
        // Where the callee's own slots go on the machine's stack: above
        // everything this body is holding there. A body that keeps its
        // operands in locals holds nothing there, so the room it was given
        // for them is where the callee's frame goes; one that lives on the
        // stack hands over what is above its live operands, which is exactly
        // where a call leaves them. Either way the machine is asked first
        // whether there is room, because a call that walks off the stack is
        // what that refusal is for.
        Where handed;
        // A stack-shaped body hands over what a call leaves: the arguments
        // are the callee's slots, in the place the machine would have put
        // them. One that keeps its operands in locals has nothing on the
        // machine's stack, so the room it was given for operands is where a
        // callee's frame goes.
        snprintf(handed, sizeof(Where), walk->on_the_stack ? "sf + %u" : "frame + %u",
                 walk->on_the_stack ? base : walk->body->slot_count);
        // What `kest_native_room` did, written out: is there a frame to
        // spare, is there stack for what the callee wants, where the call is
        // written, and the ledger entry itself. The three questions are three
        // comparisons and the entry is four stores; what the call to a door
        // cost on top of them was the call, which the host's compiler cannot
        // hoist a loop-invariant across. See D1122.
        const KestChunk *callee =
            c->module == NULL || which >= c->module->count
                ? NULL
                : c->module->functions[which];
        if (callee == NULL) {
            cannot(walk, "a call to a body this was not given");
            break;
        }
        // How much of the machine's stack the callee's frame takes is known
        // once the callee is lowered, which a body written after this one is
        // not yet: it is named here and written at the top of the file,
        // after every body is. A number read here was nought for every call
        // forward, so the reach the collector walks to stopped below a frame
        // it had to see. See D1179.
        Where needs;
        snprintf(needs, sizeof(Where), "KN_%u", which);
        if (carried_here(walk, which)) {
            c->bodies[which].carried_in = true;
            // A body the machine carries into this one rather than calling it
            // is given no frame there, so it is given none here: nothing in
            // one can stop the program, so there is nothing a frame would be
            // asked for, and two engines that count frames differently are
            // two numbers a host is told to find. See D1156.
            say(c, out,
                "    {\n        KV *stands = %s;\n"
                "        if (stands + %s > led.limit) {\n"
                "            return kest_native_crowded(rt, %u, %u, false);\n"
                "        }\n"
                "        if (stands + %s > *led.reached) {\n"
                "            *led.reached = stands + %s;\n"
                "        }\n",
                handed, needs, which, op->span.offset, needs, needs);
            leave_arguments(walk, out, which, base, reads);
            say(c, out, "        bool went = kf_%u(rt, stands, %s%s KC_%u(",
                which, leaves > 0 ? "&" : "", leaves > 0 ? first : "NULL",
                which);
            for (uint32_t k = 0; k < reads; k++) {
                at_stack(walk, second, base + k);
                say(c, out, "%s%s", k == 0 ? "" : ", ", second);
            }
            say(c, out,
                "));\n        if (!went) {\n            return false;\n"
                "        }\n    }\n");
            break;
        }
        say(c, out,
            "    {\n        uint32_t was = *led.many;\n"
            "        if (was > 0) {\n"
            "            led.calls[was - 1].said_at = %u;\n"
            "        }\n"
            "        if (was == led.most) {\n"
            "            return kest_native_crowded(rt, %u, %u, true);\n"
            "        }\n"
            "        KV *stands = %s;\n"
            "        if (stands + %s > led.limit) {\n"
            "            return kest_native_crowded(rt, %u, %u, false);\n"
            "        }\n"
            "        led.calls[was].chunk = led.chunks[%u];\n"
            "        led.calls[was].ip = NULL;\n"
            "        led.calls[was].base = stands;\n"
            "        led.calls[was].said_at = 0;\n"
            "        *led.many = was + 1;\n"
            "        if (stands + %s > *led.reached) {\n"
            "            *led.reached = stands + %s;\n"
            "        }\n",
            op->span.offset, which, op->span.offset, handed, needs, which,
            op->span.offset, which, needs, needs);
        leave_arguments(walk, out, which, base, reads);
        say(c, out, "        bool went = kf_%u(rt, stands, %s%s KC_%u(", which,
            leaves > 0 ? "&" : "", leaves > 0 ? first : "NULL", which);
        for (uint32_t k = 0; k < reads; k++) {
            at_stack(walk, second, base + k);
            say(c, out, "%s%s", k == 0 ? "" : ", ", second);
        }
        say(c, out,
            "));\n        *led.many = was;\n"
            "        if (!went) {\n            return false;\n        }\n"
            "    }\n");
        break;
    }
    case KEST_IR_CALL_VALUE: {
        // A call whose callee is decided while it runs. Everything the
        // machine asks before it enters one -- that the value names a
        // function, that the function is of the shape the call was written
        // against, and that it keeps what this body promised -- is asked
        // behind one door, because none of it can be worked out here. What a
        // body reaching one of these can do is refused before this: it may
        // hold a handle in a local only if nothing it does can reach the
        // heap, and a call through a value can. So this body is on the
        // machine's stack, the arguments are already where the callee's
        // frame goes, and what comes back lands over them. See D1107.
        if (reads != (uint32_t)op->imm[0] + 1 || leaves != op->imm[1] ||
            !walk->on_the_stack) {
            cannot(walk, "a call through a value handing over something "
                         "other than what it read");
            break;
        }
        at_stack(walk, first, base + op->imm[0]);
        say(c, out,
            "    {\n        KV what = %s;\n        uint16_t gave = 0;\n"
            "        if (!kest_call_value(rt, what, sf + %u, %u, %u, %u,\n"
            "                             &gave)) {\n"
            "            return false;\n        }\n    }\n",
            first, base, (unsigned)op->imm[0], (unsigned)op->imm[1],
            op->span.offset);
        break;
    }
    case KEST_IR_CALL_HOST: {
        // A crossing into the host. Everything the boundary asks is behind
        // one door, and this is the same call the machine's instruction
        // makes: the arguments are where the answer goes, and where the
        // machine had got to is what a host calling back in stands on. A body
        // that reaches one of these is on the machine's stack, because a host
        // may take the heap while it is in there. See D1108.
        if (reads != op->imm[1] || leaves != op->imm[2] ||
            !walk->on_the_stack) {
            cannot(walk, "a crossing handing over something other than what "
                         "it read");
            break;
        }
        say(c, out,
            "    if (!kest_call_host(rt, %u, sf + %u, %u, %u, %u)) {\n"
            "        return false;\n    }\n",
            (unsigned)op->imm[0], base, (unsigned)op->imm[1],
            (unsigned)op->imm[2], op->span.offset);
        break;
    }
    case KEST_IR_REGION_OPEN:
    case KEST_IR_REGION_CLOSE: {
        // A block of the heap handed back whole when the block ends. Which
        // block it is lives in a slot of the frame, which is where the
        // machine keeps it too. See D1119.
        Where held;
        if (reads != 0 || leaves != 0) {
            cannot(walk, "working memory opened around something other than "
                         "a block");
            break;
        }
        at_frame(walk, held, op->imm[0]);
        if (op->kind == KEST_IR_REGION_OPEN) {
            say(c, out,
                "    if (!kest_region_open(rt, %u, &%s.integer)) {\n"
                "        return false;\n    }\n",
                op->span.offset, held);
            break;
        }
        say(c, out,
            "    if (!kest_region_close(rt, %s.integer, %u)) {\n"
            "        return false;\n    }\n",
            held, op->span.offset);
        break;
    }
    case KEST_IR_GO:
        say(c, out, "    ");
        write_branch(walk, op->target, base + leaves, op->span.offset);
        break;
    case KEST_IR_ASK:
        if (reads != leaves + 1) {
            cannot(walk, "a branch reading something other than an answer");
            break;
        }
        at_stack(walk, first, walk->stack - 1);
        say(c, out, "    if (%s%s.integer) {\n        ", op->imm[1] != 0 ? "" : "!",
            first);
        write_branch(walk, op->target, base + leaves, op->span.offset);
        say(c, out, "    }\n");
        break;
    case KEST_IR_NEXT: {
        // A walk's step: count on, decide, and go round again or leave. The
        // machine has one instruction for it and this is the same three
        // things, which is what makes a loop here a loop the host's compiler
        // recognises.
        at_frame(walk, first, op->imm[0]);
        at_frame(walk, second, op->imm[1]);
        // And an unsigned count stepped in unsigned arithmetic, for the
        // reason the machine steps it that way. See D1212.
        if (kest_is_unsigned(op->type)) {
            say(c, out,
                "    %s.integer = (int64_t)((uint64_t)%s.integer + 1u);\n",
                first, first);
        } else {
            say(c, out, "    %s.integer += 1;\n", first);
        }
        say(c, out, "    if (%s%s.integer %s %s%s.integer) {\n        ",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", first,
            kest_is_unsigned(op->type) ? "<" : "<",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        write_branch(walk, op->target, base + leaves, op->span.offset);
        say(c, out, "    }\n");
        break;
    }
    case KEST_IR_GIVE: {
        uint32_t count = op->imm[0];
        if (count != reads || count != walk->into->results) {
            cannot(walk, "giving back something other than what it gives");
            break;
        }
        for (uint32_t k = 0; k < count; k++) {
            at_stack(walk, first, base + k);
            say(c, out, "    out[%u] = %s;\n", k, first);
        }
        say(c, out, "    return true;\n");
        break;
    }
    default:
        // Everything else: the heap, text, stores, the host, working memory,
        // and every place that is not a run of the frame. A `default` here is
        // not a list left incomplete -- it is what this backend is, which is
        // a growing half of a language whose other half runs everything. An
        // operation added and forgotten here is a body the machine runs.
        cannot(walk, kest_ir_word((KestIrKind)op->kind));
        break;
    }
    walk->stack = base + leaves;
}

// What a body is declared as. It answers whether it ran rather than what it
// worked out, because a body can stop -- dividing by nought, shifting by a
// count that is not one -- and what it worked out goes where the caller says.
// The machine comes with it because a body that stops says so through the
// machine, which is what makes a refusal from compiled code read like a
// refusal from the instructions (D1094).
//
// And the frame, which is the run of slots on the machine's stack that this
// body was given. A body that cannot reach the heap keeps its slots and its
// operands in locals and uses the frame for nothing but handing one to
// whatever it calls; one that can reach the heap lives in it, because the
// collector walks the machine's stack and a local is not on it. Both are
// called the same way, which is what lets a body be written before the ones
// it calls. See D1098.
// Whether an argument is handed to a body as a value. One the body reads in
// the frame its caller left it in is not: handing it over as well was a copy
// in every register and every stack slot a call passes through, for a value
// nothing read. A body the machine runs for a written one takes all of them,
// because it writes them into the frame itself. See D1199.
static bool takes_value(const Body *body, bool as_written, uint16_t p) {
    return !as_written || !body->reads_frame || p >= 32 ||
           ((body->in_frame >> p) & 1u) == 0;
}

static void write_head(KestEmitC *c, Text *into, const Body *body,
                       uint32_t which, bool as_written) {
    say(c, into, "static %sbool kf_%u(KestRuntime *rt, KV *frame, KV *out",
        as_written && body->written && body->carried_in ? "KEST_CARRIED "
                                                         : "",
        which);
    for (uint16_t p = 0; p < body->params; p++) {
        if (takes_value(body, as_written, p)) {
            say(c, into, ", KV a%u", (unsigned)p);
        }
    }
    say(c, into, ")");
}

// A body as this backend reads it, for whoever is growing it. What stops a
// body is said in the file it writes; what a body is made of is not, and the
// two questions a reader has when something was left out are "which
// operation" and "what was on the stack". Read once, because a compiler that
// asked the environment per body could change its mind half way through a
// program. It is the same door `KEST_IRSAY` is, one level down.
static bool saying(void) {
    static int asked = -1;
    if (asked < 0) {
        asked = getenv("KEST_CSAY") == NULL ? 0 : 1;
    }
    return asked != 0;
}

static void say_body(const KestIrBody *body) {
    fprintf(stderr, "c %s: %u slots, %u deep, %u in, %u out\n",
            body->symbol == NULL ? "(no name)" : body->symbol,
            body->slot_count, body->stack_needed, body->param_slots,
            body->result_slots);
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        uint32_t reads = 0;
        uint32_t leaves = 0;
        moves(body, op, &reads, &leaves);
        fprintf(stderr,
                "  %3u %-12s reads %u leaves %u  carries %u %u %u  lands %u\n",
                i, kest_ir_word((KestIrKind)op->kind), reads, leaves,
                op->imm[0], op->imm[1], op->imm[2], op->target);
    }
}

KestEmitC *kest_emitc_new(KestArena *arena, const KestModule *module) {
    KestEmitC *c = KEST_ARENA_NEW(arena, KestEmitC);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->arena = arena;
    c->module = module;
    c->scratch = kest_arena_new();
    if (c->scratch == NULL) {
        return NULL;
    }
    return c;
}

bool kest_emitc_body(void *writing, const KestIrBody *body) {
    KestEmitC *c = writing;
    if (c->count == c->room) {
        uint32_t room = c->room == 0 ? 16 : c->room * 2;
        Body *grown = grow(c->arena, c->bodies, sizeof(Body) * c->room,
                           sizeof(Body) * room, sizeof(void *));
        if (grown == NULL) {
            c->out_of_memory = true;
            return false;
        }
        c->bodies = grown;
        c->room = room;
    }
    Body *into = &c->bodies[c->count++];
    memset(into, 0, sizeof(*into));
    into->params = body->param_slots;
    into->results = body->result_slots;
    // The name is copied: a body lives in an arena of its own and is let go
    // before the file is written.
    if (body->symbol != NULL) {
        into->symbol =
            kest_arena_strndup(c->arena, body->symbol, strlen(body->symbol));
        if (into->symbol == NULL) {
            c->out_of_memory = true;
            return false;
        }
    }
    if (saying()) {
        say_body(body);
    }
    if (body->param_slots > MOST_PARAMS) {
        into->why = "more arguments than this writes";
        return true;
    }

    KestMark before = kest_arena_mark(c->arena);
    kest_arena_reset(c->scratch);
    uint32_t many = body->op_count == 0 ? 1 : body->op_count;
    Walk walk;
    memset(&walk, 0, sizeof(walk));
    walk.c = c;
    walk.body = body;
    walk.into = into;
    walk.depth = KEST_ARENA_ARRAY(c->scratch, uint32_t, many);
    walk.known = KEST_ARENA_ARRAY(c->scratch, bool, many);
    walk.landed = KEST_ARENA_ARRAY(c->scratch, bool, many);
    if (walk.depth == NULL || walk.known == NULL || walk.landed == NULL) {
        c->out_of_memory = true;
        return false;
    }
    memset(walk.depth, 0, sizeof(uint32_t) * many);
    memset(walk.known, 0, sizeof(bool) * many);
    memset(walk.landed, 0, sizeof(bool) * many);

    walk.no_heap = reaches_no_heap(c, body);
    walk.on_the_stack = !walk.no_heap;
    if (walk.on_the_stack && body->slot_count > 0) {
        walk.slot_modes = slot_modes(c->scratch, body);
        if (walk.slot_modes == NULL) {
            c->out_of_memory = true;
            return false;
        }
    }
    if (depths(&walk)) {
        if (walk.on_the_stack) {
            walk.clean = KEST_ARENA_ARRAY(c->scratch, bool, walk.deepest + 1);
            if (walk.clean == NULL) {
                c->out_of_memory = true;
                return false;
            }
            memset(walk.clean, 0, sizeof(bool) * (walk.deepest + 1));
        }
        // Which arguments it reads where its caller left them, which is
        // decided before its head is written because its head is what says
        // so. See D1198 and D1199.
        // And one keeping its operands in locals reads them out of the frame
        // too, when there are more of them than the registers a call has
        // left after the three every body takes: past those, every value is
        // a slot of the C stack the caller writes and this reads, and the
        // caller has written them into the frame already or can. `walk` in
        // the colony takes twenty-five. See D1207.
        bool from_frame = walk.on_the_stack || body->param_slots > 3;
        into->reads_frame = from_frame;
        into->in_frame = 0;
        for (uint16_t p = 0; from_frame && p < body->param_slots && p < 32;
             p++) {
            Where param;
            at_frame(&walk, param, p);
            if (param[0] == 'f' || !walk.on_the_stack) {
                into->in_frame |= 1u << p;
            }
        }
        write_head(c, &into->wrote, into, c->count - 1, true);
        say(c, &into->wrote, " {\n");
        if (walk.on_the_stack) {
            // On the machine's stack, laid out the way the machine would have
            // laid it out: the slots where the caller left the arguments, and
            // the operands above them. What that buys is that the collector
            // sees everything this body is holding, which a local is not.
            // The operands are the body's own, and are written where the
            // machine keeps them, above the slots, around what needs them
            // there. See D1179.
            say(c, &into->wrote,
                "    KV *f = frame;\n    KV *sf = frame + %u;\n"
                "    KV s[%u];\n",
                (unsigned)body->slot_count,
                walk.deepest == 0 ? 1u : walk.deepest);
            // And the slots that hold nothing the collector looks for, in an
            // array of this body's own. See D1162.
            if (walk.slot_modes != NULL) {
                say(c, &into->wrote, "    KV g[%u];\n    (void)g;\n",
                    (unsigned)body->slot_count);
            }
        } else {
            if (body->slot_count > 0) {
                say(c, &into->wrote, "    KV f[%u];\n",
                    (unsigned)body->slot_count);
            }
            if (walk.deepest > 0) {
                say(c, &into->wrote, "    KV s[%u];\n", walk.deepest);
            }
        }
        if (calls_anything(body)) {
            // Where the machine keeps what is standing, read once. Every
            // call below writes its own ledger entry rather than asking a
            // door to: three comparisons and four stores, which a call is a
            // wall in front of. See D1122.
            say(c, &into->wrote,
                "    KestLedger led;\n"
                "    if (!kest_ledger(rt, &led)) {\n"
                "        return false;\n    }\n");
        }
        // A body on the machine's stack is handed the frame its caller
        // left the arguments in -- the machine, the wrapper it enters one
        // through, and every caller written here, which writes them there
        // when `KA_` says the callee reads them there -- so an argument whose
        // slot is in that frame is read where it is. Written again, it was a
        // store into the slot a store had just filled, and 10% of the cycles
        // of compiled `rules`. See D1198.
        for (uint16_t p = 0; p < body->param_slots; p++) {
            Where param;
            at_frame(&walk, param, p);
            if (!takes_value(into, true, p)) {
                // Where the caller left it, into this body's own locals.
                if (!walk.on_the_stack) {
                    say(c, &into->wrote, "    %s = frame[%u];\n", param,
                        (unsigned)p);
                }
                continue;
            }
            say(c, &into->wrote, "    %s = a%u;\n", param, (unsigned)p);
        }
        declare_guards(&walk);
        // Said out loud rather than left to whether the body happens to read
        // them: a frame nothing reads is a warning in somebody else's build,
        // and a warning in a generated file is noise a reader learns to skip.
        say(c, &into->wrote, "    (void)rt;\n    (void)out;\n"
                             "    (void)frame;\n");
        if (body->slot_count > 0 || walk.on_the_stack) {
            say(c, &into->wrote, "    (void)f;\n");
        }
        if (walk.deepest > 0 || walk.on_the_stack) {
            say(c, &into->wrote, "    (void)s;\n");
        }
        if (walk.on_the_stack) {
            say(c, &into->wrote, "    (void)sf;\n");
        }
        for (uint32_t i = 0; i < body->op_count && walk.why == NULL; i++) {
            if (!walk.known[i]) {
                continue;
            }
            if (walk.landed[i]) {
                ask_guards(&walk, i);
                say(c, &into->wrote, "L%u:;\n", i);
            }
            walk.stack = walk.depth[i];
            walk.in_memory = in_memory(&walk, &body->ops[i]);
            // What an operation reads away and leaves, which is which of the
            // operands it writes: those from where its reads begin.
            uint32_t reads = 0;
            uint32_t leaves = 0;
            moves(body, &body->ops[i], &reads, &leaves);
            uint32_t from = walk.stack >= reads ? walk.stack - reads : 0;
            // Where two ways of arriving meet, what the machine's stack
            // holds is what one of them left, so nothing is taken as written.
            if (walk.clean != NULL && walk.landed[i]) {
                memset(walk.clean, 0, sizeof(bool) * (walk.deepest + 1));
            }
            for (uint32_t k = 0; walk.in_memory && k < walk.stack; k++) {
                if (!walk.clean[k]) {
                    say(c, &into->wrote, "    sf[%u] = s[%u];\n", k, k);
                    walk.clean[k] = true;
                }
            }
            write_op(&walk, i, &body->ops[i]);
            for (uint32_t k = from; walk.clean != NULL && k < walk.stack &&
                                    k <= walk.deepest;
                 k++) {
                if (walk.in_memory) {
                    say(c, &into->wrote, "    s[%u] = sf[%u];\n", k, k);
                }
                walk.clean[k] = walk.in_memory;
            }
            walk.in_memory = false;
            // And the same where an operation falls into the next with less
            // than the next is written for, which is the other end of what
            // `arrives` allows.
            if (i + 1 < body->op_count && walk.known[i + 1] &&
                walk.depth[i + 1] > walk.stack) {
                say(c, &into->wrote,
                    "    return kest_native_stopped(rt, %u, \"K0655\",\n"
                    "        \"no way to this operation left a value for "
                    "it\");\n",
                    body->ops[i].span.offset);
            }
        }
        say(c, &into->wrote, "}\n\n");
    }
    if (c->out_of_memory) {
        return false;
    }
    if (walk.why != NULL) {
        into->wrote.bytes = NULL;
        into->wrote.count = 0;
        into->wrote.room = 0;
        into->calls = NULL;
        into->call_count = 0;
        into->call_room = 0;
        kest_arena_rewind(c->arena, before);
        // After the rewind, because what was written about this body is
        // written where what was written of it was.
        into->why = walk.why == walk.said
                        ? kest_arena_strndup(c->arena, walk.said,
                                             strlen(walk.said))
                        : walk.why;
        if (into->why == NULL) {
            c->out_of_memory = true;
            return false;
        }
        return true;
    }
    into->written = true;
    return true;
}

// A body that calls one this backend did not write used to be a body it could
// not write either, and the whole chain above it with it: there was no
// dispatch here to fall back through, so the call had nowhere to go. Now it
// has somewhere -- the machine, which runs that body already. What is written
// for it is a C function of the shape a call here expects, whose body is the
// arguments put into the frame and one call to `kest_call_body`. So a body
// this backend cannot write costs the program that body and nothing above
// it, which is what makes half a program worth having. See D1105.
//
// A body nothing written calls gets nothing: the file is smaller and the
// symbol would be a symbol nothing names.
static void hands_over(KestEmitC *c) {
    // First, the two a caller cannot be written around: a body this backend
    // was never handed, whose shape is not here to write a call to, and one
    // with more arguments than this writes out. Both are the caller's to pay
    // for, and nothing in this tree does either.
    for (uint32_t i = 0; i < c->count; i++) {
        Body *body = &c->bodies[i];
        if (!body->written) {
            continue;
        }
        for (uint32_t k = 0; k < body->call_count; k++) {
            uint32_t which = body->calls[k];
            if (which >= c->count) {
                body->written = false;
                body->why = "it calls a body this was not given";
                break;
            }
            if (c->bodies[which].params > MOST_PARAMS) {
                body->written = false;
                body->why = "it calls a body with more arguments than this "
                            "writes";
                break;
            }
        }
    }
    // And then what is handed over, read off what is still written, so that a
    // body given up on above does not leave a shim nothing names.
    for (uint32_t i = 0; i < c->count; i++) {
        const Body *body = &c->bodies[i];
        if (!body->written) {
            continue;
        }
        for (uint32_t k = 0; k < body->call_count; k++) {
            uint32_t which = body->calls[k];
            if (!c->bodies[which].written) {
                c->bodies[which].handed_over = true;
            }
        }
    }
}

// A piece of text written as a C string, a line of it at a time, with every
// byte that is not printable, a quote, a backslash and a question mark written
// as an escape: `??=` is a trigraph to a compiler told `-std=c11`.
static void as_c_string(KestEmitC *c, Text *out, const char *text,
                        size_t length) {
    say(c, out, "\"");
    size_t on_line = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '\\' || byte == '"' || byte == '?') {
            say(c, out, "\\%c", byte);
        } else if (byte >= 32 && byte < 127) {
            say(c, out, "%c", byte);
        } else {
            say(c, out, "\\%03o", byte);
        }
        if (byte == '\n' || ++on_line >= 72) {
            say(c, out, "\"\n    \"");
            on_line = 0;
        }
    }
    say(c, out, "\"");
}

// The main of a binary that reads its program: from the file it was written
// from, or from another copy of it named on the command line.
static void write_reading_main(KestEmitC *c, Text *file, const char *from) {
    say(c, file,
        "    // The program this was written from, or another copy of it "
        "named\n"
        "    // on the command line. The C is half of a program and this is "
        "the\n"
        "    // other half; a file that has moved on since is refused by the\n"
        "    // binding rather than run.\n"
        "    const char *path = argc > 1 ? argv[1] : \"%s\";\n"
        "    // And what the program itself was started with, which is\n"
        "    // whatever follows the file: `Host.arg` answers from here.\n"
        "    program_args = argc > 2 ? argc - 2 : 0;\n"
        "    program_arg = argc > 2 ? argv + 2 : NULL;\n"
        "    // Where the library is, the way anything that is not this\n"
        "    // project's own command line finds it: the command line looks\n"
        "    // beside itself and then where it was installed, and a host has\n"
        "    // neither of those to go on.\n"
        "    KestBuild *build = kest_build(path, getenv(\"KEST_LIB\"), "
        "stderr,\n                                  KEST_FORM_TEXT, 0);\n"
        "    if (build == NULL) {\n"
        "        fprintf(stderr, \"`%%s` is not a program this can read\\n\","
        "\n                path);\n"
        "        return 1;\n"
        "    }\n",
        from == NULL ? "" : from);
}

// The start of a host that carries its program: every file it was read from,
// written into it, and a build from those rather than from a file -- so it
// runs where there is no source at all, and every word after its own name is
// the program's. See D1172.
static void write_carried_main(KestEmitC *c, Text *file,
                               const KestFile *carried, uint32_t count,
                               const char *library) {
    say(c, file,
        "    // The program and everything it imports, carried here: this\n"
        "    // builds from these and reads no file, so it runs where there\n"
        "    // is no source at all.\n"
        "    static const KestFile carried[] = {\n");
    for (uint32_t i = 0; i < count; i++) {
        say(c, file, "        {");
        as_c_string(c, file, carried[i].path, strlen(carried[i].path));
        say(c, file, ",\n    ");
        as_c_string(c, file, carried[i].text, carried[i].length);
        say(c, file, ",\n         %zu},\n", carried[i].length);
    }
    say(c, file,
        "    };\n"
        "    const char *path = carried[0].path;\n"
        "    program_args = argc > 1 ? argc - 1 : 0;\n"
        "    program_arg = argc > 1 ? argv + 1 : NULL;\n"
        "    KestBuild *build = kest_build_from(\n"
        "        carried, sizeof(carried) / sizeof(carried[0]), ");
    as_c_string(c, file, library == NULL ? "" : library,
                library == NULL ? 0 : strlen(library));
    say(c, file,
        ",\n        stderr, KEST_FORM_TEXT, 0);\n"
        "    if (build == NULL) {\n"
        "        fprintf(stderr, \"`%%s` is not a program this can read\\n\",\n"
        "                path);\n"
        "        return 1;\n"
        "    }\n");
}

const char *kest_emitc_done(KestEmitC *c, const char *entry,
                            const char *from, const KestFile *carried,
                            uint32_t carried_count, const char *library) {
    // What was read while the bodies were written is read no further.
    kest_arena_free(c->scratch);
    c->scratch = NULL;
    hands_over(c);
    Text file = {NULL, 0, 0};
    say(c, &file,
        "// Written by `kest emit --c`. What this means is the program it was\n"
        "// written from: edit that and write this again.\n"
        "//\n"
        "// It is a host of that program as well as a translation of it. A\n"
        "// body this backend had no C for is named below with the reason and\n"
        "// is a body the machine runs, so what this holds is half a program\n"
        "// and the machine holds the other half. See D1093 and D1094.\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <math.h>\n"
        "#include <time.h>\n"
        "\n"
        "#include \"kest.h\"\n"
        "\n"
        "// A multiply and an add stay two roundings, as the machine does\n"
        "// them: a compiler left to fuse them where the target has FMA\n"
        "// answers other bits than the machine does. Clang told\n"
        "// `-ffp-contract=fast` heeds no pragma, and is a build that asked\n"
        "// for other arithmetic. See D1226.\n"
        "#if defined(__clang__)\n"
        "#pragma STDC FP_CONTRACT OFF\n"
        "#elif defined(__GNUC__)\n"
        "#pragma GCC optimize (\"fp-contract=off\")\n"
        "#endif\n"
        "\n"
        "// A slot, under the name the machine's own backend gives it.\n"
        "typedef KestValue KV;\n"
        "\n"
        "// The doors a file this backend wrote calls, and the only ones that\n"
        "// are not in the public header: one binds a body to the chunk it\n"
        "// was written from, the other says what a body says when it stops.\n"
        "// Written out here rather than included, because what this includes\n"
        "// is what a host includes. Link against `libkest.a`.\n"
        "bool kest_native_at(KestRuntime *runtime, uint32_t index,\n"
        "                    const char *symbol,\n"
        "                    bool (*body)(KestRuntime *, KestValue *,\n"
        "                                 uint16_t *));\n"
        "bool kest_native_stopped(KestRuntime *runtime, uint32_t offset,\n"
        "                         const char *code, const char *message);\n"
        "bool kest_call_body(KestRuntime *runtime, uint32_t which,\n"
        "                    KestValue *base, uint16_t handed,\n"
        "                    uint16_t *gave);\n"
        "bool kest_call_value(KestRuntime *runtime, KestValue what,\n"
        "                     KestValue *base, uint16_t handed,\n"
        "                     uint16_t coming_back, uint32_t where,\n"
        "                     uint16_t *gave);\n"
        "bool kest_call_host(KestRuntime *runtime, uint16_t index,\n"
        "                    KestValue *base, uint16_t argument_slots,\n"
        "                    uint16_t result_slots, uint32_t where);\n"
        "bool kest_run_at(KestRuntime *runtime, int64_t index,\n"
        "                 uint32_t count, uint32_t where);\n"
        "bool kest_region_open(KestRuntime *runtime, uint32_t where,\n"
        "                      int64_t *into);\n"
        "bool kest_region_close(KestRuntime *runtime, int64_t was,\n"
        "                       uint32_t where);\n"
        "unsigned char *kest_elem_at(KestRuntime *runtime, KestValue handle,\n"
        "                            int64_t index, uint16_t offset,\n"
        "                            uint32_t where);\n"
        "bool kest_elem_count(KestRuntime *runtime, KestValue handle,\n"
        "                     uint32_t where, int64_t *into);\n"
        "bool kest_ledger(KestRuntime *runtime, KestLedger *into);\n"
        "bool kest_native_crowded(KestRuntime *runtime, uint32_t which,\n"
        "                         uint32_t where, bool deep);\n"
        "bool kest_store_new(KestRuntime *runtime, uint16_t layout,\n"
        "                    int64_t room, uint32_t where, KestValue "
        "*into);\n"
        "bool kest_store_add(KestRuntime *runtime, KestValue handle,\n"
        "                    uint16_t stride, const KestValue *value,\n"
        "                    uint32_t where, int64_t *into);\n"
        "bool kest_store_get(KestRuntime *runtime, KestValue handle,\n"
        "                    int64_t which, uint16_t stride,\n"
        "                    KestValue *into, uint32_t where);\n"
        "bool kest_store_set(KestRuntime *runtime, KestValue handle,\n"
        "                    int64_t which, uint16_t stride,\n"
        "                    const KestValue *value, uint32_t where,\n"
        "                    bool *was);\n"
        "bool kest_store_remove(KestRuntime *runtime, KestValue handle,\n"
        "                       int64_t which, uint32_t where, bool *was);\n"
        "bool kest_store_count(KestRuntime *runtime, KestValue handle,\n"
        "                      uint32_t where, int64_t *into);\n"
        "bool kest_store_ref(KestRuntime *runtime, KestValue handle,\n"
        "                    int64_t index, uint32_t where, int64_t *into);\n"
        "bool kest_store_seek(KestRuntime *runtime, KestValue handle,\n"
        "                     int64_t from, uint32_t where, int64_t "
        "*found);\n"
        "bool kest_array_remove(KestRuntime *runtime, KestValue handle,\n"
        "                       int64_t index, uint32_t where);\n"
        "bool kest_array_push(KestRuntime *runtime, KestValue handle,\n"
        "                     uint16_t layout, const KestValue *value,\n"
        "                     uint32_t where);\n"
        "bool kest_array_new(KestRuntime *runtime, uint16_t layout,\n"
        "                    int64_t count, const KestValue *fill,\n"
        "                    uint32_t where, KestValue *into);\n"
        "int64_t kest_text_hash(const char *bytes, int64_t length);\n"
        "int64_t kest_text_order(const char *left, int64_t left_length,\n"
        "                        const char *right, int64_t right_length,\n"
        "                        int64_t *read);\n"
        "bool kest_text_at(KestRuntime *runtime, const char *bytes,\n"
        "                  int64_t length, int64_t index, uint32_t where,\n"
        "                  int64_t *into);\n"
        "bool kest_text_cut(KestRuntime *runtime, const char *bytes,\n"
        "                   int64_t length, int64_t from, int64_t count,\n"
        "                   uint32_t where, const char **at, int64_t *many);\n"
        "bool kest_text_rest(KestRuntime *runtime, const char *bytes,\n"
        "                    int64_t length, int64_t from, uint32_t where,\n"
        "                    const char **at, int64_t *many);\n"
        "bool kest_text_matches(KestRuntime *runtime, const char *bytes,\n"
        "                       int64_t length, int64_t at,\n"
        "                       const char *needle, int64_t needle_length,\n"
        "                       uint32_t where, int64_t *into);\n"
        "bool kest_text_find(KestRuntime *runtime, const char *bytes,\n"
        "                    int64_t length, const char *needle,\n"
        "                    int64_t needle_length, int64_t from,\n"
        "                    uint32_t where, int64_t *at, int64_t *found);\n"
        "typedef enum {\n"
        "    KEST_TEXT_OF_INT,\n"
        "    KEST_TEXT_OF_UNSIGNED,\n"
        "    KEST_TEXT_OF_REAL,\n"
        "    KEST_TEXT_OF_NARROW,\n"
        "    KEST_TEXT_OF_BOOL,\n"
        "} KestTextOf;\n"
        "bool kest_text_of(KestRuntime *runtime, uint8_t how, KestValue "
        "value,\n"
        "                  uint32_t where, KestValue *into);\n"
        "bool kest_text_of_value(KestRuntime *runtime, uint16_t layout,\n"
        "                        const KestValue *slots, uint32_t where,\n"
        "                        KestValue *into);\n"
        "bool kest_text_join(KestRuntime *runtime, const KestValue *pieces,\n"
        "                    uint16_t count, uint32_t where, "
        "KestValue *into);\n"
        "bool kest_text_from(KestRuntime *runtime, KestValue handle,\n"
        "                    uint32_t where, KestValue *into);\n"
        "bool kest_array_fit(KestRuntime *runtime, KestValue handle,\n"
        "                    uint16_t layout, const KestValue *value,\n"
        "                    uint32_t where, int64_t *put);\n"
        "bool kest_array_push_text(KestRuntime *runtime, KestValue handle,\n"
        "                          uint16_t layout, const char *bytes,\n"
        "                          int64_t length, uint32_t where);\n"
        "bool kest_array_fit_text(KestRuntime *runtime, KestValue handle,\n"
        "                         const char *bytes, int64_t length,\n"
        "                         uint32_t where, int64_t *put);\n"
        "bool kest_array_room(KestRuntime *runtime, KestValue handle,\n"
        "                     uint16_t layout, int64_t wanted,\n"
        "                     uint32_t where);\n"
        "bool kest_array_clear(KestRuntime *runtime, KestValue handle,\n"
        "                      uint32_t where);\n"
        "bool kest_array_pop(KestRuntime *runtime, KestValue handle,\n"
        "                    uint32_t where, unsigned char **at);\n"
        "bool kest_array_written(KestRuntime *runtime, uint16_t layout,\n"
        "                        uint16_t count, const KestValue *values,\n"
        "                        uint32_t where, KestValue *into);\n"
        "int64_t kest_value_hash(KestRuntime *runtime, uint16_t layout,\n"
        "                        const KestValue *slots);\n"
        "bool kest_value_same(KestRuntime *runtime, uint16_t layout,\n"
        "                     const KestValue *left, const KestValue "
        "*right);\n"
        "uint64_t kest_mix(uint64_t bits);\n\n");
    if (c->wants_library) {
        say(c, &file,
            "// And the two answers this file does not work out for itself:\n"
            "// what a number outside a width becomes, and what is left over\n"
            "// from dividing two floats. They are the machine's answers and\n"
            "// the folder's alike, so this calls them rather than being a\n"
            "// third copy.\n"
            "int64_t kest_real_to_int(uint16_t scalar, double value);\n"
            "double kest_left_over(double left, double right);\n\n");
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->bodies[i].written) {
            written++;
            continue;
        }
        say(c, &file, "// not written: %s -- %s%s\n",
            c->bodies[i].symbol == NULL ? "(no name)" : c->bodies[i].symbol,
            c->bodies[i].why == NULL ? "no reason" : c->bodies[i].why,
            c->bodies[i].handed_over ? ", and handed to the machine" : "");
    }
    say(c, &file, "// %u of %u bodies written\n\n", written, c->count);
    // A body the machine writes into the one that calls it is small and
    // makes no call, and the host's compiler is asked to do the same with
    // its C, where it can be asked: left to itself it kept `worthOf` out of
    // line in `rules`, and a call is a wall in front of everything around
    // it. See D1202.
    say(c, &file,
        "#if defined(__GNUC__)\n"
        "#define KEST_CARRIED inline __attribute__((always_inline))\n"
        "#else\n#define KEST_CARRIED inline\n#endif\n\n");

    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written && !c->bodies[i].handed_over) {
            continue;
        }
        write_head(c, &file, &c->bodies[i], i, c->bodies[i].written);
        say(c, &file, ";\n");
    }
    say(c, &file, "\n");
    // What the frame of each body takes on the machine's stack, which a call
    // asks for room for and raises the collector's reach to. Written here
    // rather than at the call, because by now every body is lowered and a
    // call forward is answered the same as a call back. See D1179.
    for (uint32_t i = 0; c->module != NULL && i < c->count &&
                         i < c->module->count;
         i++) {
        const KestChunk *chunk = c->module->functions[i];
        if (chunk == NULL) {
            continue;
        }
        say(c, &file, "#define KN_%u %uu\n", i,
            (unsigned)chunk->slot_count + (unsigned)chunk->stack_needed);
    }
    say(c, &file, "\n");
    // And whether each reads its arguments in the frame it is handed, which
    // is whether a caller keeping its operands in locals writes them there.
    // A body the machine runs is handed them as values and writes them in
    // itself. Known once every body is written, like the size. See D1198.
    for (uint32_t i = 0; i < c->count; i++) {
        say(c, &file, "#define KA_%u %u\n", i,
            c->bodies[i].written && c->bodies[i].reads_frame ? 1u : 0u);
    }
    say(c, &file, "\n");
    // And what a call hands over as values, out of every argument it has:
    // a call is written before the body it calls may be, so it names them
    // all and this keeps the ones the callee takes, each after a comma of
    // its own so that keeping none leaves nothing. See D1199.
    for (uint32_t i = 0; i < c->count; i++) {
        const Body *body = &c->bodies[i];
        say(c, &file, "#define KC_%u(", i);
        for (uint16_t p = 0; p < body->params; p++) {
            say(c, &file, "%sa%u", p == 0 ? "" : ", ", (unsigned)p);
        }
        say(c, &file, ")");
        for (uint16_t p = 0; p < body->params; p++) {
            if (takes_value(body, body->written, p)) {
                say(c, &file, " , a%u", (unsigned)p);
            }
        }
        say(c, &file, "\n");
    }
    say(c, &file, "\n");
    // And what a body needs standing at the top of the file: a run of
    // constants written into the program, which a body reads at an index and
    // cannot hold. See D1119.
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written || c->bodies[i].ahead.bytes == NULL) {
            continue;
        }
        say(c, &file, "%s", c->bodies[i].ahead.bytes);
    }
    say(c, &file, "\n");
    // The bodies this backend handed to the machine, each a C function of the
    // shape a call here expects: the arguments into the frame the caller
    // already made room for, one call in, and what came back read out of the
    // same frame. It is the whole of what a body the machine runs costs a
    // body written here. See D1105.
    for (uint32_t i = 0; i < c->count; i++) {
        const Body *body = &c->bodies[i];
        if (body->written || !body->handed_over) {
            continue;
        }
        say(c, &file, "// %s -- the machine runs this one\n",
            body->symbol == NULL ? "(no name)" : body->symbol);
        write_head(c, &file, body, i, false);
        say(c, &file, " {\n");
        for (uint16_t p = 0; p < body->params; p++) {
            say(c, &file, "    frame[%u] = a%u;\n", (unsigned)p, (unsigned)p);
        }
        say(c, &file,
            "    uint16_t gave = 0;\n"
            "    if (!kest_call_body(rt, %u, frame, %u, &gave)) {\n"
            "        return false;\n    }\n",
            i, (unsigned)body->params);
        for (uint16_t r = 0; r < body->results; r++) {
            say(c, &file, "    out[%u] = frame[%u];\n", (unsigned)r,
                (unsigned)r);
        }
        if (body->results == 0) {
            say(c, &file, "    (void)out;\n");
        }
        say(c, &file, "    return true;\n}\n");
    }
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written) {
            continue;
        }
        say(c, &file, "// %s\n",
            c->bodies[i].symbol == NULL ? "(no name)" : c->bodies[i].symbol);
        say(c, &file, "%s", c->bodies[i].wrote.bytes);
    }

    // How many bodies this file holds, and how many times the machine
    // entered each of them. A check that cannot tell whether any of this ran
    // is a check that passes when none of it does, and two engines that
    // answer alike answer alike when one of them never started. It is one
    // increment at a crossing that already costs a call.
    uint32_t bound_count = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->bodies[i].written && c->bodies[i].symbol != NULL) {
            bound_count++;
        }
    }
    say(c, &file, "\nstatic uint64_t kest_entered[%u];\n\n",
        bound_count == 0 ? 1 : bound_count);

    // What the machine calls, which is not what the C calls. A body is
    // written here as the C it is, taking its arguments as values and
    // answering through a pointer; a call from the machine hands over a frame
    // and is handed back how many slots came of it. One of these a body, so
    // the shape the machine needs costs the C nothing.
    uint32_t bound_so_far = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        const Body *body = &c->bodies[i];
        if (!body->written) {
            continue;
        }
        say(c, &file,
            "static bool kn_%u(KestRuntime *rt, KestValue *frame,\n"
            "                 uint16_t *gave) {\n    kest_entered[%u]++;\n"
            "    if (!kf_%u(rt, frame, %s",
            i, bound_so_far++, i, body->results > 0 ? "frame" : "NULL");
        for (uint16_t p = 0; p < body->params; p++) {
            if (takes_value(body, true, p)) {
                say(c, &file, ", frame[%u]", (unsigned)p);
            }
        }
        say(c, &file,
            ")) {\n        return false;\n    }\n    *gave = %u;\n"
            "    return true;\n}\n",
            (unsigned)body->results);
    }

    // And which chunk each of them belongs to. The number is where the body
    // is in the module, which is what this file names them by; the name is
    // held against what is there, so a file written from another version of
    // the program refuses rather than putting one body's C under another
    // body's name.
    say(c, &file,
        "\nstatic bool bound(KestRuntime *rt) {\n    return true");
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written || c->bodies[i].symbol == NULL) {
            continue;
        }
        say(c, &file, " &&\n           kest_native_at(rt, %u, \"%s\", kn_%u)",
            i, c->bodies[i].symbol, i);
    }
    say(c, &file,
        ";\n}\n"
        "\n// And the way a host of its own uses this file. It makes a\n"
        "// machine for the same program, hands it to this, and the bodies\n"
        "// above are the ones that machine runs -- which is the whole of\n"
        "// what shipping a program compiled this way is. A host that embeds\n"
        "// two programs written this way compiles one of them with\n"
        "// `-DKEST_BOUND=some_other_name`, because two programs' bodies\n"
        "// under one name is a name that says nothing, and a host that has\n"
        "// a `main` of its own compiles with `-DKEST_NO_MAIN`. See D1111.\n"
        "#ifndef KEST_BOUND\n#define KEST_BOUND kest_natives_here\n#endif\n"
        "bool KEST_BOUND(KestRuntime *rt);\n"
        "bool KEST_BOUND(KestRuntime *rt) {\n    return bound(rt);\n}\n");

    // And the way in: this file is a host of the program it was written from,
    // because half of that program may still be the machine's to run. It
    // builds the same program, binds what it wrote, and calls `main` -- and
    // what answers `main` is whichever of the two engines holds it.
    const Body *way_in = NULL;
    for (uint32_t i = 0; entry != NULL && i < c->count; i++) {
        if (c->bodies[i].symbol != NULL &&
            strcmp(c->bodies[i].symbol, entry) == 0) {
            way_in = &c->bodies[i];
            break;
        }
    }
    // The one door a program needs to say anything, provided here so that a
    // program that writes a line is a program this can run. Everything else a
    // host provides -- a clock, a file, what the process was started with,
    // whatever an engine offers -- is the host's to bind and is not bound
    // here: a file this backend wrote is half a program rather than an engine.
    // See D1094.
    say(c, &file,
        "\n#ifndef KEST_NO_MAIN\n"
        "// What `std.io` asks the host for. Written by its length rather\n"
        "// than to a nought, because a piece of text cut out of the middle "
        "of\n// another does not end in one.\n"
        "// The arithmetic `std.math` asks the host for. A host is whoever\n"
        "// runs the program and these are its to provide, the same as the\n"
        "// command line provides them: a program that runs under `kest run`\n"
        "// runs here. Each is one line, because each is one of the host's\n"
        "// own machine's. See D1109.\n"
        "#define KEST_ONE_REAL(name, what)                                  \\\n"
        "    static void name(KestValue *frame, KestRuntime *runtime,       \\\n"
        "                     void *context) {                              \\\n"
        "        (void)runtime;                                             \\\n"
        "        (void)context;                                             \\\n"
        "        frame[0].real = what;                                      \\\n"
        "    }\n"
        "KEST_ONE_REAL(did_sqrt, sqrt(frame[0].real))\n"
        "KEST_ONE_REAL(did_floor, floor(frame[0].real))\n"
        "KEST_ONE_REAL(did_ceil, ceil(frame[0].real))\n"
        "\n"
        "// The clock, the arguments and the files. A host is whoever runs\n"
        "// the program and these are its to provide, the same as the\n"
        "// command line provides them; what this file runs is what `kest\n"
        "// run` runs, and eight programs in this tree could be written\n"
        "// whole as C and still not run without them. What the program was\n"
        "// started with is whatever follows the file it was given, which is\n"
        "// where a command line puts them too. See D1116.\n"
        "static int program_args = 0;\n"
        "static char **program_arg = NULL;\n"
        "\n"
        "static void did_clock(KestValue *frame, KestRuntime *runtime,\n"
        "                      void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    struct timespec now;\n"
        "    if (timespec_get(&now, TIME_UTC) != TIME_UTC) {\n"
        "        frame[0].integer = 0;\n        return;\n    }\n"
        "    frame[0].integer = (int64_t)now.tv_sec * 1000000 +\n"
        "                       (int64_t)(now.tv_nsec / 1000);\n"
        "}\n"
        "\n"
        "static void did_arg_count(KestValue *frame, KestRuntime *runtime,\n"
        "                          void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    frame[0].integer = program_args;\n"
        "}\n"
        "\n"
        "static void did_arg(KestValue *frame, KestRuntime *runtime,\n"
        "                    void *context) {\n"
        "    (void)context;\n"
        "    int at = (int)frame[0].integer;\n"
        "    if (program_arg == NULL || at < 0 || at >= program_args) {\n"
        "        kest_text(runtime, \"\", 0, frame);\n"
        "        frame[2].integer = 0;\n        return;\n    }\n"
        "    kest_text(runtime, program_arg[at],\n"
        "              (uint32_t)strlen(program_arg[at]), frame);\n"
        "    frame[2].integer = 1;\n"
        "}\n"
        "\n"
        "// The path a door was handed, as C holds one. A path longer than\n"
        "// this is a path this host answers nothing for, which is what it\n"
        "// answers for a path that is not there.\n"
        "static bool named_it(const KestValue *frame, char *into,\n"
        "                     size_t room) {\n"
        "    uint32_t wide = 0;\n"
        "    const char *named = kest_text_bytes(frame, &wide);\n"
        "    if (named == NULL || (size_t)wide >= room) {\n"
        "        return false;\n    }\n"
        "    memcpy(into, named, wide);\n"
        "    into[wide] = '\\0';\n"
        "    return true;\n"
        "}\n"
        "\n"
        "static void did_file_read(KestValue *frame, KestRuntime *runtime,\n"
        "                          void *context) {\n"
        "    (void)context;\n"
        "    char path[4096];\n"
        "    FILE *reading = named_it(frame, path, sizeof path)\n"
        "                        ? fopen(path, \"rb\")\n"
        "                        : NULL;\n"
        "    if (reading == NULL) {\n"
        "        kest_text(runtime, \"\", 0, frame);\n"
        "        frame[2].integer = 0;\n        return;\n    }\n"
        "    size_t room = 4096;\n"
        "    size_t held = 0;\n"
        "    char *bytes = malloc(room);\n"
        "    while (bytes != NULL) {\n"
        "        size_t read = fread(bytes + held, 1, room - held, reading);\n"
        "        held += read;\n"
        "        if (held < room) {\n            break;\n        }\n"
        "        char *grown = realloc(bytes, room * 2);\n"
        "        if (grown == NULL) {\n"
        "            free(bytes);\n            bytes = NULL;\n"
        "            break;\n        }\n"
        "        bytes = grown;\n        room *= 2;\n    }\n"
        "    // A nought among the bytes would make text that stops early,\n"
        "    // and text that stops early is a file read as less than it is.\n"
        "    bool wrong = bytes == NULL || ferror(reading) ||\n"
        "                 memchr(bytes, 0, held) != NULL;\n"
        "    fclose(reading);\n"
        "    if (wrong) {\n"
        "        free(bytes);\n"
        "        kest_text(runtime, \"\", 0, frame);\n"
        "        frame[2].integer = 0;\n        return;\n    }\n"
        "    kest_text(runtime, bytes, (uint32_t)held, frame);\n"
        "    frame[2].integer = 1;\n"
        "    free(bytes);\n"
        "}\n"
        "\n"
        "static void did_file_write(KestValue *frame, KestRuntime *runtime,\n"
        "                           void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    char path[4096];\n"
        "    uint32_t length = 0;\n"
        "    const char *bytes = kest_text_bytes(frame + 2, &length);\n"
        "    FILE *writing = bytes != NULL &&\n"
        "                            named_it(frame, path, sizeof path)\n"
        "                        ? fopen(path, \"wb\")\n"
        "                        : NULL;\n"
        "    if (writing == NULL) {\n"
        "        frame[0].integer = 0;\n        return;\n    }\n"
        "    bool wrote = fwrite(bytes, 1, length, writing) == length;\n"
        "    frame[0].integer = (fclose(writing) == 0 && wrote) ? 1 : 0;\n"
        "}\n"
        "\n"
        "static void did_file_exists(KestValue *frame, KestRuntime *runtime,\n"
        "                            void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    char path[4096];\n"
        "    FILE *there = named_it(frame, path, sizeof path)\n"
        "                      ? fopen(path, \"rb\")\n"
        "                      : NULL;\n"
        "    if (there != NULL) {\n"
        "        fclose(there);\n"
        "        frame[0].integer = 1;\n        return;\n    }\n"
        "    frame[0].integer = 0;\n"
        "}\n"
        "\n"
        "static void wrote_it(KestValue *frame, KestRuntime *runtime,\n"
        "                     void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    uint32_t length = 0;\n"
        "    const char *bytes = kest_text_bytes(frame, &length);\n"
        "    if (bytes != NULL && length > 0) {\n"
        "        fwrite(bytes, 1, length, stdout);\n"
        "    }\n"
        "}\n");
    say(c, &file,
        "\n// How much of a run was this file's, for whoever asks: a program\n"
        "// that stopped is a program half of which may still have run, so it\n"
        "// is said on the way out either way.\n"
        "static void said_how_much(const char *saying) {\n"
        "    if (saying == NULL) {\n        return;\n    }\n"
        "    uint32_t ran = 0;\n"
        "    uint64_t times = 0;\n"
        "    for (size_t i = 0; i < sizeof(kest_entered) /\n"
        "                           sizeof(kest_entered[0]); i++) {\n"
        "        times += kest_entered[i];\n"
        "        if (kest_entered[i] > 0) {\n            ran++;\n        }\n"
        "    }\n"
        "    fprintf(stderr, \"natives: %u written, %%u entered, %%llu "
        "time(s)\\n\",\n"
        "            ran, (unsigned long long)times);\n"
        "}\n"
        "\nint main(int argc, char **argv) {\n",
        bound_count);
    if (carried != NULL) {
        write_carried_main(c, &file, carried, carried_count, library);
    } else {
        write_reading_main(c, &file, from);
    }
    say(c, &file,
        "    KestHost *host = kest_host_new();\n"

        "    if (host == NULL ||\n"
        "        !kest_host_bind(host, \"Io.write\", wrote_it, NULL) ||\n"
        "        !kest_host_bind(host, \"Math.sqrt\", did_sqrt, NULL) ||\n"
        "        !kest_host_bind(host, \"Math.floor\", did_floor, NULL) ||\n"
        "        !kest_host_bind(host, \"Math.ceil\", did_ceil, NULL) ||\n"
        "        !kest_host_bind(host, \"Host.sqrt\", did_sqrt, NULL) ||\n"
        "        !kest_host_bind(host, \"Host.clock\", did_clock, NULL) ||\n"
        "        !kest_host_bind(host, \"Host.argCount\", did_arg_count,\n"
        "                        NULL) ||\n"
        "        !kest_host_bind(host, \"Host.arg\", did_arg, NULL) ||\n"
        "        !kest_host_bind(host, \"Host.fileRead\", did_file_read,\n"
        "                        NULL) ||\n"
        "        !kest_host_bind(host, \"Host.fileWrite\", did_file_write,\n"
        "                        NULL) ||\n"
        "        !kest_host_bind(host, \"Host.fileExists\", did_file_exists,\n"
        "                        NULL)) {\n"
        "        fprintf(stderr, \"there is no room for a host\\n\");\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"

        "    KestRuntime *rt = kest_start(build, host, NULL);\n"
        "    kest_host_free(host);\n"
        "    if (rt == NULL) {\n"
        "        kest_build_report(build, stderr, KEST_FORM_TEXT);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    if (!bound(rt)) {\n"
        "        fprintf(stderr, \"this C was written from another `%%s`\\n\","
        "\n                path);\n"
        "        kest_runtime_free(rt);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    // How much of this run was this file's, when somebody asks.\n"
        "    const char *saying = getenv(\"KEST_NATIVES\");\n"
        "    int32_t entry = kest_entry(rt, KEST_MAIN);\n"
        "    KestValue frame[8] = {{0}};\n"
        "    if (entry < 0 || !kest_call(rt, entry, frame,\n"
        "                                sizeof(frame) / sizeof(frame[0]))) "
        "{\n"
        "        kest_report(rt, stderr, KEST_FORM_TEXT);\n"
        "        said_how_much(saying);\n"
        "        kest_runtime_free(rt);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    said_how_much(saying);\n");
    // The status a run answers with, which is the command line's rule rather
    // than the language's: a file this backend wrote is a host of the same
    // program, and a host that answered something else would be two programs
    // rather than one compiled two ways.
    if (way_in != NULL && way_in->results == 1) {
        say(c, &file,
            "    int64_t answer = frame[0].integer;\n"
            "    kest_runtime_free(rt);\n"
            "    kest_build_free(build);\n"
            "    if (answer < 0 || answer > 255) {\n"
            "        fprintf(stderr,\n"
            "                \"`main` answered %%lld, and an exit status "
            "carries 0 to 255\\n\",\n"
            "                (long long)answer);\n"
            "        return 1;\n"
            "    }\n"
            "    return (int)answer;\n}\n");
    } else {
        say(c, &file,
            "    kest_runtime_free(rt);\n"
            "    kest_build_free(build);\n"
            "    return 0;\n}\n");
    }
    say(c, &file, "#endif\n");
    if (c->out_of_memory || file.bytes == NULL) {
        return NULL;
    }
    return file.bytes;
}
