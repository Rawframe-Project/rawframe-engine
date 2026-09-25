#include "verify.h"

#include <stdio.h>
#include <string.h>

// What a walk says when there was no room to take it in, which is a thing
// about the machine and not about the chunk. See D1251.
#define WANTED_ROOM "could not be checked for want of memory"

// Which chunk first reaches the heap, following calls, or -1. `where` is left
// at the instruction that does it.
// Where a promise is broken in what was emitted, and which chunk it is in. The
// same walk for both promises, because the thing that differs is one
// instruction: what reaches the heap is a list of them, and what reaches the
// host is `KEST_OP_CALL_HOST` and nothing else. See D853.
static int32_t breaks_in(const KestModule *module, uint32_t which,
                         uint8_t *state, uint32_t *where, int about) {
    if (state[which] != 0) {
        return -1;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (about == 0 && kest_op_allocates(op)) {
            *where = at;
            return (int32_t)which;
        }
        if (about == 1 && op == KEST_OP_CALL_HOST) {
            *where = at;
            return (int32_t)which;
        }
        // And a crossing out of the simulation profile, which is a crossing to
        // a door that does not promise it is inside one. `no.host` refuses
        // every crossing; this refuses the ones nobody has vouched for.
        // See D942.
        if (about == 2 && op == KEST_OP_CALL_HOST) {
            uint16_t door = kest_chunk_u16(chunk, at + 1);
            if (door >= module->extern_count ||
                !module->externs[door].deterministic) {
                *where = at;
                return (int32_t)which;
            }
        }
        if (op == KEST_OP_CALL) {
            uint16_t callee = kest_chunk_u16(chunk, at + 1);
            if (callee < module->count) {
                int32_t found =
                    breaks_in(module, callee, state, where, about);
                if (found >= 0) {
                    return found;
                }
            }
        }
        at += kest_op_wide(op);
    }
    return -1;
}

static bool starts_at(const uint8_t *starts, uint32_t where) {
    return ((unsigned)starts[where / 8] >> (where % 8)) & 1u;
}

// Whether every number every instruction of a body carries is one the body or
// the module has, and every jump lands where an instruction starts: what the
// machine reads without asking, proved before it runs. Answers the code and
// says what was wrong into `said`, or answers NULL. A body whose instructions
// cannot be told apart has already been refused by the walk above this, which
// is what makes the table of where each one starts worth building. See D1237.
static const char *names_only_what_is_there(const KestModule *module,
                                            const KestChunk *chunk,
                                            uint8_t *starts, char *said,
                                            size_t room) {
    memset(starts, 0, ((size_t)chunk->code_count + 8) / 8);
    for (uint32_t at = 0; at < chunk->code_count; at += kest_op_wide(chunk->code[at])) {
        starts[at / 8] = (uint8_t)(starts[at / 8] | (1u << (at % 8)));
    }
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        uint32_t wide = kest_op_wide(op);
        const char *name = kest_op_name(op);
        uint32_t previous = 0;
        for (uint32_t k = 0; k < (wide - 1) / 2; k++) {
            uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
            const char *wrong = NULL;
            uint32_t has = 0;
            switch (kest_op_operand(op, k)) {
            case KEST_OPERAND_NUMBER:
                break;
            case KEST_OPERAND_SLOT:
                has = chunk->slot_count;
                wrong = value < has ? NULL : "slot";
                break;
            case KEST_OPERAND_SLOT_RUN:
                has = chunk->slot_count;
                wrong = previous + value <= has ? NULL : "run of slots";
                break;
            case KEST_OPERAND_CONSTANT:
                has = chunk->constant_count;
                wrong = value < has ? NULL : "constant";
                break;
            case KEST_OPERAND_CONSTANT_RUN:
                has = chunk->constant_count;
                wrong = previous + value <= has ? NULL : "run of constants";
                break;
            case KEST_OPERAND_FUNCTION:
                has = module->count;
                wrong = value < has ? NULL : "function";
                break;
            case KEST_OPERAND_EXTERN:
                has = module->extern_count;
                wrong = value < has ? NULL : "door of the host";
                break;
            case KEST_OPERAND_LAYOUT:
                has = module->layout_count;
                wrong = value < has ? NULL : "layout";
                break;
            case KEST_OPERAND_FORWARD: {
                uint32_t lands = at + wide + value;
                if (lands >= chunk->code_count || !starts_at(starts, lands)) {
                    snprintf(said, room,
                             "`%s` at %u jumps to %u, where no instruction "
                             "starts",
                             name, at, lands);
                    return "K0409";
                }
                break;
            }
            case KEST_OPERAND_BACKWARD:
                if (value > at + wide || !starts_at(starts, at + wide - value)) {
                    snprintf(said, room,
                             "`%s` at %u jumps back %u, to where no "
                             "instruction starts",
                             name, at, value);
                    return "K0409";
                }
                break;
            }
            if (wrong != NULL) {
                snprintf(said, room,
                         "`%s` at %u names %s %u of the %u there are",
                         name, at, wrong,
                         kest_op_operand(op, k) == KEST_OPERAND_SLOT_RUN ||
                                 kest_op_operand(op, k) == KEST_OPERAND_CONSTANT_RUN
                             ? previous + value
                             : value,
                         has);
                return "K0408";
            }
            previous = value;
        }
        // Three read a run with a stride between its pieces, and what they
        // read last is the first plus the stride times the count.
        if (op == KEST_OP_CONST_AT || op == KEST_OP_LOAD_SLOTS ||
            op == KEST_OP_STORE_SLOTS) {
            uint32_t first = kest_chunk_u16(chunk, at + 1);
            uint32_t reach = first + kest_chunk_u16(chunk, at + 3) * kest_chunk_u16(chunk, at + 5);
            uint32_t has = op == KEST_OP_CONST_AT ? chunk->constant_count
                                                  : chunk->slot_count;
            if (reach > has) {
                snprintf(said, room,
                         "`%s` at %u reads to %u of the %u there are",
                         name, at, reach, has);
                return "K0408";
            }
        }
        // Four write a value of a layout into the frame from a slot, and one
        // reads a piece of text, which is two: what they reach is the slot
        // and the width. The machine's own guard asks this only in the build
        // that checks itself.
        uint32_t into = UINT32_MAX;
        uint32_t width = 0;
        if (op == KEST_OP_INDEX_TO || op == KEST_OP_INDEX_TO_LL) {
            into = kest_chunk_u16(chunk, at + 3);
            width = module->layouts[kest_chunk_u16(chunk, at + 1)].slots;
        } else if (op == KEST_OP_ELEM_FROM || op == KEST_OP_ELEM_FROM_LL) {
            into = kest_chunk_u16(chunk, at + 5);
            width = module->layouts[kest_chunk_u16(chunk, at + 3)].slots;
        } else if (op == KEST_OP_TEXT_IN) {
            into = kest_chunk_u16(chunk, at + 1);
            width = 2;
        }
        if (into != UINT32_MAX && into + width > chunk->slot_count) {
            snprintf(said, room,
                     "`%s` at %u reaches to slot %u of the %u there are",
                     name, at, into + width, chunk->slot_count);
            return "K0408";
        }
        at += wide;
    }
    return NULL;
}

// Where the operand stack stands at every instruction a body can reach, by
// walking every path from the first: an instruction is reached at one depth
// or it is refused, never takes more than is there, never leaves more than the
// body was given room for, and a `return` hands back exactly what the
// declaration says with nothing under it. The machine moves `top` without
// asking any of that, so this is what makes an operand an operand. What cannot
// be reached is not walked, because nothing runs it. See D1239.
static const char *stack_on_every_path(const KestModule *module,
                                       const KestChunk *chunk,
                                       uint16_t *depth, uint32_t *work,
                                       char *said, size_t room) {
    const uint16_t unknown = UINT16_MAX;
    for (uint32_t at = 0; at < chunk->code_count; at++) {
        depth[at] = unknown;
    }
    uint32_t waiting = 0;
    if (chunk->code_count > 0) {
        depth[0] = 0;
        work[waiting++] = 0;
    }
    while (waiting > 0) {
        uint32_t at = work[--waiting];
        while (true) {
            uint8_t op = chunk->code[at];
            uint32_t wide = kest_op_wide(op);
            const char *name = kest_op_name(op);
            uint32_t here = depth[at];
            uint32_t takes = 0;
            uint32_t gives = 0;
            const char *why = kest_op_stack(module, chunk, at, &takes, &gives);
            if (why != NULL) {
                snprintf(said, room, "`%s` at %u %s", name, at,
                         why);
                return "K0410";
            }
            if (takes > here) {
                snprintf(said, room,
                         "`%s` at %u takes %u slot(s) and %u are there",
                         name, at, takes, here);
                return "K0410";
            }
            uint32_t after = here - takes + gives;
            if (after > chunk->stack_needed) {
                snprintf(said, room,
                         "`%s` at %u leaves %u slot(s) where the body was "
                         "given room for %u",
                         name, at, after, chunk->stack_needed);
                return "K0410";
            }
            if (op == KEST_OP_RETURN) {
                if (here != takes || takes != chunk->result_slots) {
                    snprintf(said, room,
                             "`return` at %u gives back %u slot(s) of %u "
                             "where the declaration gives %u",
                             at, takes, here, chunk->result_slots);
                    return "K0410";
                }
                break;
            }
            if (op == KEST_OP_STOP) {
                break;
            }
            // Where it may go besides the next instruction, and whether the
            // next is one of the places at all.
            uint32_t lands = UINT32_MAX;
            bool falls = op != KEST_OP_JUMP && op != KEST_OP_LOOP;
            for (uint32_t k = 0; k < (wide - 1) / 2; k++) {
                uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
                if (kest_op_operand(op, k) == KEST_OPERAND_FORWARD) {
                    lands = at + wide + value;
                } else if (kest_op_operand(op, k) == KEST_OPERAND_BACKWARD) {
                    lands = at + wide - value;
                }
            }
            if (lands != UINT32_MAX) {
                if (depth[lands] == unknown) {
                    depth[lands] = (uint16_t)after;
                    work[waiting++] = lands;
                } else if (depth[lands] != after) {
                    snprintf(said, room,
                             "`%s` at %u arrives at %u with %u slot(s) where "
                             "another way arrives with %u",
                             name, at, lands, after,
                             depth[lands]);
                    return "K0410";
                }
            }
            if (!falls) {
                break;
            }
            uint32_t next = at + wide;
            if (next >= chunk->code_count) {
                snprintf(said, room, "`%s` at %u runs off the end",
                         name, at);
                return "K0410";
            }
            if (depth[next] != unknown) {
                if (depth[next] != after) {
                    snprintf(said, room,
                             "`%s` at %u arrives at %u with %u slot(s) where "
                             "another way arrives with %u",
                             name, at, next, after, depth[next]);
                    return "K0410";
                }
                break;
            }
            depth[next] = (uint16_t)after;
            at = next;
        }
    }
    return NULL;
}

// What a slot can be holding, as far as a walk of the code can say. The
// machine keeps nothing beside a value that says what it is: a slot is eight
// bytes read as a number, as where a piece of text is, or as a handle, by
// whichever instruction reads it. So what one holds is proved here, before
// anything runs, for every slot at every instruction a body can reach. See
// D1242.
typedef enum {
    // Nothing this body wrote: what the frame before it left there, which may
    // be an address, and reading one is a value made out of the machine.
    HOLDS_JUNK,
    // Nought, which every kind below but an address reads safely: an empty
    // piece of text, a handle to nothing that every door refuses, the number.
    HOLDS_ZERO,
    HOLDS_NUMBER,
    // Where a piece of text is. The slot after it says how long it is, and
    // the two are read as one by every instruction that reads text.
    HOLDS_TEXT,
    HOLDS_LENGTH,
    // An array or a store the machine made, or nought, and what it holds.
    HOLDS_ARRAY,
    HOLDS_STORE,
    // Where an element is inside an array, and what is there.
    HOLDS_ADDRESS,
    // Which function of the program this is, and the type it is called as.
    HOLDS_FUNCTION,
    // The tag of an enum, with which of its cases it can be, and a slot of
    // what the case carries, which is what the tag that many slots before it
    // says it is. See D1242.
    HOLDS_TAG,
    HOLDS_PAYLOAD,
    // One of several of those: moved and kept, never read as any of them.
    HOLDS_ANY,
} Holds;

// What a slot holds and, for an array, a store, an address or a function, one
// more than the layout of what is in it, at it or what it is called as, nought
// where that is not known. A function known to be exactly one of the
// program's says which with `EXACTLY` beside its number instead. An address
// stepped along a run of things inside what it was at says the step and how
// many in the top half.
typedef uint64_t Kind;
#define KIND(holds, of) ((Kind)(holds) | ((Kind)(of) << 8))
#define HOLDS_OF(kind) ((Holds)((kind) & 0xFFu))
#define LAID_OF(kind) ((uint32_t)(((kind) >> 8) & 0xFFFFFFu))
#define STEPPED(kind) ((uint32_t)((kind) >> 32))
// What was made inside a block of working memory holds, in the top byte of
// the top half, how many blocks deep it was made: text, its length, an array,
// a store, and an enum's tag and what its cases carry, whose own counts -- the
// cases a tag can be, which slot of a case -- are in the three bytes below.
#define DEPTH_OF(kind) ((uint32_t)((kind) >> 56))
#define BELOW_DEPTH(kind) (STEPPED(kind) & 0xFFFFFFu)
#define ALL_CASES 0xFFFFFFu
// Where a piece of text is and how long it is, with `PAIRED` beside each where
// the two are one piece's: the length in the slot after the text, both marked,
// and marked only where both were made or moved together. A slot moved on its
// own, or a run cut between the two, is no longer marked.
#define PAIRED 1u
#define TEXT_KIND KIND(HOLDS_TEXT, PAIRED)
#define LENGTH_KIND KIND(HOLDS_LENGTH, PAIRED)
#define EXACTLY 0x800000u
// And a function type nothing laid out says which with `NAMED` beside its
// number among the types the walk has named.
#define NAMED 0x400000u

// What the walk of what every slot holds keeps between bodies: the module,
// and every enum a tag has named and every function type a function value has,
// by number -- an enum or a function type inside a struct has no layout of its
// own, and a tag or a function value names the type it is of.
typedef struct {
    const KestModule *module;
    const KestType **enums;
    uint32_t enum_count;
    uint32_t enum_room;
    // What proving is counted against, beside everything else compiling
    // does: a walk goes over a place once for every way into it that says
    // something new, which is more than once. See D1248.
    KestDiags *diags;
} Verifying;

// One more than the number of a type among the ones named so far, naming it
// if it is new; nought when there is no more room, which reads what it holds
// as one of several things.
static uint32_t enum_number(Verifying *v, const KestType *type) {
    for (uint32_t i = 0; i < v->enum_count; i++) {
        if (v->enums[i] == type) {
            return i + 1;
        }
    }
    for (uint32_t i = 0; i < v->enum_count; i++) {
        if (kest_type_equal(v->enums[i], type)) {
            return i + 1;
        }
    }
    if (v->enums == NULL || v->enum_count == v->enum_room) {
        return 0;
    }
    v->enums[v->enum_count++] = type;
    return v->enum_count;
}

// Whether a kind holds something the heap was asked for, which a block of
// working memory gives back when it closes.
static bool made_on_the_heap(Holds holds) {
    return holds == HOLDS_TEXT || holds == HOLDS_LENGTH ||
           holds == HOLDS_ARRAY || holds == HOLDS_STORE ||
           holds == HOLDS_TAG || holds == HOLDS_PAYLOAD;
}

// A kind made at least `depth` blocks deep.
static Kind deeper_of(Kind kind, uint32_t depth) {
    if (!made_on_the_heap(HOLDS_OF(kind)) || DEPTH_OF(kind) >= depth) {
        return kind;
    }
    return (kind & ~((Kind)0xFFu << 56)) | ((Kind)(depth & 0xFFu) << 56);
}

static bool same_enum(const Verifying *v, uint32_t a, uint32_t b) {
    return a == b || (a != 0 && b != 0 && kest_type_equal(v->enums[a - 1],
                                                          v->enums[b - 1]));
}

static const char *const HOLDS_NAMES[] = {
    "nothing this body wrote", "nought",         "a number",
    "text",                    "the length of text", "an array",
    "a store",                 "an address",     "a function",
    "a tag",                   "what a case carries", "one of several things",
};

typedef struct {
    const KestModule *module;
    Verifying *v;
    const KestChunk *chunk;
    // The body's own slots and then the operand stack, as far down as it is.
    Kind *now;
    uint32_t depth;
    // Room for what a value holds, as wide as the widest layout, and for a
    // run of slots being moved, as wide as a frame.
    Kind *laid;
    uint32_t laid_room;
    Kind *spare;
    // How many blocks of working memory are open here.
    uint32_t region;
    uint32_t at;
    const char *name;
    char *said;
    size_t room;
} Kinds;

// Whether two layouts are one type. The same type is one layout; two copies of
// a shape the compiler made separately are the same shape.
static bool same_laid(const KestModule *module, uint32_t a, uint32_t b) {
    if (a == b) {
        return true;
    }
    if (a == 0 || b == 0) {
        return false;
    }
    const KestType *one = module->layout_types[a - 1];
    const KestType *two = module->layout_types[b - 1];
    return one != NULL && two != NULL && kest_type_equal(one, two);
}

// Whether two function types are called alike: the same things taken and the
// same given back. What each promises is a question for the promises' own
// proof, not for what a slot holds.
static bool called_alike(const KestType *one, const KestType *two) {
    if (one == NULL || two == NULL || one->tag != KEST_T_FN ||
        two->tag != KEST_T_FN || one->param_count != two->param_count) {
        return false;
    }
    for (uint32_t i = 0; i < one->param_count; i++) {
        if (!kest_type_equal(one->params[i], two->params[i])) {
            return false;
        }
    }
    if (one->result == NULL || two->result == NULL) {
        return one->result == two->result;
    }
    return kest_type_equal(one->result, two->result);
}

// Whether a function of the program is called as a function type.
static bool chunk_called_as(const KestModule *module, const KestChunk *callee,
                            const KestType *type) {
    if (type == NULL || type->tag != KEST_T_FN ||
        type->param_count != callee->takes_count) {
        return false;
    }
    for (uint32_t p = 0; p < type->param_count; p++) {
        const KestType *taken = module->layout_types[callee->takes[p]];
        if (taken == NULL || !kest_type_equal(type->params[p], taken)) {
            return false;
        }
    }
    if (callee->result_slots > 0) {
        const KestType *given = module->layout_types[callee->gives];
        return type->result != NULL && given != NULL &&
               kest_type_equal(type->result, given);
    }
    return type->result == NULL || type->result->tag == KEST_T_VOID;
}

// The function type a slot holding a function says it is called as, or NULL
// where it says exactly which function instead, or nothing.
static const KestType *function_type(const Verifying *v, uint32_t of) {
    if (of == 0 || (of & EXACTLY)) {
        return NULL;
    }
    if (of & NAMED) {
        uint32_t which = of & ~NAMED;
        return which == 0 || which > v->enum_count ? NULL : v->enums[which - 1];
    }
    return v->module->layout_types[of - 1];
}

// Whether what a slot holding a function says it is is called as `type`.
static bool function_as(const Verifying *v, uint32_t of, const KestType *type) {
    const KestModule *module = v->module;
    if (of == 0) {
        return false;
    }
    if (of & EXACTLY) {
        uint32_t which = of & ~EXACTLY;
        return which < module->count &&
               chunk_called_as(module, module->functions[which], type);
    }
    return called_alike(function_type(v, of), type);
}

// Whether two functions of the program take and give the same.
static bool chunks_alike(const KestModule *module, const KestChunk *one,
                         const KestChunk *two) {
    if (one->takes_count != two->takes_count ||
        one->result_slots != two->result_slots) {
        return false;
    }
    for (uint32_t p = 0; p < one->takes_count; p++) {
        if (!same_laid(module, one->takes[p] + 1u, two->takes[p] + 1u)) {
            return false;
        }
    }
    return one->result_slots == 0 ||
           same_laid(module, one->gives + 1u, two->gives + 1u);
}

// What two slots holding functions are between them: the one both are called
// as, or nothing a call can be made through.
static uint32_t functions_joined(const Verifying *v, uint32_t a, uint32_t b) {
    const KestModule *module = v->module;
    if (a == b || a == 0 || b == 0) {
        return a == b ? a : 0;
    }
    if ((a & EXACTLY) && (b & EXACTLY)) {
        uint32_t one = a & ~EXACTLY;
        uint32_t two = b & ~EXACTLY;
        return one < module->count && two < module->count &&
                       chunks_alike(module, module->functions[one],
                                    module->functions[two])
                   ? a
                   : 0;
    }
    if (a & EXACTLY) {
        return function_as(v, a, function_type(v, b)) ? b : 0;
    }
    if (b & EXACTLY) {
        return function_as(v, b, function_type(v, a)) ? a : 0;
    }
    return called_alike(function_type(v, a), function_type(v, b)) ? a : 0;
}

// One more than the layout of a type, or nought where the program laid none.
static uint32_t laid_for(const KestModule *module, const KestType *type) {
    if (type == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *known = module->layout_types[i];
        if (known == type) {
            return i + 1;
        }
    }
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *known = module->layout_types[i];
        if (known != NULL && kest_type_equal(known, type)) {
            return i + 1;
        }
    }
    return 0;
}

static Kind joined(Verifying *v, Kind a, Kind b) {
    const KestModule *module = v->module;
    if (a == b) {
        return a;
    }
    Holds x = HOLDS_OF(a);
    Holds y = HOLDS_OF(b);
    if (x == HOLDS_JUNK || y == HOLDS_JUNK) {
        return HOLDS_JUNK;
    }
    if (x == HOLDS_ZERO || y == HOLDS_ZERO) {
        Kind other = x == HOLDS_ZERO ? b : a;
        Holds is = HOLDS_OF(other);
        // Nought is a function too -- the first one -- and nowhere near an
        // address, so neither is read as one after this.
        return is == HOLDS_ADDRESS || is == HOLDS_ANY || is == HOLDS_FUNCTION
                   ? KIND(HOLDS_ANY, 0)
                   : other;
    }
    if ((x == HOLDS_NUMBER && y == HOLDS_LENGTH) ||
        (x == HOLDS_LENGTH && y == HOLDS_NUMBER) ||
        (x == HOLDS_NUMBER && y == HOLDS_NUMBER)) {
        return HOLDS_NUMBER;
    }
    if (x == HOLDS_TAG && y == HOLDS_TAG && same_enum(v, LAID_OF(a), LAID_OF(b))) {
        return deeper_of(KIND(HOLDS_TAG, LAID_OF(a)) |
                             ((Kind)(BELOW_DEPTH(a) | BELOW_DEPTH(b)) << 32),
                         DEPTH_OF(a) > DEPTH_OF(b) ? DEPTH_OF(a) : DEPTH_OF(b));
    }
    // The same thing made at two depths is what was made deeper, and text
    // is one piece only where it was one piece on both ways.
    if (x == y && (x == HOLDS_TEXT || x == HOLDS_LENGTH)) {
        return deeper_of(KIND(x, LAID_OF(a) & LAID_OF(b) & PAIRED),
                         DEPTH_OF(a) > DEPTH_OF(b) ? DEPTH_OF(a) : DEPTH_OF(b));
    }
    if (x == y && x == HOLDS_PAYLOAD && LAID_OF(a) == LAID_OF(b) &&
        BELOW_DEPTH(a) == BELOW_DEPTH(b)) {
        return deeper_of(a, DEPTH_OF(b));
    }
    if ((x == HOLDS_TAG && y == HOLDS_NUMBER) ||
        (x == HOLDS_NUMBER && y == HOLDS_TAG)) {
        return HOLDS_NUMBER;
    }
    if (x == y && (x == HOLDS_ARRAY || x == HOLDS_STORE)) {
        return deeper_of(same_laid(module, LAID_OF(a), LAID_OF(b)) ? a : KIND(x, 0),
                         DEPTH_OF(a) > DEPTH_OF(b) ? DEPTH_OF(a) : DEPTH_OF(b));
    }
    if (x == HOLDS_ADDRESS && y == HOLDS_ADDRESS) {
        return KIND(HOLDS_ADDRESS, 0);
    }
    if (x == HOLDS_FUNCTION && y == HOLDS_FUNCTION) {
        return KIND(HOLDS_FUNCTION,
                    functions_joined(v, LAID_OF(a), LAID_OF(b)));
    }
    // A function value read as the number it is says nothing it should not.
    if ((x == HOLDS_FUNCTION || x == HOLDS_LENGTH || x == HOLDS_NUMBER) &&
        (y == HOLDS_FUNCTION || y == HOLDS_LENGTH || y == HOLDS_NUMBER)) {
        return HOLDS_NUMBER;
    }
    return HOLDS_ANY;
}

// What a value of a type holds, a slot at a time, from where `at` stands.
static void typed(Verifying *v, const KestType *type, Kind *into,
                  uint32_t *at, bool flat) {
    const KestModule *module = v->module;
    if (type == NULL) {
        into[(*at)++] = HOLDS_ANY;
        return;
    }
    switch (type->tag) {
    case KEST_T_STRUCT:
        if (type->member_count == 0) {
            into[(*at)++] = HOLDS_ZERO;
            return;
        }
        for (uint32_t i = 0; i < type->member_count; i++) {
            typed(v, type->members[i].type, into, at, flat);
        }
        return;
    case KEST_T_FIXED:
        for (uint32_t i = 0; i < type->count; i++) {
            typed(v, type->element, into, at, flat);
        }
        return;
    case KEST_T_TEXT:
        into[(*at)++] = TEXT_KIND;
        into[(*at)++] = LENGTH_KIND;
        return;
    case KEST_T_ARRAY:
        into[(*at)++] = KIND(HOLDS_ARRAY, laid_for(module, type->element));
        return;
    case KEST_T_STORE:
        into[(*at)++] = KIND(HOLDS_STORE, laid_for(module, type->element));
        return;
    case KEST_T_OPTIONAL: {
        // What is not there is nought, which text and a handle read safely,
        // so the value's slots are what the value's type says -- but for a
        // function, whose nought is the first one there is.
        uint32_t from = *at;
        typed(v, type->element, into, at, flat);
        for (uint32_t s = from; s < *at; s++) {
            if (HOLDS_OF(into[s]) == HOLDS_FUNCTION) {
                into[s] = HOLDS_ANY;
            }
        }
        into[(*at)++] = HOLDS_NUMBER;
        return;
    }
    case KEST_T_FN: {
        uint32_t laid = laid_for(module, type);
        if (laid == 0) {
            uint32_t named = enum_number(v, type);
            laid = named == 0 ? 0 : NAMED | named;
        }
        into[(*at)++] = KIND(HOLDS_FUNCTION, laid);
        return;
    }
    case KEST_T_ENUM: {
        // The tag, which may be any of the cases, and what they carry, which
        // is read through it: see `resolved`. Inside what a case carries, and
        // for an enum the program laid no layout for, what the cases carry is
        // a slot every case that carries anything there agrees about, and
        // nought where a case does not reach it.
        uint32_t laid = flat ? 0u : enum_number(v, type);
        if (laid != 0 && type->slots > 1) {
            uint32_t cases = type->case_count >= 24
                                 ? ALL_CASES
                                 : (uint32_t)((1u << type->case_count) - 1u);
            into[*at] = KIND(HOLDS_TAG, laid) | ((Kind)cases << 32);
            for (uint32_t k = 1; k < type->slots; k++) {
                into[*at + k] = KIND(HOLDS_PAYLOAD, laid) | ((Kind)k << 32);
            }
            *at += type->slots;
            return;
        }
        uint32_t tag = (*at)++;
        into[tag] = HOLDS_NUMBER;
        uint32_t payload = type->slots > 1 ? type->slots - 1u : 0u;
        for (uint32_t s = 0; s < payload; s++) {
            into[tag + 1 + s] = HOLDS_JUNK;
        }
        for (uint32_t c = 0; c < type->case_count; c++) {
            Kind carried[64];
            if (payload > 64) {
                for (uint32_t s = 0; s < payload; s++) {
                    into[tag + 1 + s] = HOLDS_ANY;
                }
                break;
            }
            for (uint32_t s = 0; s < payload; s++) {
                carried[s] = HOLDS_NUMBER;
            }
            const KestVariantType *variant = &type->cases[c];
            for (uint32_t p = 0; p < variant->payload_count; p++) {
                uint32_t piece = variant->offsets[p] - 1u;
                typed(v, variant->payload[p], carried, &piece, true);
            }
            for (uint32_t s = 0; s < payload; s++) {
                into[tag + 1 + s] =
                    c == 0 ? carried[s]
                           : joined(v, into[tag + 1 + s], carried[s]);
            }
        }
        *at = tag + (type->slots > 0 ? type->slots : 1u);
        return;
    }
    case KEST_T_VOID:
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_FLAGS:
    case KEST_T_REF:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
    case KEST_T_ERROR:
        into[(*at)++] = HOLDS_NUMBER;
        return;
    }
}

// What a value of a layout holds. A layout with no type is one slot nothing
// can say anything about.
static uint32_t laid_out(Verifying *v, uint32_t which, Kind *into) {
    const KestModule *module = v->module;
    const KestLayout *layout = &module->layouts[which];
    const KestType *type = module->layout_types[which];
    uint32_t at = 0;
    typed(v, type, into, &at, false);
    for (uint32_t s = at; s < layout->slots; s++) {
        into[s] = HOLDS_ZERO;
    }
    return layout->slots;
}

// Whether a value of layout `field` sits at `offset` bytes into a value of
// layout `whole`, which is what an instruction reading a field of an element
// in place takes for granted.
static bool field_of(const KestModule *module, const KestType *whole,
                     uint32_t offset, const KestType *field) {
    if (whole == NULL || field == NULL) {
        return false;
    }
    if (offset == 0 && kest_type_equal(whole, field)) {
        return true;
    }
    if (whole->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < whole->member_count; i++) {
            const KestMember *member = &whole->members[i];
            uint32_t size = member->type == NULL ? 0 : member->type->byte_size;
            if (offset >= member->byte_offset &&
                offset < member->byte_offset + (size > 0 ? size : 1u)) {
                return field_of(module, member->type,
                                offset - member->byte_offset, field);
            }
        }
        return false;
    }
    if (whole->tag == KEST_T_FIXED && whole->element != NULL &&
        whole->element->byte_size > 0) {
        return field_of(module, whole->element,
                        offset % whole->element->byte_size, field);
    }
    return false;
}

// Whether a tag that can be any of `cases` can be case `c`. Past the
// twenty-fourth, only a tag that can be anything can be one.
static bool case_in(uint32_t cases, uint32_t c) {
    return c >= 24 ? cases == ALL_CASES : ((cases >> c) & 1u) != 0;
}

// What each slot after an enum's tag holds where the tag says case `c`, into
// `carried`: what the case carries is laid out as it would be anywhere, an
// enum inside it read through its own tag, and a slot the case carries
// nothing in is a number -- nought where the machine made it, and never a
// piece of text or a handle, because every value of the enum made anywhere is
// held to that, and a reader of the case would read one as its address.
// Answers how many there are, or nought for more than there is room for.
static uint32_t case_payload(Verifying *v, const KestType *type, uint32_t c,
                             Kind *carried) {
    uint32_t payload = type->slots > 1 ? type->slots - 1u : 0u;
    if (payload > 64 || c >= type->case_count) {
        return 0;
    }
    for (uint32_t s = 0; s < payload; s++) {
        carried[s] = HOLDS_NUMBER;
    }
    const KestVariantType *variant = &type->cases[c];
    for (uint32_t p = 0; p < variant->payload_count; p++) {
        uint32_t piece = variant->offsets[p] - 1u;
        typed(v, variant->payload[p], carried, &piece, false);
    }
    return payload;
}

// What slot `k` of an enum holds where its tag says case `c`.
static Kind case_holds(Verifying *v, const KestType *type, uint32_t c,
                       uint32_t k) {
    Kind carried[64];
    uint32_t payload = case_payload(v, type, c, carried);
    return k == 0 || k > payload ? HOLDS_ANY : carried[k - 1];
}

// What a slot of what a case carries is, read through the tag that many slots
// before it: what every case the tag can be carries there. A slot whose tag
// is not where it should be is any of them. What a case carries may be an
// enum of its own, whose tag is read the same way, as deep as they go.
static Kind resolved_as(Verifying *v, const Kind *now, uint32_t region,
                        uint32_t pos, Kind kind, uint32_t deep) {
    if (HOLDS_OF(kind) != HOLDS_PAYLOAD) {
        return kind;
    }
    uint32_t laid = LAID_OF(kind);
    uint32_t k = BELOW_DEPTH(kind);
    const KestType *type = laid == 0 ? NULL : v->enums[laid - 1];
    if (deep == 0 || type == NULL || type->tag != KEST_T_ENUM ||
        type->case_count == 0) {
        return HOLDS_ANY;
    }
    uint32_t cases = ALL_CASES;
    if (pos >= region + k) {
        Kind tag = resolved_as(v, now, region, pos - k, now[pos - k], deep - 1);
        if (HOLDS_OF(tag) == HOLDS_TAG && same_enum(v, LAID_OF(tag), laid)) {
            cases = BELOW_DEPTH(tag);
        }
    }
    Kind all = HOLDS_JUNK;
    bool first = true;
    for (uint32_t c = 0; c < type->case_count; c++) {
        if (!case_in(cases, c)) {
            continue;
        }
        Kind one = case_holds(v, type, c, k);
        all = first ? one : joined(v, all, one);
        first = false;
    }
    if (first) {
        return HOLDS_ANY;
    }
    // What a case carries was made where the value it is part of was.
    all = deeper_of(all, DEPTH_OF(kind));
    return resolved_as(v, now, region, pos, all, deep - 1);
}

static Kind resolved(Verifying *v, const Kind *now, uint32_t region,
                     uint32_t pos) {
    return resolved_as(v, now, region, pos, now[pos], 8);
}

// What a use asks of a slot.
typedef enum {
    NEEDS_NUMBER,
    NEEDS_TEXT,
    NEEDS_LENGTH,
    NEEDS_ARRAY,
    NEEDS_STORE,
    NEEDS_ADDRESS,
    NEEDS_FUNCTION,
    NEEDS_ZERO,
    NEEDS_ANYTHING,
} Needs;

static const char *const NEEDS_NAMES[] = {
    "a number", "text",       "the length of text", "an array",
    "a store",  "an address", "a function", "nought", "a value",
};

// Whether a slot holding `has` is read safely as `wants`, of layout `of` where
// that is asked (one more than it; nought for any).
static bool will_do(const Verifying *v, Kind has, Needs wants, uint32_t of) {
    const KestModule *module = v->module;
    Holds is = HOLDS_OF(has);
    switch (wants) {
    case NEEDS_NUMBER:
        return is == HOLDS_ZERO || is == HOLDS_NUMBER || is == HOLDS_LENGTH ||
               is == HOLDS_FUNCTION || is == HOLDS_TAG;
    case NEEDS_ZERO:
        return is == HOLDS_ZERO;
    case NEEDS_TEXT:
        return is == HOLDS_ZERO || is == HOLDS_TEXT;
    case NEEDS_LENGTH:
        return is == HOLDS_ZERO || is == HOLDS_LENGTH;
    case NEEDS_ARRAY:
        return is == HOLDS_ZERO ||
               (is == HOLDS_ARRAY &&
                (of == 0 || same_laid(module, LAID_OF(has), of)));
    case NEEDS_STORE:
        return is == HOLDS_ZERO ||
               (is == HOLDS_STORE &&
                (of == 0 || same_laid(module, LAID_OF(has), of)));
    case NEEDS_ADDRESS:
        return is == HOLDS_ADDRESS &&
               (of == 0 || same_laid(module, LAID_OF(has), of));
    case NEEDS_FUNCTION:
        return is == HOLDS_FUNCTION && LAID_OF(has) != 0 &&
               (of == 0 || function_as(v, LAID_OF(has), function_type(v, of)));
    case NEEDS_ANYTHING:
        return is != HOLDS_JUNK;
    }
    return false;
}

#define SLOT(w, s) ((w)->now[(s)])
#define FROM_TOP(w, n) ((w)->now[(w)->chunk->slot_count + (w)->depth - (n)])

static const char *wrong_kind(Kinds *w, const char *where, uint32_t which,
                              Kind has, Needs wants) {
    snprintf(w->said, w->room, "`%s` at %u reads %s %u as %s and it holds %s",
             w->name, w->at, where, which, NEEDS_NAMES[wants],
             HOLDS_NAMES[HOLDS_OF(has)]);
    return "K0411";
}

// Whether a slot holding text and the slot after it are one piece of text:
// made or moved together, or a length of nought, which reads nothing wherever
// the text is.
static bool one_piece(Kind text, Kind length) {
    Holds t = HOLDS_OF(text);
    Holds l = HOLDS_OF(length);
    if (l == HOLDS_ZERO) {
        return t == HOLDS_ZERO || t == HOLDS_TEXT;
    }
    return t == HOLDS_TEXT && l == HOLDS_LENGTH && (LAID_OF(text) & PAIRED) &&
           (LAID_OF(length) & PAIRED);
}

static const char *not_one_piece(Kinds *w, uint32_t where) {
    snprintf(w->said, w->room,
             "`%s` at %u reads text and a length at %u that are not one "
             "piece's",
             w->name, w->at, where);
    return "K0411";
}

// A slot moved on its own is not half of a piece any more.
static Kind alone(Kind kind) {
    Holds is = HOLDS_OF(kind);
    if (is == HOLDS_TEXT || is == HOLDS_LENGTH) {
        return kind & ~((Kind)PAIRED << 8);
    }
    return kind;
}

static const char *needs_top_of(Kinds *w, uint32_t n, Needs wants,
                                uint32_t of) {
    Kind has = resolved(w->v, w->now, w->chunk->slot_count,
                        w->chunk->slot_count + w->depth - n);
    return will_do(w->v, has, wants, of)
               ? NULL
               : wrong_kind(w, "the slot from the top", n, has, wants);
}

static const char *needs_top(Kinds *w, uint32_t n, Needs wants) {
    return needs_top_of(w, n, wants, 0);
}

static const char *needs_slot_of(Kinds *w, uint32_t s, Needs wants,
                                 uint32_t of) {
    Kind has = resolved(w->v, w->now, 0, s);
    return will_do(w->v, has, wants, of)
               ? NULL
               : wrong_kind(w, "slot", s, has, wants);
}

static const char *needs_slot(Kinds *w, uint32_t s, Needs wants) {
    return needs_slot_of(w, s, wants, 0);
}

static void push(Kinds *w, Kind kind) {
    w->now[w->chunk->slot_count + w->depth] = kind;
    w->depth++;
}

static Needs wanted_by(Kind laid, uint32_t *of) {
    *of = LAID_OF(laid);
    switch (HOLDS_OF(laid)) {
    case HOLDS_TEXT:
        return NEEDS_TEXT;
    case HOLDS_LENGTH:
        return NEEDS_LENGTH;
    case HOLDS_ARRAY:
        return NEEDS_ARRAY;
    case HOLDS_STORE:
        return NEEDS_STORE;
    case HOLDS_ANY:
        return NEEDS_ANYTHING;
    case HOLDS_ADDRESS:
        return NEEDS_ADDRESS;
    case HOLDS_FUNCTION:
        return NEEDS_FUNCTION;
    case HOLDS_ZERO:
        return NEEDS_ZERO;
    case HOLDS_TAG:
    case HOLDS_PAYLOAD:
    case HOLDS_JUNK:
    case HOLDS_NUMBER:
        break;
    }
    return NEEDS_NUMBER;
}

// Whether the enum of layout `laid` whose tag is at `pos` of the stack is one:
// what its tag can be, and what each of those cases carries in every slot --
// or what a case carries still read through the tag it came with. A case
// carries nought where it carries nothing, because the next reader reads
// that slot as nought.
static const char *fits_enum(Kinds *w, uint32_t laid, uint32_t pos) {
    Verifying *v = w->v;
    const KestType *type = v->enums[laid - 1];
    uint32_t region = w->chunk->slot_count;
    Kind tag = w->now[pos];
    Holds is = HOLDS_OF(tag);
    uint32_t cases = ALL_CASES;
    bool whole = false;
    if (is == HOLDS_TAG && same_enum(v, LAID_OF(tag), laid)) {
        cases = BELOW_DEPTH(tag);
        whole = true;
    } else if (is == HOLDS_ZERO) {
        cases = 1u;
    } else if (is == HOLDS_NUMBER && STEPPED(tag) != 0) {
        uint32_t value = STEPPED(tag) - 1u;
        if (value >= type->case_count) {
            snprintf(w->said, w->room,
                     "`%s` at %u makes a value of an enum with case %u, "
                     "which it has not got",
                     w->name, w->at, value);
            return "K0411";
        }
        cases = value < 24 ? 1u << value : ALL_CASES;
    } else if (!will_do(v, tag, NEEDS_NUMBER, 0)) {
        return wrong_kind(w, "the tag at", pos, tag, NEEDS_NUMBER);
    }
    // Moved as a whole with the tag it came with: read through it as it is.
    bool as_it_was = whole;
    for (uint32_t k = 1; as_it_was && k < type->slots; k++) {
        Kind raw = w->now[pos + k];
        as_it_was = HOLDS_OF(raw) == HOLDS_PAYLOAD && BELOW_DEPTH(raw) == k &&
                    same_enum(v, LAID_OF(raw), laid);
    }
    if (as_it_was) {
        return NULL;
    }
    for (uint32_t c = 0; c < type->case_count; c++) {
        if (!case_in(cases, c)) {
            continue;
        }
        Kind carried[64];
        uint32_t payload = case_payload(v, type, c, carried);
        if (payload + 1u < type->slots) {
            return wrong_kind(w, "what a case carries at", pos + 1,
                              HOLDS_ANY, NEEDS_ANYTHING);
        }
        for (uint32_t k = 1; k <= payload; k++) {
            Kind want = carried[k - 1];
            if (HOLDS_OF(want) == HOLDS_TAG) {
                const char *wrong = fits_enum(w, LAID_OF(want), pos + k);
                if (wrong != NULL) {
                    return wrong;
                }
                k += v->enums[LAID_OF(want) - 1]->slots - 1u;
                continue;
            }
            Kind has = resolved(v, w->now, region, pos + k);
            uint32_t of = 0;
            Needs wants = wanted_by(want, &of);
            if (!will_do(v, has, wants, of)) {
                return wrong_kind(w, "what a case carries at", pos + k, has,
                                  wants);
            }
            if (HOLDS_OF(want) == HOLDS_TEXT && k < payload &&
                !one_piece(has, resolved(v, w->now, region, pos + k + 1))) {
                return not_one_piece(w, pos + k);
            }
        }
    }
    return NULL;
}

// Whether the slots from `n_from_top` down to the top are a value of this
// layout, which is what writing one into memory or handing it over asks.
static const char *fits_top(Kinds *w, uint32_t which, uint32_t n_from_top) {
    uint32_t slots = laid_out(w->v, which, w->laid);
    uint32_t base = w->chunk->slot_count + w->depth - n_from_top;
    for (uint32_t s = 0; s < slots; s++) {
        Kind laid = w->laid[s];
        if (HOLDS_OF(laid) == HOLDS_TAG) {
            uint32_t of = LAID_OF(laid);
            const char *wrong = fits_enum(w, of, base + s);
            if (wrong != NULL) {
                return wrong;
            }
            // `fits_enum` does not touch `laid`, so what is left of it is
            // still this layout's.
            s += w->v->enums[of - 1]->slots - 1u;
            continue;
        }
        uint32_t of = 0;
        Needs wants = wanted_by(laid, &of);
        const char *wrong = needs_top_of(w, n_from_top - s, wants, of);
        if (wrong != NULL) {
            return wrong;
        }
        if (HOLDS_OF(laid) == HOLDS_TEXT && s + 1 < slots &&
            !one_piece(resolved(w->v, w->now, w->chunk->slot_count, base + s),
                       resolved(w->v, w->now, w->chunk->slot_count,
                                base + s + 1))) {
            return not_one_piece(w, base + s);
        }
    }
    return NULL;
}

// A run of `count` slots copied from `from` to `to`, either of which may be in
// the frame or on the stack: what a case carries is still read through its
// tag where the tag came along, and is what the tag said where it did not.
static void carried_over(Kinds *w, uint32_t from, uint32_t from_region,
                         uint32_t to, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        Kind kind = w->now[from + i];
        w->spare[i] = HOLDS_OF(kind) == HOLDS_PAYLOAD && BELOW_DEPTH(kind) <= i
                          ? kind
                          : resolved(w->v, w->now, from_region, from + i);
    }
    // Text whose length did not come along, and a length whose text did not,
    // are halves of nothing where they land.
    if (count > 0 && HOLDS_OF(w->spare[0]) == HOLDS_LENGTH) {
        w->spare[0] = alone(w->spare[0]);
    }
    if (count > 0 && HOLDS_OF(w->spare[count - 1]) == HOLDS_TEXT) {
        w->spare[count - 1] = alone(w->spare[count - 1]);
    }
    memcpy(w->now + to, w->spare, sizeof(Kind) * count);
}

// A value of a layout read out of something made `depth` blocks deep, which is
// that deep too.
static void push_laid_at(Kinds *w, uint32_t which, uint32_t depth) {
    uint32_t slots = laid_out(w->v, which, w->laid);
    for (uint32_t s = 0; s < slots; s++) {
        push(w, deeper_of(w->laid[s], depth));
    }
}

static void set_laid_at(Kinds *w, uint32_t which, uint32_t first,
                        uint32_t depth) {
    uint32_t slots = laid_out(w->v, which, w->laid);
    for (uint32_t s = 0; s < slots; s++) {
        SLOT(w, first + s) = deeper_of(w->laid[s], depth);
    }
}

// What the heap hands out here: made as deep as the blocks of working memory
// open around it.
static void push_made(Kinds *w, Kind kind) {
    push(w, deeper_of(kind, w->region));
}

// Whether a value of a layout can hold anything the heap hands out, which is
// what can keep something past the block that made it.
static bool can_keep(Kinds *w, uint32_t which) {
    uint32_t slots = laid_out(w->v, which, w->laid);
    for (uint32_t s = 0; s < slots; s++) {
        Holds is = HOLDS_OF(w->laid[s]);
        if (is == HOLDS_TEXT || is == HOLDS_ARRAY || is == HOLDS_STORE ||
            is == HOLDS_PAYLOAD || is == HOLDS_ANY) {
            return true;
        }
    }
    return false;
}

// Whether the `count` slots below the top `n` hold nothing made deeper than
// what they are written into, which outlives what was made inside it.
static const char *kept_in(Kinds *w, Kind container, uint32_t n,
                           uint32_t count) {
    if (HOLDS_OF(container) == HOLDS_ZERO) {
        return NULL;
    }
    for (uint32_t k = 0; k < count; k++) {
        Kind has = resolved(w->v, w->now, w->chunk->slot_count,
                            w->chunk->slot_count + w->depth - (n - k));
        if (made_on_the_heap(HOLDS_OF(has)) &&
            DEPTH_OF(has) > DEPTH_OF(container)) {
            snprintf(w->said, w->room,
                     "`%s` at %u keeps what a block of working memory %u deep "
                     "made in something made %u deep, which outlives it",
                     w->name, w->at, DEPTH_OF(has), DEPTH_OF(container));
            return "K0411";
        }
    }
    return NULL;
}

// Whether a call is handed something made in a block of working memory beside
// something older that could keep it: what the body called does with what it
// is handed is not this body's to know.
static const char *handed_to_keep(Kinds *w, uint32_t n) {
    uint32_t oldest_keeper = 0xFFu + 1u;
    uint32_t deepest = 0;
    for (uint32_t k = 1; k <= n; k++) {
        Kind has = resolved(w->v, w->now, w->chunk->slot_count,
                            w->chunk->slot_count + w->depth - k);
        Holds is = HOLDS_OF(has);
        if (!made_on_the_heap(is)) {
            continue;
        }
        if (DEPTH_OF(has) > deepest) {
            deepest = DEPTH_OF(has);
        }
        if ((is == HOLDS_ARRAY || is == HOLDS_STORE) &&
            (LAID_OF(has) == 0 || can_keep(w, LAID_OF(has) - 1)) &&
            DEPTH_OF(has) < oldest_keeper) {
            oldest_keeper = DEPTH_OF(has);
        }
    }
    if (deepest > oldest_keeper) {
        snprintf(w->said, w->room,
                 "`%s` at %u hands a call what a block of working memory %u "
                 "deep made beside something made %u deep that could keep it",
                 w->name, w->at, deepest, oldest_keeper);
        return "K0411";
    }
    return NULL;
}

// A layout whose values are bytes and nothing else, which is what text is
// read out of and written into as it is.
static bool just_bytes(Kinds *w, uint32_t which) {
    uint32_t slots = laid_out(w->v, which, w->laid);
    return w->module->layouts[which].size == 1 && slots == 1 &&
           HOLDS_OF(w->laid[0]) == HOLDS_NUMBER;
}

// Whether an element of the array a slot holds has a field of layout `field`
// at `offset` bytes.
static const char *element_field(Kinds *w, Kind array, uint32_t offset,
                                 uint32_t field) {
    uint32_t of = LAID_OF(array);
    if (HOLDS_OF(array) == HOLDS_ZERO) {
        return NULL;
    }
    if (of == 0 || !field_of(w->module, w->module->layout_types[of - 1],
                             offset, w->module->layout_types[field])) {
        snprintf(w->said, w->room,
                 "`%s` at %u reads layout %u at %u bytes into an element that "
                 "has no such field there",
                 w->name, w->at, field, offset);
        return "K0411";
    }
    return NULL;
}

// Whether `whole` holds, somewhere inside it, a run of `count` things each
// `step` bytes wide with a value of type `field` at `offset` bytes into the
// first of them: what an address stepped along that run reads.
static bool run_holds(const KestType *whole, uint32_t step, uint32_t count,
                      uint32_t offset, const KestType *field) {
    if (whole == NULL) {
        return false;
    }
    if (whole->tag == KEST_T_FIXED && whole->count == count &&
        whole->element != NULL && whole->element->byte_size == step &&
        offset < step && field_of(NULL, whole->element, offset, field)) {
        return true;
    }
    if (whole->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < whole->member_count; i++) {
            const KestMember *member = &whole->members[i];
            if (offset >= member->byte_offset &&
                run_holds(member->type, step, count,
                          offset - member->byte_offset, field)) {
                return true;
            }
        }
    }
    return false;
}

// Every address this body holds, once something may have moved what they
// point into: an array that grew, one a call was handed, a block of working
// memory given back.
static void forget_addresses(Kinds *w) {
    uint32_t live = w->chunk->slot_count + w->depth;
    for (uint32_t s = 0; s < live; s++) {
        if (HOLDS_OF(w->now[s]) == HOLDS_ADDRESS) {
            w->now[s] = HOLDS_JUNK;
        }
    }
}

// Whether any of the top `n` slots may be a way to an array: a call handed
// one can grow it.
static bool hands_over_a_handle(Kinds *w, uint32_t n) {
    for (uint32_t k = 1; k <= n; k++) {
        Holds is = HOLDS_OF(FROM_TOP(w, k));
        if (is == HOLDS_ARRAY || is == HOLDS_STORE || is == HOLDS_ANY ||
            is == HOLDS_ADDRESS) {
            return true;
        }
    }
    return false;
}

static Kind constant_holds(const KestModule *module, const KestChunk *chunk,
                           uint32_t which) {
    // What the walk before this one refuses, read as nothing rather than past
    // the end: the verifier is what the machine trusts, and it does not read
    // what it has not asked about either.
    if (which >= chunk->constant_count) {
        return HOLDS_ANY;
    }
    uint8_t class = chunk->constant_classes[which];
    if (class == KEST_CONST_TEXT) {
        return HOLDS_TEXT;
    }
    if (class == KEST_CONST_FN) {
        uint64_t function = (uint64_t)chunk->constants[which].integer;
        return function < module->count && function < EXACTLY
                   ? KIND(HOLDS_FUNCTION, EXACTLY | (uint32_t)function)
                   : KIND(HOLDS_FUNCTION, 0);
    }
    // A constant is kept once however many places use it, so the nought
    // after an empty piece of text is the nought every other use reads too.
    if (chunk->constants[which].integer == 0) {
        return HOLDS_ZERO;
    }
    if (class == KEST_CONST_INT && which > 0 &&
        chunk->constant_classes[which - 1] == KEST_CONST_TEXT) {
        return HOLDS_LENGTH;
    }
    // A small whole number keeps what it is, which is what says which case a
    // value being made is.
    int64_t value = chunk->constants[which].integer;
    if (class == KEST_CONST_INT && value > 0 && value < 0x7FFFFFFE) {
        return KIND(HOLDS_NUMBER, 0) | ((Kind)(value + 1) << 32);
    }
    return HOLDS_NUMBER;
}

// What slot `i` of a run of `count` constants from `first` holds: a piece of
// text beside its own length, which the constant after it is, is one piece.
static Kind constant_run_holds(const KestModule *module, const KestChunk *chunk,
                               uint32_t first, uint32_t count, uint32_t i) {
    Kind kind = constant_holds(module, chunk, first + i);
    uint32_t at = first + i;
    if (HOLDS_OF(kind) == HOLDS_TEXT && i + 1 < count &&
        at + 1 < chunk->constant_count &&
        chunk->constant_classes[at + 1] == KEST_CONST_INT) {
        return kind | ((Kind)PAIRED << 8);
    }
    if (i > 0 && at < chunk->constant_count &&
        chunk->constant_classes[at] == KEST_CONST_INT &&
        chunk->constant_classes[at - 1] == KEST_CONST_TEXT &&
        HOLDS_OF(kind) == HOLDS_LENGTH) {
        return kind | ((Kind)PAIRED << 8);
    }
    return kind;
}

// Simple instructions, as what they read from the top down and what they
// leave from the bottom up, one letter a slot: `n` a number, `t` text and its
// length (two slots), `h` an array, `s` a store, `a` an address.
static const char *simply(Kinds *w, const char *reads, const char *leaves) {
    uint32_t n = 1;
    for (const char *r = reads; *r != '\0'; r++) {
        const char *wrong = NULL;
        switch (*r) {
        case 'n':
            wrong = needs_top(w, n++, NEEDS_NUMBER);
            break;
        case 'h':
            wrong = needs_top(w, n++, NEEDS_ARRAY);
            break;
        case 's':
            wrong = needs_top(w, n++, NEEDS_STORE);
            break;
        case 't':
            wrong = needs_top(w, n++, NEEDS_LENGTH);
            if (wrong == NULL) {
                wrong = needs_top(w, n++, NEEDS_TEXT);
            }
            if (wrong == NULL) {
                uint32_t at = w->chunk->slot_count + w->depth - n + 1;
                if (!one_piece(resolved(w->v, w->now, w->chunk->slot_count, at),
                               resolved(w->v, w->now, w->chunk->slot_count,
                                        at + 1))) {
                    wrong = not_one_piece(w, at);
                }
            }
            break;
        default:
            break;
        }
        if (wrong != NULL) {
            return wrong;
        }
    }
    w->depth -= n - 1;
    for (const char *l = leaves; *l != '\0'; l++) {
        switch (*l) {
        case 'n':
            push(w, HOLDS_NUMBER);
            break;
        case 't':
            push_made(w, TEXT_KIND);
            push_made(w, LENGTH_KIND);
            break;
        default:
            break;
        }
    }
    return NULL;
}

// What one instruction does to what every slot holds, read off the machine's
// handler for it the way `kest_op_stack` reads what it does to the depth.
// Answers the refusal's code, with what was wrong in `said`, or NULL.
static const char *kinds_step(Kinds *w) {
    const KestChunk *chunk = w->chunk;
    const KestModule *module = w->module;
    Verifying *v = w->v;
    uint8_t op = chunk->code[w->at];
    uint32_t u[5] = {0, 0, 0, 0, 0};
    for (uint32_t k = 0; k < (kest_op_wide(op) - 1) / 2 && k < 5; k++) {
        u[k] = kest_chunk_u16(chunk, w->at + 1 + 2 * k);
    }
    const char *wrong = NULL;
    switch ((KestOp)op) {
    case KEST_OP_CONST:
        push(w, constant_holds(module, chunk, u[0]));
        return NULL;
    case KEST_OP_CONST_RUN:
        for (uint32_t i = 0; i < u[1]; i++) {
            push(w, constant_run_holds(module, chunk, u[0], u[1], i));
        }
        return NULL;
    case KEST_OP_CONST_AT: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        w->depth--;
        for (uint32_t k = 0; k < u[1]; k++) {
            Kind all = constant_run_holds(module, chunk, u[0], u[1], k);
            for (uint32_t row = 1; row < u[2]; row++) {
                all = joined(v, all,
                             constant_run_holds(module, chunk,
                                                u[0] + row * u[1], u[1], k));
            }
            push(w, all);
        }
        return NULL;
    }
    case KEST_OP_LOAD:
        if ((wrong = needs_slot(w, u[0], NEEDS_ANYTHING)) != NULL) {
            return wrong;
        }
        push(w, alone(resolved(v, w->now, 0, u[0])));
        return NULL;
    case KEST_OP_LOAD2:
        if ((wrong = needs_slot(w, u[0], NEEDS_ANYTHING)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_ANYTHING)) != NULL) {
            return wrong;
        }
        // Two slots side by side are what they were side by side.
        if (u[1] == u[0] + 1) {
            push(w, resolved(v, w->now, 0, u[0]));
            push(w, resolved(v, w->now, 0, u[1]));
        } else {
            push(w, alone(resolved(v, w->now, 0, u[0])));
            push(w, alone(resolved(v, w->now, 0, u[1])));
        }
        return NULL;
    case KEST_OP_LOADK:
        if ((wrong = needs_slot(w, u[0], NEEDS_ANYTHING)) != NULL) {
            return wrong;
        }
        push(w, alone(resolved(v, w->now, 0, u[0])));
        push(w, constant_holds(module, chunk, u[1]));
        return NULL;
    case KEST_OP_LOADN:
        for (uint32_t i = 0; i < u[1]; i++) {
            if ((wrong = needs_slot(w, u[0] + i, NEEDS_ANYTHING)) != NULL) {
                return wrong;
            }
        }
        carried_over(w, u[0], 0, chunk->slot_count + w->depth, u[1]);
        w->depth += u[1];
        return NULL;
    case KEST_OP_STORE:
        if ((wrong = needs_top(w, 1, NEEDS_ANYTHING)) != NULL) {
            return wrong;
        }
        SLOT(w, u[0]) = alone(resolved(v, w->now, chunk->slot_count,
                                       chunk->slot_count + w->depth - 1));
        w->depth--;
        return NULL;
    case KEST_OP_STOREN:
        for (uint32_t i = 0; i < u[1]; i++) {
            if ((wrong = needs_top(w, u[1] - i, NEEDS_ANYTHING)) != NULL) {
                return wrong;
            }
        }
        carried_over(w, chunk->slot_count + w->depth - u[1], chunk->slot_count,
                     u[0], u[1]);
        w->depth -= u[1];
        return NULL;
    case KEST_OP_STORE_K:
        SLOT(w, u[0]) = constant_holds(module, chunk, u[1]);
        return NULL;
    case KEST_OP_FIELD: {
        // `offset`, `size`, `total`: the `size` slots at `offset` of the
        // `total` on top are what is left.
        uint32_t base = chunk->slot_count + w->depth - u[2];
        carried_over(w, base + u[0], chunk->slot_count, base, u[1]);
        w->depth -= u[2] - u[1];
        return NULL;
    }
    case KEST_OP_ROTATE: {
        // The top one moved under the rest, so what a case carries is read
        // through its tag first, where the tag still is.
        uint32_t base = chunk->slot_count + w->depth - u[0];
        for (uint32_t i = 0; i < u[0]; i++) {
            w->spare[i] = resolved(v, w->now, chunk->slot_count, base + i);
        }
        memcpy(w->now + base, w->spare, sizeof(Kind) * u[0]);
        Kind tag = w->now[chunk->slot_count + w->depth - 1];
        for (uint32_t i = u[0] - 1; i > 0; i--) {
            w->now[base + i] = w->now[base + i - 1];
        }
        w->now[base] = tag;
        // Everything else moved up together and is beside what it was beside.
        // The one moved to the bottom is beside nothing it was; the one it
        // lands on left what was under it; the one it came from under lost
        // what was above.
        if (u[0] > 1) {
            w->now[base] = alone(w->now[base]);
            if (HOLDS_OF(w->now[base + 1]) == HOLDS_LENGTH) {
                w->now[base + 1] = alone(w->now[base + 1]);
            }
            uint32_t top = chunk->slot_count + w->depth - 1;
            if (HOLDS_OF(w->now[top]) == HOLDS_TEXT) {
                w->now[top] = alone(w->now[top]);
            }
        }
        return NULL;
    }
    case KEST_OP_POP:
        w->depth--;
        return NULL;
    case KEST_OP_POPN:
        w->depth -= u[0];
        return NULL;
    case KEST_OP_TRUE:
        push(w, KIND(HOLDS_NUMBER, 0) | ((Kind)2 << 32));
        return NULL;
    case KEST_OP_FALSE:
        push(w, HOLDS_ZERO);
        return NULL;

    case KEST_OP_ARRAY: {
        uint32_t slots = module->layouts[u[1]].slots;
        for (uint32_t e = 0; e < u[0]; e++) {
            if ((wrong = fits_top(w, u[1], (u[0] - e) * slots)) != NULL) {
                return wrong;
            }
        }
        w->depth -= u[0] * slots;
        push_made(w, KIND(HOLDS_ARRAY, u[1] + 1));
        return NULL;
    }
    case KEST_OP_MAKE_ARRAY: {
        // What fills none is never written, which is what `array()` is.
        uint32_t slots = module->layouts[u[0]].slots;
        if ((wrong = needs_top(w, slots + 1, NEEDS_NUMBER)) != NULL ||
            (HOLDS_OF(FROM_TOP(w, slots + 1)) != HOLDS_ZERO &&
             (wrong = fits_top(w, u[0], slots)) != NULL)) {
            return wrong;
        }
        w->depth -= slots + 1;
        push_made(w, KIND(HOLDS_ARRAY, u[0] + 1));
        return NULL;
    }
    case KEST_OP_PUSH:
    case KEST_OP_FIT: {
        uint32_t slots = module->layouts[u[0]].slots;
        if ((wrong = fits_top(w, u[0], slots)) != NULL ||
            (wrong = needs_top_of(w, slots + 1, NEEDS_ARRAY, u[0] + 1)) !=
                NULL ||
            (wrong = kept_in(w, FROM_TOP(w, slots + 1), slots, slots)) != NULL) {
            return wrong;
        }
        w->depth -= slots + 1;
        forget_addresses(w);
        if ((KestOp)op == KEST_OP_FIT) {
            push(w, HOLDS_NUMBER);
        }
        return NULL;
    }
    case KEST_OP_PUSH_TEXT:
    case KEST_OP_FIT_TEXT:
        // Text written into an array as its bytes, so an array of bytes.
        if (!just_bytes(w, u[0])) {
            snprintf(w->said, w->room,
                     "`%s` at %u writes text into an array of layout %u, "
                     "which is not bytes",
                     w->name, w->at, u[0]);
            return "K0411";
        }
        if ((wrong = needs_top(w, 1, NEEDS_LENGTH)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_TEXT)) != NULL ||
            (wrong = needs_top_of(w, 3, NEEDS_ARRAY, u[0] + 1)) != NULL) {
            return wrong;
        }
        w->depth -= 3;
        forget_addresses(w);
        if ((KestOp)op == KEST_OP_FIT_TEXT) {
            push(w, HOLDS_NUMBER);
        }
        return NULL;
    case KEST_OP_ROOM:
        // Room in an array of this layout, or in a store of anything.
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        if (!will_do(v, FROM_TOP(w, 2), NEEDS_STORE, 0) &&
            (wrong = needs_top_of(w, 2, NEEDS_ARRAY, u[0] + 1)) != NULL) {
            return wrong;
        }
        w->depth -= 2;
        forget_addresses(w);
        return NULL;
    case KEST_OP_INDEX:
    case KEST_OP_TAKE: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top_of(w, 2, NEEDS_ARRAY, u[0] + 1)) != NULL) {
            return wrong;
        }
        uint32_t made = DEPTH_OF(FROM_TOP(w, 2));
        w->depth -= 2;
        if ((KestOp)op == KEST_OP_TAKE) {
            forget_addresses(w);
        }
        push_laid_at(w, u[0], made);
        return NULL;
    }
    case KEST_OP_ELEM_AT: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_ARRAY)) != NULL ||
            (wrong = element_field(w, FROM_TOP(w, 2), u[0], u[1])) != NULL) {
            return wrong;
        }
        uint32_t made = DEPTH_OF(FROM_TOP(w, 2));
        w->depth -= 2;
        push_laid_at(w, u[1], made);
        return NULL;
    }
    case KEST_OP_INDEX_LL:
        if ((wrong = needs_slot_of(w, u[0], NEEDS_ARRAY, u[2] + 1)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        push_laid_at(w, u[2], DEPTH_OF(SLOT(w, u[0])));
        return NULL;
    case KEST_OP_INDEX_TO:
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top_of(w, 2, NEEDS_ARRAY, u[0] + 1)) != NULL) {
            return wrong;
        }
        {
            uint32_t made = DEPTH_OF(FROM_TOP(w, 2));
            w->depth -= 2;
            set_laid_at(w, u[0], u[1], made);
        }
        return NULL;
    case KEST_OP_INDEX_TO_LL:
        if ((wrong = needs_slot_of(w, u[2], NEEDS_ARRAY, u[0] + 1)) != NULL ||
            (wrong = needs_slot(w, u[3], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        set_laid_at(w, u[0], u[1], DEPTH_OF(SLOT(w, u[2])));
        return NULL;
    case KEST_OP_ELEM_FROM:
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_ARRAY)) != NULL ||
            (wrong = element_field(w, FROM_TOP(w, 2), u[0], u[1])) != NULL) {
            return wrong;
        }
        {
            uint32_t made = DEPTH_OF(FROM_TOP(w, 2));
            w->depth -= 2;
            set_laid_at(w, u[1], u[2], made);
        }
        return NULL;
    case KEST_OP_ELEM_FROM_LL:
        if ((wrong = needs_slot(w, u[3], NEEDS_ARRAY)) != NULL ||
            (wrong = needs_slot(w, u[4], NEEDS_NUMBER)) != NULL ||
            (wrong = element_field(w, SLOT(w, u[3]), u[0], u[1])) != NULL) {
            return wrong;
        }
        set_laid_at(w, u[1], u[2], DEPTH_OF(SLOT(w, u[3])));
        return NULL;
    case KEST_OP_POP_LAST: {
        if ((wrong = needs_top_of(w, 1, NEEDS_ARRAY, u[0] + 1)) != NULL) {
            return wrong;
        }
        uint32_t made = DEPTH_OF(FROM_TOP(w, 1));
        w->depth--;
        forget_addresses(w);
        push_laid_at(w, u[0], made);
        push(w, HOLDS_NUMBER);
        return NULL;
    }
    case KEST_OP_CLEAR:
        if ((wrong = simply(w, "h", "")) != NULL) {
            return wrong;
        }
        forget_addresses(w);
        return NULL;
    case KEST_OP_ELEM_ADDR: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_ARRAY)) != NULL) {
            return wrong;
        }
        uint32_t of = LAID_OF(FROM_TOP(w, 2));
        w->depth -= 2;
        push(w, KIND(HOLDS_ADDRESS, of));
        return NULL;
    }
    case KEST_OP_OFFSET_ADDR: {
        // One of `count` things each `stride` bytes wide inside what the
        // address was at: the address says so, and what is read at it has to
        // be inside such a run.
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_ADDRESS)) != NULL) {
            return wrong;
        }
        Kind at = FROM_TOP(w, 2);
        w->depth -= 2;
        push(w, STEPPED(at) != 0 || u[0] == 0 || u[1] == 0
                    ? KIND(HOLDS_ADDRESS, 0)
                    : KIND(HOLDS_ADDRESS, LAID_OF(at)) |
                          ((Kind)((u[0] << 16) | u[1]) << 32));
        return NULL;
    }
    case KEST_OP_LOAD_AT: {
        if ((wrong = needs_top(w, 1, NEEDS_ADDRESS)) != NULL) {
            return wrong;
        }
        uint32_t of = LAID_OF(FROM_TOP(w, 1));
        uint32_t stepped = STEPPED(FROM_TOP(w, 1));
        if (of == 0 ||
            !(stepped == 0
                  ? field_of(module, module->layout_types[of - 1], u[0],
                             module->layout_types[u[1]])
                  : run_holds(module->layout_types[of - 1], stepped >> 16,
                              stepped & 0xFFFFu, u[0],
                              module->layout_types[u[1]]))) {
            snprintf(w->said, w->room,
                     "`%s` at %u reads layout %u at %u bytes into what an "
                     "address is at, which has no such field there",
                     w->name, w->at, u[1], u[0]);
            return "K0411";
        }
        w->depth--;
        push_laid_at(w, u[1], w->region);
        return NULL;
    }
    case KEST_OP_LOAD_ELEM:
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_ARRAY)) != NULL ||
            (wrong = element_field(w, FROM_TOP(w, 2), u[0], u[1])) != NULL) {
            return wrong;
        }
        push_laid_at(w, u[1], DEPTH_OF(FROM_TOP(w, 2)));
        return NULL;
    case KEST_OP_STORE_ELEM: {
        uint32_t slots = module->layouts[u[1]].slots;
        if ((wrong = fits_top(w, u[1], slots)) != NULL ||
            (wrong = needs_top(w, slots + 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, slots + 2, NEEDS_ARRAY)) != NULL ||
            (wrong = element_field(w, FROM_TOP(w, slots + 2), u[0], u[1])) !=
                NULL ||
            (wrong = kept_in(w, FROM_TOP(w, slots + 2), slots, slots)) != NULL) {
            return wrong;
        }
        w->depth -= slots + 2;
        return NULL;
    }
    case KEST_OP_LOAD_SLOTS: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        w->depth--;
        for (uint32_t k = 0; k < u[1]; k++) {
            Kind all = resolved(v, w->now, 0, u[0] + k);
            for (uint32_t row = 1; row < u[2]; row++) {
                all = joined(v, all,
                             resolved(v, w->now, 0, u[0] + row * u[1] + k));
            }
            if (!will_do(v, all, NEEDS_ANYTHING, 0)) {
                return wrong_kind(w, "slot", u[0] + k, all, NEEDS_ANYTHING);
            }
            push(w, all);
        }
        return NULL;
    }
    case KEST_OP_STORE_SLOTS: {
        // A row a run chooses is any of them, so each is what it held or what
        // is written, whichever it turns out to be.
        for (uint32_t k = 0; k < u[1]; k++) {
            if ((wrong = needs_top(w, u[1] - k, NEEDS_ANYTHING)) != NULL) {
                return wrong;
            }
        }
        if ((wrong = needs_top(w, u[1] + 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        for (uint32_t row = 0; row < u[2]; row++) {
            for (uint32_t k = 0; k < u[1]; k++) {
                uint32_t s = u[0] + row * u[1] + k;
                SLOT(w, s) = joined(
                    v, resolved(v, w->now, 0, s),
                    resolved(v, w->now, chunk->slot_count,
                             chunk->slot_count + w->depth - (u[1] - k)));
            }
        }
        w->depth -= u[1] + 1;
        return NULL;
    }

    case KEST_OP_LEN:
        return simply(w, "h", "n");
    case KEST_OP_TEXT_LEN:
    case KEST_OP_HASH_T:
        return simply(w, "t", "n");
    case KEST_OP_TEXT_AT:
        return simply(w, "nt", "n");
    case KEST_OP_TEXT_IN:
        if ((wrong = needs_slot(w, u[0], NEEDS_TEXT)) != NULL ||
            (wrong = needs_slot(w, u[0] + 1, NEEDS_LENGTH)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        if (!one_piece(resolved(v, w->now, 0, u[0]),
                       resolved(v, w->now, 0, u[0] + 1))) {
            return not_one_piece(w, u[0]);
        }
        push(w, HOLDS_NUMBER);
        return NULL;
    case KEST_OP_TEXT_SLICE:
    case KEST_OP_TEXT_REST: {
        // A cut of text is where the text was: as deep as what it cut.
        bool slicing = (KestOp)op == KEST_OP_TEXT_SLICE;
        uint32_t from = chunk->slot_count + w->depth - (slicing ? 4u : 3u);
        uint32_t made = DEPTH_OF(resolved(v, w->now, chunk->slot_count, from));
        if ((wrong = simply(w, slicing ? "nnt" : "nt", "t")) != NULL) {
            return wrong;
        }
        FROM_TOP(w, 2) = deeper_of(TEXT_KIND, made);
        FROM_TOP(w, 1) = deeper_of(LENGTH_KIND, made);
        return NULL;
    }
    case KEST_OP_TEXT_MATCHES:
        return simply(w, "tnt", "n");
    case KEST_OP_TEXT_FIND:
        return simply(w, "ntt", "nn");
    case KEST_OP_TEXT_I:
    case KEST_OP_TEXT_U:
    case KEST_OP_TEXT_F:
    case KEST_OP_TEXT_F32:
    case KEST_OP_TEXT_B:
        return simply(w, "n", "t");
    case KEST_OP_TEXT_FLAGS:
    case KEST_OP_TEXT_VALUE: {
        uint32_t slots = module->layouts[u[0]].slots;
        if ((wrong = fits_top(w, u[0], slots)) != NULL) {
            return wrong;
        }
        w->depth -= slots;
        push_made(w, TEXT_KIND);
        push_made(w, LENGTH_KIND);
        return NULL;
    }
    case KEST_OP_CONCAT:
        for (uint32_t i = 0; i < u[0]; i++) {
            if ((wrong = needs_top(w, 2 * i + 1, NEEDS_LENGTH)) != NULL ||
                (wrong = needs_top(w, 2 * i + 2, NEEDS_TEXT)) != NULL) {
                return wrong;
            }
            uint32_t at = chunk->slot_count + w->depth - (2 * i + 2);
            if (!one_piece(resolved(v, w->now, chunk->slot_count, at),
                           resolved(v, w->now, chunk->slot_count, at + 1))) {
                return not_one_piece(w, at);
            }
        }
        w->depth -= 2 * u[0];
        push_made(w, TEXT_KIND);
        push_made(w, LENGTH_KIND);
        return NULL;
    case KEST_OP_HASH_VALUE: {
        uint32_t slots = module->layouts[u[0]].slots;
        if ((wrong = fits_top(w, u[0], slots)) != NULL) {
            return wrong;
        }
        w->depth -= slots;
        push(w, HOLDS_NUMBER);
        return NULL;
    }
    case KEST_OP_EQ_VALUE:
    case KEST_OP_NE_VALUE: {
        uint32_t slots = module->layouts[u[0]].slots;
        if ((wrong = fits_top(w, u[0], slots)) != NULL ||
            (wrong = fits_top(w, u[0], 2 * slots)) != NULL) {
            return wrong;
        }
        w->depth -= 2 * slots;
        push(w, HOLDS_NUMBER);
        return NULL;
    }
    case KEST_OP_TEXT_FROM: {
        // An array's bytes read as text, so an array of bytes.
        if ((wrong = needs_top(w, 1, NEEDS_ARRAY)) != NULL) {
            return wrong;
        }
        uint32_t of = LAID_OF(FROM_TOP(w, 1));
        if (HOLDS_OF(FROM_TOP(w, 1)) != HOLDS_ZERO &&
            (of == 0 || !just_bytes(w, of - 1))) {
            snprintf(w->said, w->room,
                     "`%s` at %u reads text out of an array that is not bytes",
                     w->name, w->at);
            return "K0411";
        }
        w->depth--;
        push_made(w, TEXT_KIND);
        push_made(w, LENGTH_KIND);
        return NULL;
    }

    case KEST_OP_NEW_STORE:
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        w->depth--;
        push_made(w, KIND(HOLDS_STORE, u[1] + 1));
        return NULL;
    case KEST_OP_ADD:
    case KEST_OP_SET: {
        // What goes into a store is a value of what the store holds, which
        // the handle says.
        bool adding = (KestOp)op == KEST_OP_ADD;
        uint32_t under = adding ? u[0] + 1 : u[0] + 2;
        Kind store = FROM_TOP(w, under);
        uint32_t of = LAID_OF(store);
        if ((wrong = needs_top(w, under, NEEDS_STORE)) != NULL) {
            return wrong;
        }
        if (HOLDS_OF(store) != HOLDS_ZERO &&
            (of == 0 || module->layouts[of - 1].slots != u[0])) {
            snprintf(w->said, w->room,
                     "`%s` at %u writes %u slot(s) into a store of what it "
                     "cannot say is that wide",
                     w->name, w->at, u[0]);
            return "K0411";
        }
        if ((of != 0 && (wrong = fits_top(w, of - 1, u[0])) != NULL) ||
            (wrong = kept_in(w, store, u[0], u[0])) != NULL) {
            return wrong;
        }
        if (!adding && (wrong = needs_top(w, u[0] + 1, NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        w->depth -= under;
        push(w, HOLDS_NUMBER);
        return NULL;
    }
    case KEST_OP_GET: {
        if ((wrong = needs_top(w, 1, NEEDS_NUMBER)) != NULL ||
            (wrong = needs_top(w, 2, NEEDS_STORE)) != NULL) {
            return wrong;
        }
        uint32_t of = LAID_OF(FROM_TOP(w, 2));
        uint32_t made = DEPTH_OF(FROM_TOP(w, 2));
        w->depth -= 2;
        if (of != 0 && module->layouts[of - 1].slots == u[0]) {
            push_laid_at(w, of - 1, made);
        } else {
            for (uint32_t k = 0; k < u[0]; k++) {
                push(w, HOLDS_ANY);
            }
        }
        push(w, HOLDS_NUMBER);
        return NULL;
    }
    case KEST_OP_REMOVE:
    case KEST_OP_STORE_REF:
        return simply(w, "ns", "n");
    case KEST_OP_COUNT:
        return simply(w, "s", "n");
    case KEST_OP_SEEK_FROM:
    case KEST_OP_SEEK_NEXT:
        if ((wrong = needs_slot(w, u[0], NEEDS_STORE)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        SLOT(w, u[1]) = HOLDS_NUMBER;
        return NULL;

    case KEST_OP_ADD_I:
    case KEST_OP_SUB_I:
    case KEST_OP_MUL_I:
    case KEST_OP_DIV_I:
    case KEST_OP_MOD_I:
    case KEST_OP_DIV_U:
    case KEST_OP_MOD_U:
    case KEST_OP_AND_I:
    case KEST_OP_OR_I:
    case KEST_OP_XOR_I:
    case KEST_OP_SHL:
    case KEST_OP_SHR_I:
    case KEST_OP_SHR_U:
    case KEST_OP_ADD_I_NARROW:
    case KEST_OP_SUB_I_NARROW:
    case KEST_OP_MUL_I_NARROW:
    case KEST_OP_ADD_F:
    case KEST_OP_SUB_F:
    case KEST_OP_MUL_F:
    case KEST_OP_DIV_F:
    case KEST_OP_MOD_F:
    case KEST_OP_ADD_F32:
    case KEST_OP_SUB_F32:
    case KEST_OP_MUL_F32:
    case KEST_OP_DIV_F32:
    case KEST_OP_MOD_F32:
    case KEST_OP_LT_I:
    case KEST_OP_LE_I:
    case KEST_OP_GT_I:
    case KEST_OP_GE_I:
    case KEST_OP_LT_U:
    case KEST_OP_LE_U:
    case KEST_OP_GT_U:
    case KEST_OP_GE_U:
    case KEST_OP_LT_F:
    case KEST_OP_LE_F:
    case KEST_OP_GT_F:
    case KEST_OP_GE_F:
    case KEST_OP_EQ_I:
    case KEST_OP_NE_I:
    case KEST_OP_EQ_F:
    case KEST_OP_NE_F:
        return simply(w, "nn", "n");
    case KEST_OP_NEG_I:
    case KEST_OP_NOT_I:
    case KEST_OP_NARROW:
    case KEST_OP_I2F:
    case KEST_OP_U2F:
    case KEST_OP_F2I:
    case KEST_OP_TO_F32:
    case KEST_OP_NEG_F:
    case KEST_OP_NEG_F32:
    case KEST_OP_NOT:
    case KEST_OP_HASH_I:
    case KEST_OP_HASH_F:
    case KEST_OP_FLOAT_BITS:
    case KEST_OP_BITS_F32:
    case KEST_OP_MOD_I_C:
    case KEST_OP_DIV_I_C:
    case KEST_OP_ADD_I_NARROW_C:
    case KEST_OP_SUB_I_NARROW_C:
    case KEST_OP_MUL_I_NARROW_C:
        return simply(w, "n", "n");
    case KEST_OP_EQ_T:
    case KEST_OP_NE_T:
    case KEST_OP_LT_T:
    case KEST_OP_LE_T:
    case KEST_OP_GT_T:
    case KEST_OP_GE_T:
        return simply(w, "tt", "n");
    case KEST_OP_MOD_I_K:
    case KEST_OP_DIV_I_K:
        if ((wrong = needs_slot(w, u[0], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        push(w, HOLDS_NUMBER);
        return NULL;
    case KEST_OP_ADD_I_NARROW_K:
    case KEST_OP_SUB_I_NARROW_K:
    case KEST_OP_MUL_I_NARROW_K:
        if ((wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        push(w, HOLDS_NUMBER);
        return NULL;
    case KEST_OP_ADD_I_NARROW_TO:
    case KEST_OP_SUB_I_NARROW_TO:
        if ((wrong = simply(w, "nn", "")) != NULL) {
            return wrong;
        }
        SLOT(w, u[1]) = HOLDS_NUMBER;
        return NULL;
    case KEST_OP_ADD_F_TO:
    case KEST_OP_SUB_F_TO:
        if ((wrong = simply(w, "nn", "")) != NULL) {
            return wrong;
        }
        SLOT(w, u[0]) = HOLDS_NUMBER;
        return NULL;
    case KEST_OP_ADD_F_LL:
    case KEST_OP_SUB_F_LL:
        if ((wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL ||
            (wrong = needs_slot(w, u[2], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        SLOT(w, u[0]) = HOLDS_NUMBER;
        return NULL;
    case KEST_OP_ADD_K_SELF:
    case KEST_OP_SUB_K_SELF:
        if ((wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        SLOT(w, u[1]) = HOLDS_NUMBER;
        return NULL;

    case KEST_OP_JUMP:
    case KEST_OP_LOOP:
    case KEST_OP_STOP:
        return NULL;
    case KEST_OP_JUMP_FALSE:
    case KEST_OP_JUMP_TRUE:
    case KEST_OP_JUMP_FALSE_LT_C:
    case KEST_OP_JUMP_FALSE_LE_C:
    case KEST_OP_JUMP_FALSE_GT_C:
    case KEST_OP_JUMP_FALSE_GE_C:
    case KEST_OP_JUMP_FALSE_EQ_C:
    case KEST_OP_JUMP_FALSE_NE_C:
        return simply(w, "n", "");
    case KEST_OP_JUMP_FALSE_LT_I:
    case KEST_OP_JUMP_FALSE_LE_I:
    case KEST_OP_JUMP_FALSE_GT_I:
    case KEST_OP_JUMP_FALSE_GE_I:
    case KEST_OP_JUMP_FALSE_EQ_I:
    case KEST_OP_JUMP_FALSE_NE_I:
    case KEST_OP_JUMP_TRUE_LT_I:
    case KEST_OP_JUMP_TRUE_LE_I:
    case KEST_OP_JUMP_TRUE_GT_I:
    case KEST_OP_JUMP_TRUE_GE_I:
    case KEST_OP_JUMP_TRUE_EQ_I:
    case KEST_OP_JUMP_TRUE_NE_I:
    case KEST_OP_JUMP_FALSE_LT_F:
    case KEST_OP_JUMP_FALSE_LE_F:
    case KEST_OP_JUMP_FALSE_GT_F:
    case KEST_OP_JUMP_FALSE_GE_F:
    case KEST_OP_JUMP_FALSE_EQ_F:
    case KEST_OP_JUMP_FALSE_NE_F:
    case KEST_OP_JUMP_TRUE_LT_F:
    case KEST_OP_JUMP_TRUE_LE_F:
    case KEST_OP_JUMP_TRUE_GT_F:
    case KEST_OP_JUMP_TRUE_GE_F:
    case KEST_OP_JUMP_TRUE_EQ_F:
    case KEST_OP_JUMP_TRUE_NE_F:
        return simply(w, "nn", "");
    case KEST_OP_JUMP_FALSE_LT_K:
    case KEST_OP_JUMP_FALSE_LE_K:
    case KEST_OP_JUMP_FALSE_GT_K:
    case KEST_OP_JUMP_FALSE_GE_K:
    case KEST_OP_JUMP_FALSE_EQ_K:
    case KEST_OP_JUMP_FALSE_NE_K:
    case KEST_OP_JUMP_FALSE_LT_FK:
    case KEST_OP_JUMP_FALSE_LE_FK:
    case KEST_OP_JUMP_FALSE_GT_FK:
    case KEST_OP_JUMP_FALSE_GE_FK:
    case KEST_OP_JUMP_FALSE_EQ_FK:
    case KEST_OP_JUMP_FALSE_NE_FK:
    case KEST_OP_JUMP_TRUE_LT_FK:
    case KEST_OP_JUMP_TRUE_LE_FK:
    case KEST_OP_JUMP_TRUE_GT_FK:
    case KEST_OP_JUMP_TRUE_GE_FK:
    case KEST_OP_JUMP_TRUE_EQ_FK:
    case KEST_OP_JUMP_TRUE_NE_FK:
        return needs_slot(w, u[0], NEEDS_NUMBER);
    case KEST_OP_JUMP_FALSE_LT_E:
    case KEST_OP_JUMP_FALSE_LE_E:
    case KEST_OP_JUMP_FALSE_GT_E:
    case KEST_OP_JUMP_FALSE_GE_E:
    case KEST_OP_JUMP_FALSE_EQ_E:
    case KEST_OP_JUMP_FALSE_NE_E: {
        // An element read as the number it is weighed as: a layout that is
        // anything else is its bits read as a number.
        if ((wrong = needs_slot_of(w, u[0], NEEDS_ARRAY, u[2] + 1)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        uint32_t slots = laid_out(v, u[2], w->laid);
        if (slots != 1 || HOLDS_OF(w->laid[0]) != HOLDS_NUMBER) {
            snprintf(w->said, w->room,
                     "`%s` at %u weighs an element of a layout that is not "
                     "a number",
                     w->name, w->at);
            return "K0411";
        }
        return NULL;
    }
    case KEST_OP_NEXT_LESS_I:
    case KEST_OP_NEXT_LESS_U:
        if ((wrong = needs_slot(w, u[0], NEEDS_NUMBER)) != NULL ||
            (wrong = needs_slot(w, u[1], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        SLOT(w, u[0]) = HOLDS_NUMBER;
        return NULL;
    case KEST_OP_SCRATCH:
        if (w->region == 0xFFu) {
            snprintf(w->said, w->room,
                     "`%s` at %u opens more blocks of working memory than "
                     "this can count",
                     w->name, w->at);
            return "K0411";
        }
        SLOT(w, u[0]) = HOLDS_NUMBER;
        w->region++;
        return NULL;
    case KEST_OP_UNSCRATCH: {
        // What the block made is given back: every slot holding any of it,
        // or holding what cannot be told apart from it, holds nothing now.
        if ((wrong = needs_slot(w, u[0], NEEDS_NUMBER)) != NULL) {
            return wrong;
        }
        if (w->region == 0) {
            snprintf(w->said, w->room,
                     "`%s` at %u closes a block of working memory nothing "
                     "opened",
                     w->name, w->at);
            return "K0411";
        }
        forget_addresses(w);
        uint32_t live = chunk->slot_count + w->depth;
        for (uint32_t s = 0; s < live; s++) {
            Holds is = HOLDS_OF(w->now[s]);
            if (is == HOLDS_ANY ||
                (made_on_the_heap(is) && DEPTH_OF(w->now[s]) >= w->region)) {
                w->now[s] = HOLDS_JUNK;
            }
        }
        w->region--;
        return NULL;
    }

    case KEST_OP_CALL: {
        const KestChunk *callee = module->functions[u[0]];
        uint32_t n = u[1];
        for (uint32_t p = 0; p < callee->takes_count; p++) {
            uint32_t slots = module->layouts[callee->takes[p]].slots;
            if ((wrong = fits_top(w, callee->takes[p], n)) != NULL) {
                return wrong;
            }
            n -= slots;
        }
        if ((wrong = handed_to_keep(w, u[1])) != NULL) {
            return wrong;
        }
        bool handed = hands_over_a_handle(w, u[1]);
        w->depth -= u[1];
        if (handed) {
            forget_addresses(w);
        }
        if (callee->result_slots > 0) {
            push_laid_at(w, callee->gives, w->region);
        }
        return NULL;
    }
    case KEST_OP_CALL_VALUE: {
        // Which function is a number a run knows, and the type it is called
        // as is what every function that number can be was checked against
        // where it became a value: what is handed over and what comes back
        // are that type's.
        if ((wrong = needs_top(w, 1, NEEDS_FUNCTION)) != NULL) {
            return wrong;
        }
        uint32_t of = LAID_OF(FROM_TOP(w, 1));
        if (of & EXACTLY) {
            const KestChunk *callee = module->functions[of & ~EXACTLY];
            uint32_t n = u[0] + 1;
            if (u[0] != callee->param_slots || u[1] != callee->result_slots) {
                snprintf(w->said, w->room,
                         "`%s` at %u calls a function taking %u and giving %u "
                         "as one taking %u and giving %u",
                         w->name, w->at, callee->param_slots,
                         callee->result_slots, u[0], u[1]);
                return "K0411";
            }
            for (uint32_t p = 0; p < callee->takes_count; p++) {
                if ((wrong = fits_top(w, callee->takes[p], n)) != NULL) {
                    return wrong;
                }
                n -= module->layouts[callee->takes[p]].slots;
            }
            if ((wrong = handed_to_keep(w, u[0] + 1)) != NULL) {
                return wrong;
            }
            bool handed = hands_over_a_handle(w, u[0] + 1);
            w->depth -= u[0] + 1;
            if (handed) {
                forget_addresses(w);
            }
            if (callee->result_slots > 0) {
                push_laid_at(w, callee->gives, w->region);
            }
            return NULL;
        }
        const KestType *type = function_type(v, of);
        uint32_t n = u[0] + 1;
        if (type == NULL || type->tag != KEST_T_FN) {
            snprintf(w->said, w->room,
                     "`%s` at %u calls a function of a type it cannot say",
                     w->name, w->at);
            return "K0411";
        }
        // What a function type takes and gives is written into room as wide
        // as the widest layout, and a type nothing laid out may be wider.
        for (uint32_t p = 0; p <= type->param_count; p++) {
            const KestType *one =
                p < type->param_count ? type->params[p] : type->result;
            if (one != NULL && one->slots > w->laid_room) {
                snprintf(w->said, w->room,
                         "`%s` at %u calls a function of a type wider than "
                         "anything this program laid out",
                         w->name, w->at);
                return "K0411";
            }
        }
        for (uint32_t p = 0; p < type->param_count; p++) {
            uint32_t at = 0;
            typed(v, type->params[p], w->laid, &at, false);
            for (uint32_t s = 0; s < at; s++) {
                uint32_t inside = 0;
                Needs wants = wanted_by(w->laid[s], &inside);
                if ((wrong = needs_top_of(w, n - s, wants, inside)) != NULL) {
                    return wrong;
                }
            }
            n -= at;
        }
        if (n != 1) {
            snprintf(w->said, w->room,
                     "`%s` at %u hands over %u slot(s) to a function that "
                     "takes %u",
                     w->name, w->at, u[0], u[0] + 1 - n);
            return "K0411";
        }
        if ((wrong = handed_to_keep(w, u[0] + 1)) != NULL) {
            return wrong;
        }
        bool handed = hands_over_a_handle(w, u[0] + 1);
        w->depth -= u[0] + 1;
        if (handed) {
            forget_addresses(w);
        }
        if (u[1] > 0) {
            uint32_t at = 0;
            typed(v, type->result, w->laid, &at, false);
            if (at != u[1]) {
                snprintf(w->said, w->room,
                         "`%s` at %u takes back %u slot(s) from a function "
                         "that gives %u",
                         w->name, w->at, u[1], at);
                return "K0411";
            }
            for (uint32_t s = 0; s < at; s++) {
                push(w, deeper_of(w->laid[s], w->region));
            }
        }
        return NULL;
    }
    case KEST_OP_CALL_HOST: {
        const KestExtern *door = &module->externs[u[0]];
        uint32_t n = u[1];
        for (uint32_t p = 0; p < door->takes_count; p++) {
            uint32_t slots = module->layouts[door->takes[p]].slots;
            if ((wrong = fits_top(w, door->takes[p], n)) != NULL) {
                return wrong;
            }
            n -= slots;
        }
        bool handed = hands_over_a_handle(w, u[1]);
        w->depth -= u[1];
        if (handed) {
            forget_addresses(w);
        }
        if (u[2] > 0 && door->gives_value) {
            push_laid_at(w, door->gives, w->region);
        } else {
            for (uint32_t k = 0; k < u[2]; k++) {
                push(w, HOLDS_ANY);
            }
        }
        return NULL;
    }
    case KEST_OP_RETURN:
        if (w->region != 0) {
            snprintf(w->said, w->room,
                     "`%s` at %u leaves with %u block(s) of working memory "
                     "open",
                     w->name, w->at, w->region);
            return "K0411";
        }
        if (u[0] > 0) {
            return fits_top(w, chunk->gives, u[0]);
        }
        return NULL;
    }
    return NULL;
}

// Folds what one way into a place holds into what the others did, as far up
// as the stack is there. Answers whether anything changed.
static bool fold_into(Verifying *v, Kind **kept, uint32_t place,
                      const Kind *now, uint32_t live, uint32_t wide,
                      uint32_t frame, KestArena *scratch, bool *starved) {
    if (kept[place] == NULL) {
        kept[place] = KEST_ARENA_ARRAY(scratch, Kind, wide);
        if (kept[place] == NULL) {
            *starved = true;
            return false;
        }
        memcpy(kept[place], now, sizeof(Kind) * live);
        return true;
    }
    bool changed = false;
    for (uint32_t s = 0; s < live; s++) {
        // What a case carries, met by something else, is what its own tag
        // says it is on the way it came.
        uint32_t region = s < frame ? 0 : frame;
        Kind was = kept[place][s];
        Kind is = now[s];
        if (was != is && HOLDS_OF(was) == HOLDS_PAYLOAD) {
            was = resolved(v, kept[place], region, s);
        }
        if (was != is && HOLDS_OF(is) == HOLDS_PAYLOAD) {
            is = resolved(v, now, region, s);
        }
        Kind both = joined(v, was, is);
        if (both != kept[place][s]) {
            kept[place][s] = both;
            changed = true;
        }
    }
    return changed;
}

// Which slot of the frame a straight run of instructions just before `at`
// left on top of the stack, if it was loaded there by the instruction before
// and nothing can arrive between the two; UINT32_MAX otherwise.
static uint32_t loaded_from(const KestChunk *chunk, const uint8_t *lands,
                            uint32_t before, uint32_t at) {
    if (before == UINT32_MAX || lands[at] ||
        before + kest_op_wide(chunk->code[before]) != at) {
        return UINT32_MAX;
    }
    uint8_t op = chunk->code[before];
    if (op == KEST_OP_LOAD) {
        return kest_chunk_u16(chunk, before + 1);
    }
    if (op == KEST_OP_LOAD2) {
        return kest_chunk_u16(chunk, before + 3);
    }
    return UINT32_MAX;
}

// Whether the branch at `at` weighs a slot of the frame against a whole
// number written in the code: which slot, which number, and whether the jump
// is taken when the two are equal. Read off the instruction itself where one
// instruction does all of it, and off the straight run before it where
// fusing was not done -- `load`, `const`, `eq.i` and `jump.false`, or the
// load that `jump.false.eq.c` weighs -- where nothing can arrive in between.
static bool weighs_a_slot(const KestChunk *chunk, const uint8_t *lands,
                          const uint32_t *before, uint32_t at, uint32_t *slot,
                          int64_t *value, bool *jumps_on_equal) {
    uint8_t op = chunk->code[at];
    if (op == KEST_OP_JUMP_FALSE_EQ_K || op == KEST_OP_JUMP_FALSE_NE_K) {
        uint32_t which = kest_chunk_u16(chunk, at + 3);
        if (chunk->constant_classes[which] != KEST_CONST_INT) {
            return false;
        }
        *slot = kest_chunk_u16(chunk, at + 1);
        *value = chunk->constants[which].integer;
        *jumps_on_equal = op == KEST_OP_JUMP_FALSE_NE_K;
        return true;
    }
    if (op == KEST_OP_JUMP_FALSE_EQ_C || op == KEST_OP_JUMP_FALSE_NE_C) {
        uint32_t which = kest_chunk_u16(chunk, at + 1);
        uint32_t from = loaded_from(chunk, lands, before[0], at);
        if (from == UINT32_MAX ||
            chunk->constant_classes[which] != KEST_CONST_INT) {
            return false;
        }
        *slot = from;
        *value = chunk->constants[which].integer;
        *jumps_on_equal = op == KEST_OP_JUMP_FALSE_NE_C;
        return true;
    }
    if (op != KEST_OP_JUMP_FALSE && op != KEST_OP_JUMP_TRUE) {
        return false;
    }
    // `a`, `b`, the comparison, the jump: one of `a` and `b` a slot and the
    // other a constant, in either order.
    uint32_t weigh = before[0];
    uint32_t second = before[1];
    uint32_t first = before[2];
    if (weigh == UINT32_MAX || second == UINT32_MAX || first == UINT32_MAX ||
        lands[at] || lands[weigh] || lands[second] ||
        weigh + kest_op_wide(chunk->code[weigh]) != at ||
        second + kest_op_wide(chunk->code[second]) != weigh ||
        first + kest_op_wide(chunk->code[first]) != second) {
        return false;
    }
    uint8_t compare = chunk->code[weigh];
    if (compare != KEST_OP_EQ_I && compare != KEST_OP_NE_I) {
        return false;
    }
    uint32_t loaded = UINT32_MAX;
    uint32_t constant = UINT32_MAX;
    uint8_t one = chunk->code[first];
    uint8_t two = chunk->code[second];
    if (one == KEST_OP_LOAD && two == KEST_OP_CONST) {
        loaded = kest_chunk_u16(chunk, first + 1);
        constant = kest_chunk_u16(chunk, second + 1);
    } else if (one == KEST_OP_CONST && two == KEST_OP_LOAD) {
        constant = kest_chunk_u16(chunk, first + 1);
        loaded = kest_chunk_u16(chunk, second + 1);
    } else if (two == KEST_OP_LOADK) {
        loaded = kest_chunk_u16(chunk, second + 1);
        constant = kest_chunk_u16(chunk, second + 3);
    }
    if (loaded == UINT32_MAX ||
        chunk->constant_classes[constant] != KEST_CONST_INT) {
        return false;
    }
    *slot = loaded;
    *value = chunk->constants[constant].integer;
    // `jump.false` after `eq.i` is taken when they differ; every other pair
    // turns it round once.
    bool equal_is_true = compare == KEST_OP_EQ_I;
    *jumps_on_equal = (op == KEST_OP_JUMP_TRUE) == equal_is_true;
    return true;
}

// Whether a way arriving at `place` has as many blocks of working memory open
// as every way before it did.
static const char *same_blocks(uint8_t *opened, uint32_t place,
                               uint32_t region, Kinds *w) {
    if (opened[place] == 0xFF) {
        opened[place] = (uint8_t)region;
        return NULL;
    }
    if (opened[place] == region) {
        return NULL;
    }
    snprintf(w->said, w->room,
             "`%s` at %u arrives at %u with %u block(s) of working memory "
             "open where another way arrives with %u",
             w->name, w->at, place, region, opened[place]);
    return "K0411";
}

// What every slot holds at every instruction a body can reach, walked from
// the first with what the declaration says the arguments are and nothing in
// the rest, and folded where two ways meet. Every instruction's reading is
// held to what it reads; `depth` is where the stack stands at each, which the
// walk before this one proved. See D1242.
static const char *holds_on_every_path(Verifying *v, const KestChunk *chunk,
                                       const uint16_t *depth, Kind *laid,
                                       uint32_t laid_room,
                                       KestArena *scratch, char *said,
                                       size_t room) {
    const KestModule *module = v->module;
    uint32_t wide = (uint32_t)chunk->slot_count + chunk->stack_needed + 1;
    uint32_t count = chunk->code_count;
    KestMark mark = kest_arena_mark(scratch);
    Kind **kept = KEST_ARENA_ARRAY(scratch, Kind *, count + 1);
    uint8_t *lands = kest_arena_alloc(scratch, count + 1, 1);
    uint8_t *queued = kest_arena_alloc(scratch, count + 1, 1);
    uint32_t *work = KEST_ARENA_ARRAY(scratch, uint32_t, count + 1);
    Kind *now = KEST_ARENA_ARRAY(scratch, Kind, wide);
    Kind *spare = KEST_ARENA_ARRAY(scratch, Kind, wide);
    // How many blocks of working memory are open at each place two ways meet,
    // which is one number or one way leaves a block the other never opened.
    uint8_t *opened = kest_arena_alloc(scratch, count + 1, 1);
    if (kept == NULL || lands == NULL || queued == NULL || work == NULL ||
        now == NULL || spare == NULL || opened == NULL) {
        kest_arena_rewind(scratch, mark);
        snprintf(said, room, WANTED_ROOM);
        return "K0411";
    }
    lands[0] = 1;
    for (uint32_t at = 0; at < count; at += kest_op_wide(chunk->code[at])) {
        uint8_t op = chunk->code[at];
        uint32_t size = kest_op_wide(op);
        for (uint32_t k = 0; k < (size - 1) / 2; k++) {
            uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
            KestOperand is = kest_op_operand(op, k);
            if (is == KEST_OPERAND_FORWARD) {
                lands[at + size + value] = 1;
            } else if (is == KEST_OPERAND_BACKWARD) {
                lands[at + size - value] = 1;
            }
        }
    }
    // The arguments are what the declaration says; every other slot is what
    // the last frame to stand here left.
    for (uint32_t s = 0; s < wide; s++) {
        now[s] = HOLDS_JUNK;
    }
    uint32_t slot = 0;
    for (uint32_t p = 0; p < chunk->takes_count; p++) {
        slot += laid_out(v, chunk->takes[p], now + slot);
    }
    bool starved = false;
    fold_into(v, kept, 0, now, chunk->slot_count, wide, chunk->slot_count,
              scratch, &starved);
    memset(opened, 0xFF, count + 1);
    opened[0] = 0;
    uint32_t waiting = 0;
    work[waiting++] = 0;
    queued[0] = 1;
    const char *wrong = NULL;
    Kinds w = {module, v, chunk, now, 0, laid, laid_room, spare, 0, 0, NULL,
               said, room};
    while (waiting > 0 && wrong == NULL && !starved) {
        uint32_t at = work[--waiting];
        queued[at] = 0;
        memcpy(now, kept[at], sizeof(Kind) * ((size_t)chunk->slot_count + depth[at]));
        w.region = opened[at];
        // Out of work is a build that stops, which it has already been told:
        // what is left of this body is not walked.
        if (!kest_diags_work(v->diags, 1)) {
            break;
        }
        // The instructions this run walked just before this one, newest
        // first, for reading a weighing that was not fused into one.
        uint32_t before[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
        while (wrong == NULL) {
            uint8_t op = chunk->code[at];
            uint32_t size = kest_op_wide(op);
            w.at = at;
            w.depth = depth[at];
            w.name = kest_op_name(op);
            if (!kest_diags_work(v->diags, 1)) {
                break;
            }
            wrong = kinds_step(&w);
            if (wrong != NULL) {
                break;
            }
            // What this walk left and what the walk of the depth says it
            // leaves are one number, or one of the two tables is wrong.
            uint32_t takes = 0;
            uint32_t gives = 0;
            if (op != KEST_OP_RETURN && op != KEST_OP_STOP &&
                kest_op_stack(module, chunk, at, &takes, &gives) == NULL &&
                w.depth != depth[at] - takes + gives) {
                snprintf(said, room,
                         "`%s` at %u leaves %u slot(s) by what it reads and "
                         "%u by what it moves",
                         w.name, at, w.depth, depth[at] - takes + gives);
                wrong = "K0411";
                break;
            }
            if (op == KEST_OP_RETURN || op == KEST_OP_STOP) {
                break;
            }
            // A tag weighed against a case is that case on the way it was
            // and every other on the way it was not.
            uint32_t tag_slot = UINT32_MAX;
            uint32_t equal = 0;
            uint32_t other = 0;
            uint32_t weighed = 0;
            int64_t against = 0;
            bool leaves_on_equal = false;
            if (weighs_a_slot(chunk, lands, before, at, &weighed, &against,
                              &leaves_on_equal) &&
                weighed < chunk->slot_count) {
                Kind tag = resolved(v, now, 0, weighed);
                if (HOLDS_OF(tag) == HOLDS_TAG && against >= 0 && against < 24) {
                    tag_slot = weighed;
                    equal = BELOW_DEPTH(tag) & (1u << against);
                    other = BELOW_DEPTH(tag) & ~(1u << against);
                }
            }
            Kind weighed_tag = HOLDS_JUNK;
            if (tag_slot != UINT32_MAX) {
                weighed_tag = resolved(v, now, 0, tag_slot) &
                              ~((Kind)ALL_CASES << 32);
                now[tag_slot] =
                    weighed_tag | ((Kind)(leaves_on_equal ? equal : other) << 32);
            }
            for (uint32_t k = 0; k < (size - 1) / 2; k++) {
                uint32_t value = kest_chunk_u16(chunk, at + 1 + 2 * k);
                KestOperand is = kest_op_operand(op, k);
                uint32_t place = is == KEST_OPERAND_FORWARD    ? at + size + value
                                 : is == KEST_OPERAND_BACKWARD ? at + size - value
                                                               : UINT32_MAX;
                if (place != UINT32_MAX &&
                    (wrong = same_blocks(opened, place, w.region, &w)) != NULL) {
                    break;
                }
                if (place != UINT32_MAX &&
                    fold_into(v, kept, place, now,
                              (uint32_t)chunk->slot_count + depth[place], wide,
                              chunk->slot_count, scratch, &starved) &&
                    !queued[place]) {
                    queued[place] = 1;
                    work[waiting++] = place;
                }
            }
            if (wrong != NULL || op == KEST_OP_JUMP || op == KEST_OP_LOOP) {
                break;
            }
            if (tag_slot != UINT32_MAX) {
                now[tag_slot] =
                    weighed_tag | ((Kind)(leaves_on_equal ? other : equal) << 32);
            }
            uint32_t next = at + size;
            if (lands[next]) {
                if ((wrong = same_blocks(opened, next, w.region, &w)) != NULL) {
                    break;
                }
                if (fold_into(v, kept, next, now,
                              (uint32_t)chunk->slot_count + depth[next], wide,
                              chunk->slot_count, scratch, &starved) &&
                    !queued[next]) {
                    queued[next] = 1;
                    work[waiting++] = next;
                }
                break;
            }
            before[2] = before[1];
            before[1] = before[0];
            before[0] = at;
            at = next;
        }
    }
    if (wrong == NULL && starved) {
        snprintf(said, room, WANTED_ROOM);
        wrong = "K0411";
    }
    kest_arena_rewind(scratch, mark);
    return wrong;
}

bool kest_module_prove(const KestModule *module, KestArena *arena,
                       KestDiags *diags) {
    if (module->count == 0) {
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    if (state == NULL) {
        return false;
    }

    // What the walks below keep about one body -- where each instruction
    // starts, the depth of the stack at each, and the places still to walk
    // from -- sized for the largest body and in an arena of the verifier's
    // own, given back at the end. Taken under the build's, so it is inside
    // what the host gave the build, and what it held at its widest is part of
    // what the build cost: `examples/embed.c` builds a program inside what it
    // cost a moment before, and was refused 4,100 bytes short when proving
    // took from the build's own. See D1237 and D1247.
    uint32_t largest = 1;
    for (uint32_t i = 0; i < module->count; i++) {
        if (module->functions[i]->code_count > largest) {
            largest = module->functions[i]->code_count;
        }
    }
    KestArena *scratch = kest_arena_new_under(arena);
    uint8_t *starts = NULL;
    uint16_t *depth = NULL;
    uint32_t *work = NULL;
    if (scratch != NULL) {
        starts = kest_arena_alloc(scratch, ((size_t)largest + 8) / 8, 1);
        depth = KEST_ARENA_ARRAY(scratch, uint16_t, largest);
        work = KEST_ARENA_ARRAY(scratch, uint32_t, largest);
        if (depth == NULL || work == NULL) {
            starts = NULL;
        }
    }
    uint32_t widest = 1;
    for (uint32_t i = 0; i < module->layout_count; i++) {
        if (module->layouts[i].slots > widest) {
            widest = module->layouts[i].slots;
        }
    }
    Kind *laid = scratch == NULL ? NULL : KEST_ARENA_ARRAY(scratch, Kind, widest);
    Verifying verifying = {module, NULL, 0, 0, diags};
    if (scratch != NULL) {
        verifying.enums = KEST_ARENA_ARRAY(scratch, const KestType *, 256);
        verifying.enum_room = verifying.enums == NULL ? 0 : 256;
    }

    bool held = true;

    // Every chunk has to be walkable, which means that stepping by what each
    // instruction says it takes lands exactly on the end. A width that is
    // wrong for one instruction puts everything after it out of step, and a
    // walk that reads the middle of an instruction as an instruction is how
    // half the calls in a program went unseen once (D057).
    uint32_t known = (uint32_t)KEST_OP_STOP + 1;
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        // The walks below that go over a body once, counted once a body.
        kest_diags_work(diags, chunk->code_count);
        uint32_t at = 0;
        uint8_t last = KEST_OP_RETURN;
        const char *wrong = NULL;
        // A `return` may give back less than the function says, because the
        // one written past the end of a body gives nothing and is there for a
        // body that falls off it. More is what a host would read out of its
        // frame past the end, so it is the direction that is held.
        int32_t gives = -1;
        while (at < chunk->code_count) {
            last = chunk->code[at];
            if (last >= known) {
                wrong = "lands on something that is not an instruction";
                break;
            }
            if (last == KEST_OP_RETURN && gives < 0) {
                uint16_t count = kest_chunk_u16(chunk, at + 1);
                if (count > chunk->result_slots) {
                    gives = count;
                }
            }
            at += kest_op_wide(last);
        }
        if (wrong == NULL && at != chunk->code_count) {
            wrong = "steps past the end";
        }
        // Every chunk ends in a return, so a walk that ends anywhere else
        // stepped through the middle of something. Landing on the end by luck
        // is possible; landing on the end having last seen a return is not.
        if (wrong == NULL && last != KEST_OP_RETURN) {
            wrong = "ends on something that is not a return";
        }
        if (wrong != NULL) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0406", nowhere,
                           "`%s` cannot be walked: %u bytes of code and a walk "
                           "that %s",
                           chunk->name, chunk->code_count, wrong);
            kest_diags_fault(diags,
                             "an instruction is a different width from what "
                             "it says");
            held = false;
        }
        // Every number an instruction carries names something that is there,
        // and every jump lands on an instruction. See D1237.
        if (wrong == NULL) {
            char said[200];
            const char *code = NULL;
            if (starts == NULL) {
                snprintf(said, sizeof said,
                         WANTED_ROOM);
                code = "K0408";
            }
            if (code == NULL) {
                code = names_only_what_is_there(module, chunk, starts, said,
                                                sizeof said);
            }
            if (code == NULL) {
                code = stack_on_every_path(module, chunk, depth, work, said,
                                           sizeof said);
            }
            if (code == NULL) {
                code = laid == NULL
                           ? "K0411"
                           : holds_on_every_path(&verifying, chunk, depth,
                                                 laid, widest, scratch, said,
                                                 sizeof said);
                if (code != NULL && laid == NULL) {
                    snprintf(said, sizeof said,
                             WANTED_ROOM);
                }
            }
            // A walk that ran out of room found nothing wrong: it is said
            // the way a build that runs out of room anywhere else is said,
            // rather than as the compiler's fault in what it wrote. The
            // chunk is not held all the same, since nothing proved it.
            if (code != NULL && strcmp(said, WANTED_ROOM) == 0) {
                kest_diags_starve(diags);
                held = false;
                code = NULL;
            }
            if (code != NULL) {
                KestSpan nowhere = {0, 0};
                kest_diags_in(diags, chunk->source);
                kest_diags_add(diags, KEST_SEVERITY_ERROR, code, nowhere,
                               "`%s`: %s", chunk->name, said);
                kest_diags_fault(diags,
                                 "what the compiler wrote into an instruction "
                                 "and what the program holds disagree");
                held = false;
            }
        }
        // How wide a frame has to be is answered from the declaration before
        // anything runs, so a `return` wider than that would be read back into
        // a host's frame past the end of it.
        if (wrong == NULL && gives >= 0) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0407", nowhere,
                           "`%s` has a `return` giving %d slots back where its "
                           "declaration gives %u",
                           chunk->name, gives, chunk->result_slots);
            kest_diags_fault(diags,
                             "what a call reads back is the declaration's "
                             "width");
            held = false;
        }
    }

    for (uint32_t i = 0; i < module->count; i++) {
        for (int about = 0; about < 3; about++) {
            if (!(about == 2   ? module->functions[i]->deterministic
                  : about == 1 ? module->functions[i]->no_host
                               : module->functions[i]->no_alloc)) {
                continue;
            }
            memset(state, 0, module->count);
            uint32_t where = 0;
            int32_t at = breaks_in(module, i, state, &where, about);
            if (at < 0) {
                continue;
            }
            // Reaching here means the walk over the tree missed something, so
            // it is reported against the instruction rather than against a
            // promise: the promise was checked and this is the code that was
            // emitted for it.
            const KestChunk *guilty = module->functions[at];
            KestSpan span = {kest_chunk_origin(guilty, where), 1};
            const char *written = module->functions[i]->wrote;

            kest_diags_in(diags, guilty->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0405", span,
                           about == 2
                               ? "this reaches outside the simulation "
                                 "profile, and `%s` promises `deterministic`"
                           : about == 1
                               ? "this calls the host, and `%s` promises "
                                 "`no.host`"
                               : "this reaches the heap, and `%s` promises "
                                 "`no.alloc`",
                           written);
            kest_diags_fault(diags,
                             "the promise was allowed and the code says "
                             "otherwise");
            held = false;
        }
    }
    kest_arena_free(scratch);
    return held;
}

