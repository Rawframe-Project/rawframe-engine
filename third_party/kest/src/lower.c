#include "lower.h"

#include <stdlib.h>
#include <string.h>

// A jump and a loop carry how far as two bytes, so this is how much code there
// can be between one and where it lands.
#define MAX_REACH UINT16_MAX

struct KestLower {
    KestProgram *program;
    KestModule *module;
    KestArena *arena;
    // Which chunk comes next. Bodies arrive in the order the module's
    // functions were registered in, which is what makes this a count rather
    // than a search.
    uint32_t next;
    KestChunk *chunk;
    const KestIrBody *body;

    // Where each operation's first byte is, so that a branch can be filled in
    // once its landing place has been written.
    uint32_t *at;
    // Whether anything branches to each operation. Two instructions written
    // one after the other can be made into one, and that moves where the
    // second of them starts — so a jump landing between them would land inside
    // an instruction. Nothing at a place something points at may be taken back
    // into what comes after it. See D871.
    bool *landed_on;
    // The branches written and not yet filled in: where the two bytes are, and
    // which operation they lead to.
    uint32_t *waiting;
    uint32_t *leads_to;
    uint32_t wait_count;

    // The last instruction written and where it starts, so that what comes
    // next can take it into itself.
    uint8_t last_op;
    uint32_t last_at;
    // What was written before the last instruction, most recent last, so
    // that taking back two in a row -- an argument read in place and then an
    // element read into the frame -- still knows what is in front of both.
    // A body starts with none. See D1167.
    uint8_t earlier_op[4];
    uint32_t earlier_at[4];
    uint8_t earlier;
    uint32_t pointed_at;

    bool out_of_memory;
    // What bodies carried into this one at their calls need of the frame: the
    // slots one of them takes, above the body's own, and how much deeper the
    // stack may go. Every carried body is done with before the next begins,
    // so the slots are the widest one's rather than all of them. See D1156.
    uint16_t carried_slots;
    uint16_t carried_stack;
    uint16_t carried_slack;
};

typedef struct KestLower Lower;

static void refuse(Lower *lower, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_addv(lower->program->diags, KEST_SEVERITY_ERROR, code, span,
                    format, args);
    va_end(args);
}

// An operation with no instruction behind it. Reaching one means the body and
// this backend disagree about what a program is, which is this project's
// mistake and not the program's — so it says so, in the words the other half
// of the compiler uses for the same kind of news.
static void fault(Lower *lower, KestSpan span, const char *what) {
    kest_diags_disagree(lower->program->diags, span, "%s", what);
}

static void emit(Lower *lower, uint8_t byte, KestSpan origin) {
    if (lower->earlier == 4) {
        for (int i = 1; i < 4; i++) {
            lower->earlier_op[i - 1] = lower->earlier_op[i];
            lower->earlier_at[i - 1] = lower->earlier_at[i];
        }
        lower->earlier = 3;
    }
    lower->earlier_op[lower->earlier] = lower->last_op;
    lower->earlier_at[lower->earlier] = lower->last_at;
    lower->earlier++;
    lower->last_op = byte;
    lower->last_at = lower->chunk->code_count;
    if (!kest_chunk_emit(lower->module, lower->chunk, byte, origin.offset)) {
        lower->out_of_memory = true;
    }
}

static void emit_u16(Lower *lower, uint16_t value, KestSpan origin) {
    if (!kest_chunk_emit_u16(lower->module, lower->chunk, value,
                             origin.offset)) {
        lower->out_of_memory = true;
    }
}

static void take_back(Lower *lower) {
    kest_chunk_take_back(lower->chunk, lower->last_at);
    if (lower->earlier > 0) {
        lower->earlier--;
        lower->last_op = lower->earlier_op[lower->earlier];
        lower->last_at = lower->earlier_at[lower->earlier];
    } else {
        // Nothing known in front of it: an instruction no fusion looks for.
        lower->last_op = KEST_OP_STOP;
        lower->last_at = lower->chunk->code_count;
    }
}

// Whether the last thing written was `op` with one operand, nothing pointing
// between, and what the operand is. A load of one slot and a constant of one
// value are the two this is asked about: a run of slots cannot be the first
// half of a pair, because what follows it is not where it ends. See D961 and
// D1155.
static bool one_operand_before(const Lower *lower, uint8_t op,
                               uint16_t *operand) {
    if (lower->last_op != op || lower->last_at < lower->pointed_at ||
        lower->last_at + 3 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *operand = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    return true;
}

// What the load just written reads, when the last thing written was a load and
// nothing points between the two. Answers how many slots it took and where they
// start, or nought for anything else.
static uint16_t load_before(const Lower *lower, uint16_t *slot) {
    uint32_t width = lower->last_op == KEST_OP_LOAD    ? 3
                     : lower->last_op == KEST_OP_LOADN ? 5
                                                       : 0;
    if (width == 0 || lower->last_at < lower->pointed_at ||
        lower->last_at + width != lower->chunk->code_count) {
        return 0;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *slot = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    return (uint16_t)(width == 3 ? 1 : (at[3] | ((uint16_t)at[4] << 8)));
}

// Two loads of slots that sit next to each other are one load of both. A struct
// built out of locals is written as a load for each field, and the machine
// already has an instruction that takes a run of slots in one go — `load.n`,
// which a wide value is loaded with — so this is a dispatch off every field
// past the first and no instruction the machine did not have. See D871.
static void emit_load(Lower *lower, uint16_t slot, uint16_t size,
                      KestSpan origin) {
    uint16_t before = 0;
    uint16_t took = load_before(lower, &before);
    if (took > 0 && (uint32_t)before + took == slot &&
        (uint32_t)took + size <= UINT16_MAX) {
        take_back(lower);
        slot = before;
        size = (uint16_t)(took + size);
    }
    // And two that do not sit next to each other are still two pushes, which
    // is one instruction with two operands. The pair is a tenth of what the
    // frame step runs. See D961.
    uint16_t first = 0;
    if (size == 1 && one_operand_before(lower, KEST_OP_LOAD, &first)) {
        take_back(lower);
        emit(lower, KEST_OP_LOAD2, origin);
        emit_u16(lower, first, origin);
        emit_u16(lower, slot, origin);
        return;
    }
    emit(lower, size == 1 ? KEST_OP_LOAD : KEST_OP_LOADN, origin);
    emit_u16(lower, slot, origin);
    if (size != 1) {
        emit_u16(lower, size, origin);
    }
}

static bool fusing(void);
static bool local_and_constant_before(const Lower *lower, uint16_t *slot,
                                      uint16_t *which);

// Whether the last thing written was an index of a run whose element is this
// many slots wide, and which layout it read. An index pushes a struct onto the
// stack and the store after it copies it off again, and the store cannot begin
// until the index has finished: that is the dependency D1011 says is worth
// taking into one instruction. Three bytes, the same shape every other
// peephole here reads.
static bool index_before(const Lower *lower, uint16_t size, uint16_t *layout) {
    if (lower->last_op != KEST_OP_INDEX || lower->last_at < lower->pointed_at ||
        lower->last_at + 3 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    uint16_t which = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    if (which >= lower->module->layout_count ||
        lower->module->layouts[which].slots != size) {
        return false;
    }
    *layout = which;
    return true;
}

// Whether the last thing written was the address of one of an array. A read of
// a field through that address is the same read the machine can do from the
// array and the index, in one instruction and one bounds check rather than
// two dispatches and an address round trip. See D1044.
static bool elem_addr_before(const Lower *lower) {
    return lower->last_op == KEST_OP_ELEM_ADDR &&
           lower->last_at >= lower->pointed_at &&
           lower->last_at + 3 == lower->chunk->code_count;
}

// The arithmetic the store just after it is taking the answer of, when that
// arithmetic is the instruction before. Four of them: the two that carry a
// width and the two that do not, which is what the pair counts say the
// programs here run. The width is read back out of the bytes so that the
// fused instruction carries it too. See D1014.
static uint8_t arithmetic_before(const Lower *lower, uint16_t *kind) {
    if (lower->last_at < lower->pointed_at) {
        return 0;
    }
    uint32_t wide = lower->chunk->code_count - lower->last_at;
    const uint8_t *at = lower->chunk->code + lower->last_at;
    if (wide == 1) {
        if (lower->last_op == KEST_OP_ADD_F) {
            return KEST_OP_ADD_F_TO;
        }
        if (lower->last_op == KEST_OP_SUB_F) {
            return KEST_OP_SUB_F_TO;
        }
        return 0;
    }
    if (wide != 3) {
        return 0;
    }
    *kind = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    if (lower->last_op == KEST_OP_ADD_I_NARROW) {
        return KEST_OP_ADD_I_NARROW_TO;
    }
    if (lower->last_op == KEST_OP_SUB_I_NARROW) {
        return KEST_OP_SUB_I_NARROW_TO;
    }
    return 0;
}

static bool two_locals_before(const Lower *lower, uint16_t *first,
                              uint16_t *second);

static void emit_store(Lower *lower, uint16_t slot, uint16_t size,
                       KestSpan origin) {
    uint16_t layout = 0;
    if (size != 1 && fusing() && index_before(lower, size, &layout)) {
        take_back(lower);
        // And the run and the index it was read by, where both are locals
        // nothing points between: every `let one = world[at]`. See D1167.
        uint16_t holds = 0;
        uint16_t at = 0;
        if (two_locals_before(lower, &holds, &at)) {
            take_back(lower);
            emit(lower, KEST_OP_INDEX_TO_LL, origin);
            emit_u16(lower, layout, origin);
            emit_u16(lower, slot, origin);
            emit_u16(lower, holds, origin);
            emit_u16(lower, at, origin);
            if (size + 2u > lower->chunk->fused_slots) {
                lower->chunk->fused_slots = (uint16_t)(size + 2u);
            }
            return;
        }
        emit(lower, KEST_OP_INDEX_TO, origin);
        emit_u16(lower, layout, origin);
        emit_u16(lower, slot, origin);
        if (size > lower->chunk->fused_slots) {
            lower->chunk->fused_slots = size;
        }
        return;
    }
    uint16_t kind = 0;
    uint8_t made = size != 1 || !fusing() ? 0 : arithmetic_before(lower, &kind);
    if (made != 0) {
        take_back(lower);
        // And the local and the constant it was made of, when the local is
        // the one being written: `x += k` as one instruction. See D1155.
        uint16_t read = 0;
        uint16_t which = 0;
        if ((made == KEST_OP_ADD_I_NARROW_TO ||
             made == KEST_OP_SUB_I_NARROW_TO) &&
            local_and_constant_before(lower, &read, &which) && read == slot) {
            if (lower->chunk->fused_slots < 2) {
                lower->chunk->fused_slots = 2;
            }
            take_back(lower);
            emit(lower,
                 made == KEST_OP_ADD_I_NARROW_TO ? KEST_OP_ADD_K_SELF
                                                  : KEST_OP_SUB_K_SELF,
                 origin);
            emit_u16(lower, kind, origin);
            emit_u16(lower, slot, origin);
            emit_u16(lower, which, origin);
            return;
        }
        // And a float sum of two locals nothing points between, read where
        // they are. See D1166.
        uint16_t first = 0;
        uint16_t second = 0;
        if ((made == KEST_OP_ADD_F_TO || made == KEST_OP_SUB_F_TO) &&
            two_locals_before(lower, &first, &second)) {
            if (lower->chunk->fused_slots < 2) {
                lower->chunk->fused_slots = 2;
            }
            take_back(lower);
            emit(lower,
                 made == KEST_OP_ADD_F_TO ? KEST_OP_ADD_F_LL : KEST_OP_SUB_F_LL,
                 origin);
            emit_u16(lower, slot, origin);
            emit_u16(lower, first, origin);
            emit_u16(lower, second, origin);
            return;
        }
        emit(lower, made, origin);
        if (made == KEST_OP_ADD_I_NARROW_TO || made == KEST_OP_SUB_I_NARROW_TO) {
            emit_u16(lower, kind, origin);
        }
        emit_u16(lower, slot, origin);
        return;
    }
    // A local moved by a constant and written back where it was read: made as
    // the arithmetic on a local first, and one instruction with the store.
    // The operands are the same three. See D1155 and D1168.
    if (size == 1 && fusing() &&
        (lower->last_op == KEST_OP_ADD_I_NARROW_K ||
         lower->last_op == KEST_OP_SUB_I_NARROW_K) &&
        lower->last_at >= lower->pointed_at &&
        lower->last_at + 7 == lower->chunk->code_count) {
        const uint8_t *at = lower->chunk->code + lower->last_at;
        uint16_t cut_to = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
        uint16_t read = (uint16_t)(at[3] | ((uint16_t)at[4] << 8));
        uint16_t which = (uint16_t)(at[5] | ((uint16_t)at[6] << 8));
        if (read == slot) {
            uint8_t self = lower->last_op == KEST_OP_ADD_I_NARROW_K
                               ? KEST_OP_ADD_K_SELF
                               : KEST_OP_SUB_K_SELF;
            take_back(lower);
            emit(lower, self, origin);
            emit_u16(lower, cut_to, origin);
            emit_u16(lower, slot, origin);
            emit_u16(lower, which, origin);
            return;
        }
    }
    uint16_t constant = 0;
    if (size == 1 && fusing() &&
        one_operand_before(lower, KEST_OP_CONST, &constant)) {
        // The constant is never on the stack now. See D1155.
        if (lower->chunk->fused_slots < 1) {
            lower->chunk->fused_slots = 1;
        }
        take_back(lower);
        emit(lower, KEST_OP_STORE_K, origin);
        emit_u16(lower, slot, origin);
        emit_u16(lower, constant, origin);
        return;
    }
    emit(lower, size == 1 ? KEST_OP_STORE : KEST_OP_STOREN, origin);
    emit_u16(lower, slot, origin);
    if (size != 1) {
        emit_u16(lower, size, origin);
    }
}

// Whether the fusions this file makes are made at all. There is one way to
// turn them off and it is here rather than on the command line: what it is for
// is compiling the same program twice and requiring the same answer, which is
// how a transformation is held to being one that keeps a program's meaning.
// D1009 says every optimization is held that way and D1011 is the first one
// that was.
//
// A plainer program runs the same operations in the same order; what differs
// is how many instructions they are written as. Nothing a reader sees changes
// except `kest emit`, which prints what was emitted and is where the
// difference is meant to show.
//
// Read once, because a compiler that asked the environment per body would be
// one whose answer could change half way through a program.
static bool fusing(void) {
    static int decided = -1;
    return !kest_ir_asked_off("KEST_PLAIN", &decided);
}

// A run of values the chunk holds, and the instruction that reads it. A local
// and then a constant is the commonest pair this machine runs — every `x + 1`,
// every `i < n` against a written number — and it is one instruction with two
// operands. See D961.
static void emit_constant(Lower *lower, uint16_t first, uint16_t count,
                          KestSpan origin) {
    const KestIrBody *body = lower->body;
    if ((uint32_t)first + count > body->constant_count) {
        fault(lower, origin, "this reads a value the body has not got");
        return;
    }
    uint32_t index = kest_chunk_constant_run(
        lower->module, lower->chunk, body->constants + first,
        body->constant_classes + first, count);
    if (lower->module->out_of_room) {
        lower->out_of_memory = true;
        return;
    }
    uint16_t slot = 0;
    if (fusing() && count == 1 && index <= UINT16_MAX &&
        one_operand_before(lower, KEST_OP_LOAD, &slot)) {
        take_back(lower);
        emit(lower, KEST_OP_LOADK, origin);
        emit_u16(lower, slot, origin);
        emit_u16(lower, (uint16_t)index, origin);
        return;
    }
    emit(lower, count == 1 ? KEST_OP_CONST : KEST_OP_CONST_RUN, origin);
    emit_u16(lower, (uint16_t)index, origin);
    if (count != 1) {
        emit_u16(lower, count, origin);
    }
}

// The arithmetic a cut arrives behind, when it is the instruction just before
// it. Each of the three is one byte and carries nothing after it, so it is the
// last instruction when it is the last byte — the same thing that lets a jump
// take back the comparison before it. See D868.
static uint8_t fused_with_narrow(uint8_t arithmetic) {
    switch (arithmetic) {
    case KEST_OP_ADD_I:
        return KEST_OP_ADD_I_NARROW;
    case KEST_OP_SUB_I:
        return KEST_OP_SUB_I_NARROW;
    case KEST_OP_MUL_I:
        return KEST_OP_MUL_I_NARROW;
    default:
        return KEST_OP_NARROW;
    }
}

// The same arithmetic with a constant for its right side, and a local for its
// left or what is on the stack. See D1168.
static uint8_t by_a_constant(uint8_t narrowed, bool of_a_local) {
    if (narrowed == KEST_OP_ADD_I_NARROW) {
        return of_a_local ? KEST_OP_ADD_I_NARROW_K : KEST_OP_ADD_I_NARROW_C;
    }
    if (narrowed == KEST_OP_SUB_I_NARROW) {
        return of_a_local ? KEST_OP_SUB_I_NARROW_K : KEST_OP_SUB_I_NARROW_C;
    }
    return of_a_local ? KEST_OP_MUL_I_NARROW_K : KEST_OP_MUL_I_NARROW_C;
}

// The comparison a jump reads, when the jump is the next thing after it. Every
// one of these leaves its answer on the stack for one instruction, which then
// pops it and throws it away, so the pair is one instruction and one dispatch.
// Only whole numbers and floats: they are what a loop counts with and what an
// index is.
// `a || b` asks whether the first one is true, and `!x` asks the same question
// of one thing, so both were written as `not` and then a jump that reads what
// `not` wrote. The jump asks it directly.
static uint8_t fused_with_jump(uint8_t compare, bool asking_true) {
    switch (compare) {
    case KEST_OP_LT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_I : KEST_OP_JUMP_FALSE_LT_I;
    case KEST_OP_LE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_I : KEST_OP_JUMP_FALSE_LE_I;
    case KEST_OP_GT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_I : KEST_OP_JUMP_FALSE_GT_I;
    case KEST_OP_GE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_I : KEST_OP_JUMP_FALSE_GE_I;
    case KEST_OP_EQ_I:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_I : KEST_OP_JUMP_FALSE_EQ_I;
    case KEST_OP_NE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_I : KEST_OP_JUMP_FALSE_NE_I;
    case KEST_OP_LT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_F : KEST_OP_JUMP_FALSE_LT_F;
    case KEST_OP_LE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_F : KEST_OP_JUMP_FALSE_LE_F;
    case KEST_OP_GT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_F : KEST_OP_JUMP_FALSE_GT_F;
    case KEST_OP_GE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_F : KEST_OP_JUMP_FALSE_GE_F;
    case KEST_OP_EQ_F:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_F : KEST_OP_JUMP_FALSE_EQ_F;
    case KEST_OP_NE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_F : KEST_OP_JUMP_FALSE_NE_F;
    default:
        return asking_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE;
    }
}

// Which instruction compares, by what is being compared. One row an operation,
// because the question is the same four every time — a piece of text, a float,
// an unsigned number, or the plain one — and it was written out six times.
// `==` and `!=` over a run of slots are the exception and are answered where
// they are written: both sides are a run rather than one.
static const struct {
    uint8_t operation;
    uint8_t whole;
    uint8_t without_sign;
    uint8_t real;
    uint8_t text;
} COMPARISONS[] = {
    {KEST_IR_LT, KEST_OP_LT_I, KEST_OP_LT_U, KEST_OP_LT_F, KEST_OP_LT_T},
    {KEST_IR_LE, KEST_OP_LE_I, KEST_OP_LE_U, KEST_OP_LE_F, KEST_OP_LE_T},
    {KEST_IR_GT, KEST_OP_GT_I, KEST_OP_GT_U, KEST_OP_GT_F, KEST_OP_GT_T},
    {KEST_IR_GE, KEST_OP_GE_I, KEST_OP_GE_U, KEST_OP_GE_F, KEST_OP_GE_T},
    // Equality does not ask whether a number has a sign: the same bits are
    // the same bits either way.
    {KEST_IR_EQ, KEST_OP_EQ_I, KEST_OP_EQ_I, KEST_OP_EQ_F, KEST_OP_EQ_T},
    {KEST_IR_NE, KEST_OP_NE_I, KEST_OP_NE_I, KEST_OP_NE_F, KEST_OP_NE_T},
};

static uint8_t compares(uint16_t operation, const KestType *type) {
    bool text = type != NULL && type->tag == KEST_T_TEXT;
    bool real = kest_is_float(type);
    bool without_sign = kest_is_unsigned(type);
    for (size_t i = 0; i < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); i++) {
        if (COMPARISONS[i].operation != operation) {
            continue;
        }
        return text ? COMPARISONS[i].text
               : real ? COMPARISONS[i].real
               : without_sign ? COMPARISONS[i].without_sign
                              : COMPARISONS[i].whole;
    }
    return 0;
}

// The arithmetic for an operation and what it is on. Which of the three
// families it belongs to is the type's to say, which is why the body carries
// one addition and this carries three.
static uint8_t arithmetic(uint16_t operation, const KestType *type) {
    bool real = kest_is_float(type);
    bool narrow = kest_is_narrow(type);
    bool without_sign = kest_is_unsigned(type);
    switch (operation) {
    case KEST_IR_ADD:
        return real ? (narrow ? KEST_OP_ADD_F32 : KEST_OP_ADD_F)
                    : KEST_OP_ADD_I;
    case KEST_IR_SUB:
        return real ? (narrow ? KEST_OP_SUB_F32 : KEST_OP_SUB_F)
                    : KEST_OP_SUB_I;
    case KEST_IR_MUL:
        return real ? (narrow ? KEST_OP_MUL_F32 : KEST_OP_MUL_F)
                    : KEST_OP_MUL_I;
    case KEST_IR_DIV:
        return real ? (narrow ? KEST_OP_DIV_F32 : KEST_OP_DIV_F)
                    : (without_sign ? KEST_OP_DIV_U : KEST_OP_DIV_I);
    case KEST_IR_MOD:
        return real ? (narrow ? KEST_OP_MOD_F32 : KEST_OP_MOD_F)
                    : (without_sign ? KEST_OP_MOD_U : KEST_OP_MOD_I);
    case KEST_IR_NEG:
        return real ? (narrow ? KEST_OP_NEG_F32 : KEST_OP_NEG_F)
                    : KEST_OP_NEG_I;
    case KEST_IR_AND:
        return KEST_OP_AND_I;
    case KEST_IR_OR:
        return KEST_OP_OR_I;
    case KEST_IR_XOR:
        return KEST_OP_XOR_I;
    case KEST_IR_FLIP:
        return KEST_OP_NOT_I;
    case KEST_IR_SHL:
        return KEST_OP_SHL;
    case KEST_IR_SHR:
        return without_sign ? KEST_OP_SHR_U : KEST_OP_SHR_I;
    default:
        return 0;
    }
}

// What a value of this type is written as. A number knows its own width and
// whether it has a sign, so the operation says "the text of this" and the type
// says which.
static uint8_t writes_text(const KestType *type) {
    if (type == NULL) {
        return KEST_OP_TEXT_I;
    }
    if (type->tag == KEST_T_FLAGS) {
        return KEST_OP_TEXT_FLAGS;
    }
    if (type->tag == KEST_T_ENUM || type->tag == KEST_T_OPTIONAL ||
        type->tag == KEST_T_STRUCT || type->tag == KEST_T_FIXED) {
        return KEST_OP_TEXT_VALUE;
    }
    if (type->tag == KEST_T_FLOAT) {
        return kest_is_narrow(type) ? KEST_OP_TEXT_F32 : KEST_OP_TEXT_F;
    }
    if (type->tag == KEST_T_BOOL) {
        return KEST_OP_TEXT_B;
    }
    return kest_is_unsigned(type) ? KEST_OP_TEXT_U : KEST_OP_TEXT_I;
}

// A branch whose landing place has not been written yet. Where the two bytes
// are is kept beside which operation they lead to, and both are filled in when
// the body is finished.
static void waits_for(Lower *lower, uint32_t target, KestSpan origin) {
    emit_u16(lower, 0, origin);
    if (lower->out_of_memory) {
        return;
    }
    lower->waiting[lower->wait_count] = lower->chunk->code_count - 2;
    lower->leads_to[lower->wait_count] = target;
    lower->wait_count++;
}

// A branch back to something already written, which is a distance rather than
// a place to fill in.
static void reaches_back(Lower *lower, uint32_t target, uint32_t after,
                         KestSpan origin) {
    uint32_t distance = lower->chunk->code_count + after - lower->at[target];
    if (distance > MAX_REACH) {
        refuse(lower, origin, "K0503",
               "this loop is %u bytes of code, and a loop reaches back %u",
               distance, (uint32_t)MAX_REACH);
        distance = 0;
    }
    emit_u16(lower, (uint16_t)distance, origin);
}

static void fill_in_branches(Lower *lower) {
    if (lower->out_of_memory) {
        return;
    }
    for (uint32_t i = 0; i < lower->wait_count; i++) {
        uint32_t placeholder = lower->waiting[i];
        uint32_t lands = lower->at[lower->leads_to[i]];
        uint32_t distance = lands - placeholder - 2;
        if (distance > MAX_REACH) {
            refuse(lower, lower->body->declared, "K0503",
                   "this jumps %u bytes of code, and a jump reaches %u",
                   distance, (uint32_t)MAX_REACH);
            continue;
        }
        lower->chunk->code[placeholder] = (uint8_t)(distance & 0xff);
        lower->chunk->code[placeholder + 1] = (uint8_t)(distance >> 8);
    }
}

// The branch a comparison or a `not` just before it is taken into. Twice at
// most: the jump takes back the `not` before it, and then the comparison that
// `not` was turning round. Both are one byte and both came through `emit`,
// which is what makes "the last instruction" a thing that can be known rather
// than guessed at from the bytes.
static uint8_t asks(Lower *lower, bool when_true) {
    uint8_t op = when_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE;
    for (uint32_t round = 0; fusing() && round < 2; round++) {
        // Only while the branch is still a plain one: one that has already
        // taken a comparison into itself is not looking for another.
        if ((op != KEST_OP_JUMP_FALSE && op != KEST_OP_JUMP_TRUE) ||
            lower->last_at + 1 != lower->chunk->code_count ||
            lower->last_at < lower->pointed_at) {
            break;
        }
        bool asking_true = op == KEST_OP_JUMP_TRUE;
        uint8_t fused =
            lower->last_op == KEST_OP_NOT
                ? (asking_true ? KEST_OP_JUMP_FALSE : KEST_OP_JUMP_TRUE)
                : fused_with_jump(lower->last_op, asking_true);
        if (fused == op) {
            break;
        }
        take_back(lower);
        op = fused;
    }
    return op;
}

// The jump a comparison was taken into, and the two it can become when what
// is before it is a `load.k` or a `const` nothing points between: a local
// weighed against a constant, or what is on the stack. One row a comparison;
// a float one has the first and not the second, and has it both ways round.
// See D1154, D1155 and D1165.
static const struct {
    uint8_t jump;
    uint8_t local;
    uint8_t top;
    // And weighing an element a run and an index read where they are, when
    // what is before the constant is that read. See D1178.
    uint8_t element;
} WEIGHED[] = {
    {KEST_OP_JUMP_FALSE_LT_I, KEST_OP_JUMP_FALSE_LT_K, KEST_OP_JUMP_FALSE_LT_C,
     KEST_OP_JUMP_FALSE_LT_E},
    {KEST_OP_JUMP_FALSE_LE_I, KEST_OP_JUMP_FALSE_LE_K, KEST_OP_JUMP_FALSE_LE_C,
     KEST_OP_JUMP_FALSE_LE_E},
    {KEST_OP_JUMP_FALSE_GT_I, KEST_OP_JUMP_FALSE_GT_K, KEST_OP_JUMP_FALSE_GT_C,
     KEST_OP_JUMP_FALSE_GT_E},
    {KEST_OP_JUMP_FALSE_GE_I, KEST_OP_JUMP_FALSE_GE_K, KEST_OP_JUMP_FALSE_GE_C,
     KEST_OP_JUMP_FALSE_GE_E},
    {KEST_OP_JUMP_FALSE_EQ_I, KEST_OP_JUMP_FALSE_EQ_K, KEST_OP_JUMP_FALSE_EQ_C,
     KEST_OP_JUMP_FALSE_EQ_E},
    {KEST_OP_JUMP_FALSE_NE_I, KEST_OP_JUMP_FALSE_NE_K, KEST_OP_JUMP_FALSE_NE_C,
     KEST_OP_JUMP_FALSE_NE_E},
    {KEST_OP_JUMP_FALSE_LT_F, KEST_OP_JUMP_FALSE_LT_FK, 0, 0},
    {KEST_OP_JUMP_FALSE_LE_F, KEST_OP_JUMP_FALSE_LE_FK, 0, 0},
    {KEST_OP_JUMP_FALSE_GT_F, KEST_OP_JUMP_FALSE_GT_FK, 0, 0},
    {KEST_OP_JUMP_FALSE_GE_F, KEST_OP_JUMP_FALSE_GE_FK, 0, 0},
    {KEST_OP_JUMP_FALSE_EQ_F, KEST_OP_JUMP_FALSE_EQ_FK, 0, 0},
    {KEST_OP_JUMP_FALSE_NE_F, KEST_OP_JUMP_FALSE_NE_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_LT_F, KEST_OP_JUMP_TRUE_LT_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_LE_F, KEST_OP_JUMP_TRUE_LE_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_GT_F, KEST_OP_JUMP_TRUE_GT_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_GE_F, KEST_OP_JUMP_TRUE_GE_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_EQ_F, KEST_OP_JUMP_TRUE_EQ_FK, 0, 0},
    {KEST_OP_JUMP_TRUE_NE_F, KEST_OP_JUMP_TRUE_NE_FK, 0, 0},
};

static uint8_t weighed(uint8_t jump, bool against_local) {
    for (size_t i = 0; i < sizeof(WEIGHED) / sizeof(WEIGHED[0]); i++) {
        if (WEIGHED[i].jump == jump) {
            return against_local ? WEIGHED[i].local : WEIGHED[i].top;
        }
    }
    return 0;
}

static uint8_t weighed_element(uint8_t jump) {
    for (size_t i = 0; i < sizeof(WEIGHED) / sizeof(WEIGHED[0]); i++) {
        if (WEIGHED[i].jump == jump) {
            return WEIGHED[i].element;
        }
    }
    return 0;
}

// What the element read just written reads, when the last thing written was
// an `index.ll` nothing points between: the run, the index and the layout.
static bool element_before(const Lower *lower, uint16_t *holds, uint16_t *at,
                           uint16_t *layout) {
    if (lower->last_op != KEST_OP_INDEX_LL ||
        lower->last_at < lower->pointed_at ||
        lower->last_at + 7 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *code = lower->chunk->code + lower->last_at;
    *holds = (uint16_t)(code[1] | ((uint16_t)code[2] << 8));
    *at = (uint16_t)(code[3] | ((uint16_t)code[4] << 8));
    *layout = (uint16_t)(code[5] | ((uint16_t)code[6] << 8));
    return true;
}

static bool local_and_constant_before(const Lower *lower, uint16_t *slot,
                                      uint16_t *which) {
    if (lower->last_op != KEST_OP_LOADK || lower->last_at < lower->pointed_at ||
        lower->last_at + 5 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *slot = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    *which = (uint16_t)(at[3] | ((uint16_t)at[4] << 8));
    return true;
}

// A small body carried to where it is called rather than called: its
// instructions written into the caller with its slots moved above the
// caller's own and its constants added to the caller's, the arguments stored
// into those slots where a call would have handed them over, and every
// `return` a jump to the end, which is the same three bytes -- so the jumps
// inside it land where they landed. What `rules` measured by writing three of
// them out by hand was 8.8 per cent, and D1070's reasons for not doing this
// are each answered by what may be carried: see D1156.
//
// What a carried body may hold is what cannot be refused, loop or call:
// arithmetic that wraps, comparisons, constants, locals and jumps forward.
// Nothing in one can stop the program, so no refusal is ever said from inside
// a body that is not standing in a frame of its own, and nothing in one
// spends a step of a budget, so what a budget bounds is what it bounded less
// the calls. Everything else is the kind of operand a call site cannot know
// how to move, and a body with one in it is called as it always was.
// A distance is how far forward a jump lands, counted from the end of the
// jump, which is the one number the carrying can change: what a body ends
// with is left out of it. See D1156.
typedef enum { NO_OPERAND, A_SLOT, A_CONSTANT, A_NUMBER, A_DISTANCE } Operand;

static const struct {
    uint8_t op;
    Operand operands[3];
} CARRIED[] = {
    {KEST_OP_CONST, {A_CONSTANT}},
    {KEST_OP_CONST_RUN, {A_CONSTANT, A_NUMBER}},
    {KEST_OP_LOAD, {A_SLOT}},
    {KEST_OP_STORE, {A_SLOT}},
    {KEST_OP_LOADN, {A_SLOT, A_NUMBER}},
    {KEST_OP_STOREN, {A_SLOT, A_NUMBER}},
    {KEST_OP_LOAD2, {A_SLOT, A_SLOT}},
    {KEST_OP_LOADK, {A_SLOT, A_CONSTANT}},
    {KEST_OP_STORE_K, {A_SLOT, A_CONSTANT}},
    {KEST_OP_ADD_K_SELF, {A_NUMBER, A_SLOT, A_CONSTANT}},
    {KEST_OP_SUB_K_SELF, {A_NUMBER, A_SLOT, A_CONSTANT}},
    {KEST_OP_ADD_I_NARROW_TO, {A_NUMBER, A_SLOT}},
    {KEST_OP_SUB_I_NARROW_TO, {A_NUMBER, A_SLOT}},
    {KEST_OP_ADD_F_TO, {A_SLOT}},
    {KEST_OP_ADD_F_LL, {A_SLOT, A_SLOT, A_SLOT}},
    {KEST_OP_SUB_F_LL, {A_SLOT, A_SLOT, A_SLOT}},
    {KEST_OP_SUB_F_TO, {A_SLOT}},
    {KEST_OP_NARROW, {A_NUMBER}},
    {KEST_OP_ADD_I_NARROW, {A_NUMBER}},
    {KEST_OP_SUB_I_NARROW, {A_NUMBER}},
    {KEST_OP_MUL_I_NARROW, {A_NUMBER}},
    {KEST_OP_ADD_I_NARROW_C, {A_NUMBER, A_CONSTANT}},
    {KEST_OP_SUB_I_NARROW_C, {A_NUMBER, A_CONSTANT}},
    {KEST_OP_MUL_I_NARROW_C, {A_NUMBER, A_CONSTANT}},
    {KEST_OP_ADD_I_NARROW_K, {A_NUMBER, A_SLOT, A_CONSTANT}},
    {KEST_OP_SUB_I_NARROW_K, {A_NUMBER, A_SLOT, A_CONSTANT}},
    {KEST_OP_MUL_I_NARROW_K, {A_NUMBER, A_SLOT, A_CONSTANT}},
    {KEST_OP_ROTATE, {A_NUMBER}},
    {KEST_OP_POPN, {A_NUMBER}},
    {KEST_OP_POP, {NO_OPERAND}},
    {KEST_OP_TRUE, {NO_OPERAND}},
    {KEST_OP_FALSE, {NO_OPERAND}},
    {KEST_OP_ADD_I, {NO_OPERAND}},
    {KEST_OP_SUB_I, {NO_OPERAND}},
    {KEST_OP_MUL_I, {NO_OPERAND}},
    {KEST_OP_NEG_I, {NO_OPERAND}},
    {KEST_OP_AND_I, {NO_OPERAND}},
    {KEST_OP_OR_I, {NO_OPERAND}},
    {KEST_OP_XOR_I, {NO_OPERAND}},
    {KEST_OP_NOT_I, {NO_OPERAND}},
    {KEST_OP_NOT, {NO_OPERAND}},
    {KEST_OP_I2F, {NO_OPERAND}},
    {KEST_OP_U2F, {NO_OPERAND}},
    {KEST_OP_F2I, {A_NUMBER}},
    {KEST_OP_TO_F32, {NO_OPERAND}},
    {KEST_OP_FLOAT_BITS, {A_NUMBER}},
    {KEST_OP_BITS_F32, {NO_OPERAND}},
    {KEST_OP_ADD_F, {NO_OPERAND}},
    {KEST_OP_SUB_F, {NO_OPERAND}},
    {KEST_OP_MUL_F, {NO_OPERAND}},
    {KEST_OP_DIV_F, {NO_OPERAND}},
    {KEST_OP_NEG_F, {NO_OPERAND}},
    {KEST_OP_ADD_F32, {NO_OPERAND}},
    {KEST_OP_SUB_F32, {NO_OPERAND}},
    {KEST_OP_MUL_F32, {NO_OPERAND}},
    {KEST_OP_DIV_F32, {NO_OPERAND}},
    {KEST_OP_NEG_F32, {NO_OPERAND}},
    {KEST_OP_LT_I, {NO_OPERAND}},
    {KEST_OP_LE_I, {NO_OPERAND}},
    {KEST_OP_GT_I, {NO_OPERAND}},
    {KEST_OP_GE_I, {NO_OPERAND}},
    {KEST_OP_LT_U, {NO_OPERAND}},
    {KEST_OP_LE_U, {NO_OPERAND}},
    {KEST_OP_GT_U, {NO_OPERAND}},
    {KEST_OP_GE_U, {NO_OPERAND}},
    {KEST_OP_LT_F, {NO_OPERAND}},
    {KEST_OP_LE_F, {NO_OPERAND}},
    {KEST_OP_GT_F, {NO_OPERAND}},
    {KEST_OP_GE_F, {NO_OPERAND}},
    {KEST_OP_EQ_I, {NO_OPERAND}},
    {KEST_OP_NE_I, {NO_OPERAND}},
    {KEST_OP_EQ_F, {NO_OPERAND}},
    {KEST_OP_NE_F, {NO_OPERAND}},
    // Forward jumps carry a distance, which is the same inside the caller as
    // inside the body because the body is written whole and in order.
    {KEST_OP_JUMP, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LT_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GT_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_EQ_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_NE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LT_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GT_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_EQ_I, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_NE_I, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LT_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GT_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_EQ_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_NE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LT_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GT_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_EQ_F, {A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_NE_F, {A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LT_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LE_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GT_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GE_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_EQ_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_NE_K, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LT_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LE_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GT_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GE_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_EQ_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_NE_C, {A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LT_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_LE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GT_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_GE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_EQ_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_FALSE_NE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LT_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_LE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GT_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_GE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_EQ_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    {KEST_OP_JUMP_TRUE_NE_FK, {A_SLOT, A_CONSTANT, A_DISTANCE}},
    // Carried as a jump to the end, which is the one thing about it that
    // changes.
    {KEST_OP_RETURN, {A_DISTANCE}},
};

// The operands of one of those, or NULL for an instruction a body carrying it
// could not be moved with.
static const Operand *carried_operands(uint8_t op) {
    for (size_t i = 0; i < sizeof(CARRIED) / sizeof(CARRIED[0]); i++) {
        if (CARRIED[i].op == op) {
            return CARRIED[i].operands;
        }
    }
    return NULL;
}

// The most bytes of code a carried body may be. A body is carried to every
// place it is called from, so what a program grows by is this times the
// calls; what a call costs is paid once a call whatever the body's size, so
// past a few dozen instructions there is nothing left to buy. See D1156. A
// rule deciding between five states is forty instructions in 185 bytes, and
// carried it is eight per cent of `bench/control`, so the few dozen is forty
// or so rather than thirty. See D1175.
#define MOST_CARRIED 256

// Whether the function at `index` may be carried to a call of it from the
// body being written: written already, by the same file, small, and made of
// nothing but what `CARRIED` names.
static uint16_t operand_at(const uint8_t *code, uint32_t at);

static bool may_carry(const Lower *lower, uint16_t index) {
    if (!fusing() || lower->module->carrying_off ||
        index >= lower->next - 1 ||
        index >= lower->module->count) {
        return false;
    }
    const KestChunk *callee = lower->module->functions[index];
    if (callee == NULL || callee->native != NULL || callee->code_count == 0 ||
        callee->code_count > MOST_CARRIED ||
        callee->source != lower->chunk->source ||
        callee->origin_count == 0) {
        return false;
    }
    for (uint32_t at = 0; at < callee->code_count;) {
        const Operand *operands = carried_operands(callee->code[at]);
        if (operands == NULL) {
            return false;
        }
        // And the row held to the instruction's own width, so that a row
        // written wrong is a body called as it always was rather than one
        // carried a byte out: `f2i` was written with no operand the first
        // time, and the body it was in answered another case. See D1156.
        uint32_t said = 1;
        for (int o = 0; o < 3 && operands[o] != NO_OPERAND; o++) {
            said += 2;
        }
        if (said != kest_op_wide(callee->code[at])) {
            return false;
        }
        at += said;
    }
    // And every jump in it landing where an instruction starts. A carried
    // body is laid out again before it is written (D1196), and where a jump
    // lands is found by where its instruction moved to: one landing inside an
    // instruction lands nowhere that moved, and was carried as a jump off the
    // end of the code rather than the body run as the machine would run it.
    // Nothing this compiler writes lands inside an instruction; a fold that
    // took one back across a landing would, and the machine is what says so.
    bool starts[MOST_CARRIED + 1] = {false};
    for (uint32_t at = 0; at < callee->code_count;
         at += kest_op_wide(callee->code[at])) {
        starts[at] = true;
    }
    starts[callee->code_count] = true;
    for (uint32_t at = 0; at < callee->code_count;) {
        const Operand *operands = carried_operands(callee->code[at]);
        uint32_t wide = kest_op_wide(callee->code[at]);
        uint32_t read = at + 1;
        for (int o = 0; o < 3 && operands[o] != NO_OPERAND; o++) {
            uint32_t lands = at + wide + operand_at(callee->code, read);
            if (operands[o] == A_DISTANCE &&
                callee->code[at] != KEST_OP_RETURN &&
                (lands > callee->code_count || !starts[lands])) {
                return false;
            }
            read += 2;
        }
        at += wide;
    }
    return true;
}

static uint16_t operand_at(const uint8_t *code, uint32_t at) {
    return (uint16_t)(code[at] | ((uint16_t)code[at + 1] << 8));
}

// Whether a carried body writes any of its parameters. One that does not can
// read them where the caller's locals are, which is what a call that pushed
// them out of a local and a carry that stored them back into a slot was two
// copies of the same thing for.
static bool writes_a_parameter(const KestChunk *callee) {
    for (uint32_t at = 0; at < callee->code_count;) {
        uint8_t op = callee->code[at];
        const Operand *operands = carried_operands(op);
        bool writes = op == KEST_OP_STORE || op == KEST_OP_STOREN ||
                      op == KEST_OP_STORE_K || op == KEST_OP_ADD_K_SELF ||
                      op == KEST_OP_SUB_K_SELF ||
                      op == KEST_OP_ADD_I_NARROW_TO ||
                      op == KEST_OP_SUB_I_NARROW_TO ||
                      op == KEST_OP_ADD_F_TO || op == KEST_OP_SUB_F_TO ||
                      op == KEST_OP_ADD_F_LL || op == KEST_OP_SUB_F_LL;
        uint32_t read = at + 1;
        for (int o = 0; writes && o < 3 && operands[o] != NO_OPERAND; o++) {
            if (operands[o] == A_SLOT &&
                operand_at(callee->code, read) < callee->param_slots) {
                return true;
            }
            read += 2;
        }
        at += kest_op_wide(op);
    }
    return false;
}

// The caller's slots the last of the arguments were just loaded out of, one a
// parameter slot, when the last thing written was one load of them nothing
// points between. Answers how many, or nought: a call `f(xs[i], dt)` loads
// `dt` last, and the body can read it where it is while `xs[i]` is stored.
#define MOST_ALIASED 16
static uint16_t loaded_just_now(const Lower *lower, uint16_t wanted,
                                uint16_t from[MOST_ALIASED]) {
    if (wanted == 0 || lower->last_at < lower->pointed_at) {
        return 0;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    uint32_t wide = lower->chunk->code_count - lower->last_at;
    if (lower->last_op == KEST_OP_LOAD && wide == 3) {
        from[0] = operand_at(at, 1);
        return 1;
    }
    if (lower->last_op == KEST_OP_LOADN && wide == 5 &&
        operand_at(at, 3) <= wanted && operand_at(at, 3) <= MOST_ALIASED) {
        uint16_t many = operand_at(at, 3);
        for (uint16_t i = 0; i < many; i++) {
            from[i] = (uint16_t)(operand_at(at, 1) + i);
        }
        return many;
    }
    if (lower->last_op == KEST_OP_LOAD2 && wide == 5 && wanted >= 2) {
        from[0] = operand_at(at, 1);
        from[1] = operand_at(at, 3);
        return 2;
    }
    return 0;
}

// Where a carried body's slot is in its caller: the parameters stored before
// the base, the ones read where the caller has them in `from`, and the body's
// other slots after the stored ones.
static uint16_t carried_slot(uint16_t value, uint16_t stored, uint16_t aliased,
                             const uint16_t *from, uint16_t base) {
    return value < stored ? (uint16_t)(value + base)
           : value < stored + aliased ? from[value - stored]
                                       : (uint16_t)(value - aliased + base);
}

// Whether every run of slots the body reads or writes as one is still one run
// where the slots land. A parameter read where the caller has it is wherever
// the caller had it, and `load.n` over two of them reads the two slots after
// the first: a body with such a run is carried with its arguments stored.
static bool runs_hold(const KestChunk *callee, uint16_t stored,
                      uint16_t aliased, const uint16_t *from, uint16_t base) {
    for (uint32_t at = 0; at < callee->code_count;) {
        uint8_t op = callee->code[at];
        if (op == KEST_OP_LOADN || op == KEST_OP_STOREN) {
            uint16_t first = operand_at(callee->code, at + 1);
            uint16_t many = operand_at(callee->code, at + 3);
            uint16_t landed = carried_slot(first, stored, aliased, from, base);
            for (uint16_t i = 1; i < many; i++) {
                if (carried_slot((uint16_t)(first + i), stored, aliased, from,
                                 base) != landed + i) {
                    return false;
                }
            }
        }
        at += kest_op_wide(op);
    }
    return true;
}

// Whether the operation at `at` writes what is on the stack into one slot,
// and which. A carried body whose answer goes there writes a constant it
// answers with there itself. See D1196.
static bool puts_one_slot(const KestIrBody *body, uint32_t at,
                          uint16_t *slot) {
    if (at >= body->op_count || body->ops[at].kind != KEST_IR_PUT ||
        body->ops[at].place == KEST_IR_NO_PLACE) {
        return false;
    }
    const KestIrPlace *place = &body->places[body->ops[at].place];
    if (place->kind != KEST_IR_PLACE_SLOT || place->slots != 1) {
        return false;
    }
    *slot = place->slot;
    return true;
}

// The function at `index`, written where it is called. The arguments are on
// the stack where the call would have handed them over, so they are stored
// into the slots the body calls its parameters, and the body runs above the
// caller's own slots.
static void carry(Lower *lower, uint16_t index, uint16_t argument_slots,
                  int32_t answer_to, uint32_t after, KestSpan span) {
    const KestChunk *callee = lower->module->functions[index];
    uint16_t base = lower->body->slot_count;
    // Written down on the body it was carried into, so that what the body is
    // said to need still counts the call the other backend makes.
    KestChunk *into = lower->chunk;
    if (into->carried_count == into->carried_room) {
        uint16_t room = into->carried_room == 0 ? 4 : into->carried_room * 2;
        uint16_t *grown = KEST_ARENA_ARRAY(lower->module->arena, uint16_t, room);
        if (grown == NULL) {
            lower->out_of_memory = true;
            return;
        }
        for (uint16_t i = 0; i < into->carried_count; i++) {
            grown[i] = into->carried[i];
        }
        into->carried = grown;
        into->carried_room = room;
    }
    into->carried[into->carried_count++] = index;
    uint16_t from[MOST_ALIASED];
    uint16_t aliased = 0;
    if (argument_slots == callee->param_slots &&
        !writes_a_parameter(callee)) {
        aliased = loaded_just_now(lower, argument_slots, from);
        if (aliased > 0 &&
            !runs_hold(callee, (uint16_t)(argument_slots - aliased), aliased,
                       from, base)) {
            aliased = 0;
        }
    }
    // The parameters loaded last are read where they are, and the ones
    // before them are stored the way any local is, so an element read
    // straight into them is one instruction.
    uint16_t stored = (uint16_t)(argument_slots - aliased);
    if (aliased > 0) {
        take_back(lower);
    }
    if (stored > 0) {
        emit_store(lower, base, stored, span);
    }
    uint32_t first = 0;
    if (callee->constant_count > 0) {
        first = kest_chunk_constant_run(lower->module, lower->chunk,
                                        callee->constants,
                                        callee->constant_classes,
                                        (uint16_t)callee->constant_count);
        if (lower->module->out_of_room) {
            lower->out_of_memory = true;
            return;
        }
    }
    // The returns a body ends with land where they are, so they are left out
    // and everything that went to one goes to where they were: a body that
    // answers on its last line ends with that return and then the one a body
    // with no answer would have.
    uint32_t end = callee->code_count;
    for (uint32_t at = 0; at < callee->code_count;
         at += kest_op_wide(callee->code[at])) {
        if (callee->code[at] != KEST_OP_RETURN) {
            end = callee->code_count;
        } else if (end == callee->code_count) {
            end = at;
        }
    }
    // Where the value is going, when the call's answer is one slot written
    // straight into a local: a `return` of a constant is then the constant
    // written there and a jump past the write, and not a push, a jump to the
    // end and the write. Which returns those are is decided before anything
    // is written, because it moves every instruction after them and the
    // body's jumps are distances. Nothing in a carried body jumps backwards.
    // See D1196.
    bool lands[MOST_CARRIED + 1] = {false};
    bool fused[MOST_CARRIED + 1] = {false};
    uint16_t moved_to[MOST_CARRIED + 1] = {0};
    for (uint32_t at = 0; at < end; at += kest_op_wide(callee->code[at])) {
        const Operand *operands = carried_operands(callee->code[at]);
        uint32_t wide = kest_op_wide(callee->code[at]);
        uint32_t read = at + 1;
        for (int o = 0; o < 3 && operands[o] != NO_OPERAND; o++) {
            uint32_t lands_at = at + wide + operand_at(callee->code, read);
            if (operands[o] == A_DISTANCE &&
                callee->code[at] != KEST_OP_RETURN && lands_at < end) {
                lands[lands_at] = true;
            }
            read += 2;
        }
    }
    uint32_t before = end;
    for (uint32_t at = 0; at < end; at += kest_op_wide(callee->code[at])) {
        fused[at] = answer_to >= 0 && callee->code[at] == KEST_OP_RETURN &&
                    operand_at(callee->code, at + 1) == 1 && before < at &&
                    callee->code[before] == KEST_OP_CONST && !lands[at];
        before = at;
    }
    uint32_t written = 0;
    for (uint32_t at = 0; at < end;) {
        uint32_t wide = kest_op_wide(callee->code[at]);
        moved_to[at] = (uint16_t)written;
        written += callee->code[at] == KEST_OP_CONST && at + wide < end &&
                           fused[at + wide]
                       ? 5
                       : wide;
        at += wide;
    }
    uint32_t moved_end = written;
    uint32_t which = 0;
    for (uint32_t at = 0; at < end; which++) {
        uint8_t op = callee->code[at];
        uint32_t wide = kest_op_wide(op);
        KestSpan where = {which < callee->origin_count
                              ? callee->origins[which]
                              : span.offset,
                          1};
        const Operand *operands = carried_operands(op);
        if (op == KEST_OP_CONST && at + wide < end && fused[at + wide]) {
            if (lower->chunk->fused_slots < 1) {
                lower->chunk->fused_slots = 1;
            }
            emit(lower, KEST_OP_STORE_K, where);
            emit_u16(lower, (uint16_t)answer_to, where);
            emit_u16(lower,
                     (uint16_t)(operand_at(callee->code, at + 1) + first),
                     where);
            at += wide;
            continue;
        }
        if (op == KEST_OP_RETURN) {
            emit(lower, KEST_OP_JUMP, where);
            if (fused[at]) {
                waits_for(lower, after, where);
            } else {
                emit_u16(lower, (uint16_t)(moved_end - moved_to[at] - wide),
                         where);
            }
            at += wide;
            continue;
        }
        emit(lower, op, where);
        uint32_t read = at + 1;
        for (int o = 0; o < 3 && operands[o] != NO_OPERAND; o++) {
            uint16_t value = operand_at(callee->code, read);
            read += 2;
            if (operands[o] == A_SLOT) {
                value = carried_slot(value, stored, aliased, from, base);
            } else if (operands[o] == A_CONSTANT) {
                value = (uint16_t)(value + first);
            } else if (operands[o] == A_DISTANCE) {
                // Into the returns the body ends with, none of which is
                // written: where they were is the end.
                uint32_t lands_at = at + wide + value;
                uint32_t moved = lands_at >= end ? moved_end
                                                 : moved_to[lands_at];
                value = (uint16_t)(moved - moved_to[at] - wide);
            }
            emit_u16(lower, value, where);
        }
        at += wide;
    }
    // Nothing written after the carried body may be taken back into it: its
    // end is where every one of its returns lands.
    lower->pointed_at = lower->chunk->code_count;
    uint16_t taken = (uint16_t)(callee->slot_count - aliased);
    if (taken > lower->carried_slots) {
        lower->carried_slots = taken;
    }
    // Where the body's stack starts is where the arguments were, and the
    // compiler's reckoning of this body already counted them there: what the
    // carried body can add to the deepest moment is how much deeper it goes
    // than the arguments it took off.
    uint16_t deeper = callee->stack_needed > argument_slots
                          ? (uint16_t)(callee->stack_needed - argument_slots)
                          : 0;
    if (deeper > lower->carried_stack) {
        lower->carried_stack = deeper;
    }
    // And by how much the reckoning may now be more than enough: what the
    // carried body adds, and the arguments too where they were never pushed
    // because the body reads them where the caller has them.
    uint16_t slack = (uint16_t)(deeper + aliased);
    if (slack > lower->carried_slack) {
        lower->carried_slack = slack;
    }
}

// Whether the last thing written was `load2`, or `load.n` of two, nothing
// points between, and the two slots it read. See D1155 and D1166.
static bool two_locals_before(const Lower *lower, uint16_t *first,
                              uint16_t *second) {
    if ((lower->last_op != KEST_OP_LOAD2 && lower->last_op != KEST_OP_LOADN) ||
        lower->last_at < lower->pointed_at ||
        lower->last_at + 5 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *first = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    *second = (uint16_t)(at[3] | ((uint16_t)at[4] << 8));
    // Two locals side by side are loaded as a run of two, which is the same
    // two values: what the second operand says there is how many, and the
    // second is the slot after the first. See D1166.
    if (lower->last_op == KEST_OP_LOADN) {
        if (*second != 2) {
            return false;
        }
        *second = (uint16_t)(*first + 1);
    }
    return true;
}

// Reading a place into what is on the stack, and writing what is on the stack
// into one.
static void read_place(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    switch (place->kind) {
    case KEST_IR_PLACE_SLOT:
        emit_load(lower, place->slot, place->slots, op->span);
        return;
    case KEST_IR_PLACE_RUN:
        emit(lower, KEST_OP_LOAD_SLOTS, op->span);
        emit_u16(lower, place->slot, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    case KEST_IR_PLACE_ELEM:
        // Whether the handle and the index are read or left where they are is
        // what says which of the two this is: a read that consumes them is an
        // index, and one that does not is a place a write is coming to.
        if (op->arg_count == 0) {
            emit(lower, KEST_OP_LOAD_ELEM, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            return;
        }
        {
            uint16_t holds = 0;
            uint16_t at = 0;
            // One slot an element only: a struct read this way is stored
            // next, and `index.to` takes that store into the read, which
            // is worth more than this. See D1155.
            if (fusing() && place->layout < lower->module->layout_count &&
                lower->module->layouts[place->layout].slots == 1 &&
                two_locals_before(lower, &holds, &at)) {
                // The two slots `load2` pushed are never on the stack now.
                if (lower->chunk->fused_slots < 2) {
                    lower->chunk->fused_slots = 2;
                }
                take_back(lower);
                emit(lower, KEST_OP_INDEX_LL, op->span);
                emit_u16(lower, holds, op->span);
                emit_u16(lower, at, op->span);
                emit_u16(lower, place->layout, op->span);
                return;
            }
        }
        emit(lower, KEST_OP_INDEX, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    case KEST_IR_PLACE_AT:
        if (fusing() && elem_addr_before(lower)) {
            take_back(lower);
            emit(lower, KEST_OP_ELEM_AT, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            return;
        }
        emit(lower, KEST_OP_LOAD_AT, op->span);
        emit_u16(lower, place->offset, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    case KEST_IR_PLACE_HELD:
        break;
    }
    fault(lower, op->span, "this reads a kind of place with no instruction");
}

static void write_place(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    switch (place->kind) {
    case KEST_IR_PLACE_SLOT:
        emit_store(lower, place->slot, place->slots, op->span);
        return;
    case KEST_IR_PLACE_RUN:
        emit(lower, KEST_OP_STORE_SLOTS, op->span);
        emit_u16(lower, place->slot, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    case KEST_IR_PLACE_ELEM: {
        // And the same the other way round: a run of slots pushed and then
        // packed into the element. The push and the pack are the same slots.
        // One slot as well as many: `state[at] = next` is a whole number
        // read out of a local and written, which is the same pair. See
        // D1185.
        uint16_t from = 0;
        if (fusing() && place->layout < lower->module->layout_count &&
            load_before(lower, &from) ==
                lower->module->layouts[place->layout].slots) {
            take_back(lower);
            uint16_t wide = lower->module->layouts[place->layout].slots;
            // And the run and the index, where both are locals nothing
            // points between: every `world[at] = one`. See D1167.
            uint16_t holds = 0;
            uint16_t at = 0;
            if (two_locals_before(lower, &holds, &at)) {
                take_back(lower);
                emit(lower, KEST_OP_ELEM_FROM_LL, op->span);
                emit_u16(lower, place->offset, op->span);
                emit_u16(lower, place->layout, op->span);
                emit_u16(lower, from, op->span);
                emit_u16(lower, holds, op->span);
                emit_u16(lower, at, op->span);
                if (wide + 2u > lower->chunk->fused_slots) {
                    lower->chunk->fused_slots = (uint16_t)(wide + 2u);
                }
                return;
            }
            emit(lower, KEST_OP_ELEM_FROM, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            emit_u16(lower, from, op->span);
            if (wide > lower->chunk->fused_slots) {
                lower->chunk->fused_slots = wide;
            }
            return;
        }
        emit(lower, KEST_OP_STORE_ELEM, op->span);
        emit_u16(lower, place->offset, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    }
    case KEST_IR_PLACE_AT:
    case KEST_IR_PLACE_HELD:
        break;
    }
    fault(lower, op->span, "this writes a kind of place with no instruction");
}

static void take_address(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    if (place->kind == KEST_IR_PLACE_ELEM) {
        emit(lower, KEST_OP_ELEM_ADDR, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    }
    if (place->kind == KEST_IR_PLACE_AT) {
        emit(lower, KEST_OP_OFFSET_ADDR, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    }
    fault(lower, op->span, "this takes the address of a place that has none");
}

// How wide the value an operation leaves is, which is what says whether a
// `drop` is one instruction or two.
static uint16_t leaves(const Lower *lower, KestIrRef ref) {
    return ref < lower->body->value_count ? lower->body->values[ref].slots : 0;
}

static void lower_op(Lower *lower, uint32_t index, const KestIrOp *op) {
    KestSpan span = op->span;
    (void)index;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
        emit_constant(lower, op->imm[0], op->imm[1], span);
        return;
    case KEST_IR_CONST_AT: {
        uint32_t first = kest_chunk_constant_run(
            lower->module, lower->chunk, lower->body->constants + op->imm[0],
            lower->body->constant_classes + op->imm[0], op->imm[2] * op->imm[1]);
        if (lower->module->out_of_room) {
            lower->out_of_memory = true;
            return;
        }
        emit(lower, KEST_OP_CONST_AT, span);
        emit_u16(lower, (uint16_t)first, span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;
    }
    case KEST_IR_TRUE:
        emit(lower, KEST_OP_TRUE, span);
        return;
    case KEST_IR_FALSE:
        emit(lower, KEST_OP_FALSE, span);
        return;
    case KEST_IR_LOAD:
        read_place(lower, op);
        return;
    case KEST_IR_PUT:
        write_place(lower, op);
        return;
    case KEST_IR_ADDR:
        take_address(lower, op);
        return;
    // A value out of its parts is already its parts, laid out where they were
    // made, and two ways of arriving at one value are one value. Neither is
    // an instruction here.
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
        return;
    case KEST_IR_PART:
        // The front of a run is the run with what is above it dropped, which
        // is cheaper than reaching into it and is the same answer.
        if (op->imm[0] == 0) {
            if (op->imm[2] > op->imm[1]) {
                emit(lower, KEST_OP_POPN, span);
                emit_u16(lower, (uint16_t)(op->imm[2] - op->imm[1]), span);
            }
            return;
        }
        emit(lower, KEST_OP_FIELD, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;
    case KEST_IR_TURN:
        emit(lower, KEST_OP_ROTATE, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_DROP: {
        uint16_t wide = leaves(lower, lower->body->args[op->first_arg]);
        if (wide == 1) {
            emit(lower, KEST_OP_POP, span);
            return;
        }
        emit(lower, KEST_OP_POPN, span);
        emit_u16(lower, wide, span);
        return;
    }

    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_DIV:
    case KEST_IR_MOD:
    case KEST_IR_NEG:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_FLIP:
    case KEST_IR_SHL:
    case KEST_IR_SHR: {
        uint8_t does = arithmetic(op->kind, op->type);
        if (does == 0) {
            fault(lower, span, "this is arithmetic with no instruction");
            return;
        }
        // A division by a constant, of a local or of what is on the stack.
        // What it refuses it still refuses: the machine asks of the constant
        // what it asked of the operand. See D1168.
        if (fusing() && (does == KEST_OP_MOD_I || does == KEST_OP_DIV_I)) {
            uint16_t slot = 0;
            uint16_t which = 0;
            if (local_and_constant_before(lower, &slot, &which)) {
                if (lower->chunk->fused_slots < 2) {
                    lower->chunk->fused_slots = 2;
                }
                take_back(lower);
                emit(lower, does == KEST_OP_MOD_I ? KEST_OP_MOD_I_K
                                                  : KEST_OP_DIV_I_K,
                     span);
                emit_u16(lower, slot, span);
                emit_u16(lower, which, span);
                return;
            }
            if (one_operand_before(lower, KEST_OP_CONST, &which)) {
                if (lower->chunk->fused_slots < 1) {
                    lower->chunk->fused_slots = 1;
                }
                take_back(lower);
                emit(lower, does == KEST_OP_MOD_I ? KEST_OP_MOD_I_C
                                                  : KEST_OP_DIV_I_C,
                     span);
                emit_u16(lower, which, span);
                return;
            }
        }
        emit(lower, does, span);
        return;
    }
    case KEST_IR_NARROW: {
        uint8_t does = KEST_OP_NARROW;
        if (fusing() && lower->last_at + 1 == lower->chunk->code_count &&
            lower->last_at >= lower->pointed_at) {
            does = fused_with_narrow(lower->last_op);
            if (does != KEST_OP_NARROW) {
                take_back(lower);
                // And the constant the arithmetic had for its right side, of
                // a local or of what is on the stack. See D1168.
                uint16_t slot = 0;
                uint16_t which = 0;
                if (local_and_constant_before(lower, &slot, &which)) {
                    if (lower->chunk->fused_slots < 2) {
                        lower->chunk->fused_slots = 2;
                    }
                    take_back(lower);
                    emit(lower, by_a_constant(does, true), span);
                    emit_u16(lower, op->imm[0], span);
                    emit_u16(lower, slot, span);
                    emit_u16(lower, which, span);
                    return;
                }
                if (one_operand_before(lower, KEST_OP_CONST, &which)) {
                    if (lower->chunk->fused_slots < 1) {
                        lower->chunk->fused_slots = 1;
                    }
                    take_back(lower);
                    emit(lower, by_a_constant(does, false), span);
                    emit_u16(lower, op->imm[0], span);
                    emit_u16(lower, which, span);
                    return;
                }
            }
        }
        emit(lower, does, span);
        emit_u16(lower, op->imm[0], span);
        return;
    }
    case KEST_IR_TO_FLOAT:
        emit(lower, kest_is_unsigned(op->type) ? KEST_OP_U2F : KEST_OP_I2F,
             span);
        return;
    case KEST_IR_TO_WHOLE:
        emit(lower, KEST_OP_F2I, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TO_F32:
        emit(lower, KEST_OP_TO_F32, span);
        return;
    case KEST_IR_BITS:
        // A slot holds an `f64` as the sixty-four bits it is, so an `f64` and
        // its bits are the same slot read two ways and there is nothing to
        // do. An `f32` is held widened, so it is narrowed and read as its
        // thirty-two bits, and the other way. See D1171.
        //
        // And the way to the bits is always an instruction, whatever the
        // width, because what is not a number is one such value there: the
        // sign and the payload the processor gave it are the processor's.
        // See D1238.
        if (op->imm[0] == 0) {
            emit(lower, KEST_OP_FLOAT_BITS, span);
            emit_u16(lower, kest_is_narrow(op->type) ? 32 : 64, span);
        } else if (kest_is_narrow(op->type)) {
            emit(lower, KEST_OP_BITS_F32, span);
        }
        return;

    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE:
        if (kest_is_a_run(op->type)) {
            emit(lower, op->kind == KEST_IR_EQ ? KEST_OP_EQ_VALUE
                                               : KEST_OP_NE_VALUE,
                 span);
            emit_u16(lower, op->imm[0], span);
            return;
        }
        {
            uint8_t does = compares(op->kind, op->type);
            if (does == 0) {
                fault(lower, span, "this compares with no instruction");
                return;
            }
            emit(lower, does, span);
        }
        return;
    case KEST_IR_NOT:
        emit(lower, KEST_OP_NOT, span);
        return;
    case KEST_IR_HASH:
        // A reference is one slot and goes through the walk that knows what a
        // value is made of, because only part of it may be hashed: the place
        // is the program's and the number above it is the process's. See
        // D1054.
        if (kest_is_a_run(op->type) ||
            (op->type != NULL && op->type->tag == KEST_T_REF)) {
            emit(lower, KEST_OP_HASH_VALUE, span);
            emit_u16(lower, op->imm[0], span);
            return;
        }
        emit(lower,
             op->type != NULL && op->type->tag == KEST_T_TEXT ? KEST_OP_HASH_T
             : kest_is_float(op->type)                             ? KEST_OP_HASH_F
                                                              : KEST_OP_HASH_I,
             span);
        return;

    case KEST_IR_TEXT_LEN:
        emit(lower, KEST_OP_TEXT_LEN, span);
        return;
    case KEST_IR_TEXT_AT:
        emit(lower, KEST_OP_TEXT_AT, span);
        return;
    case KEST_IR_TEXT_IN:
        emit(lower, KEST_OP_TEXT_IN, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_TEXT_SLICE:
        emit(lower, KEST_OP_TEXT_SLICE, span);
        return;
    case KEST_IR_TEXT_REST:
        emit(lower, KEST_OP_TEXT_REST, span);
        return;
    case KEST_IR_TEXT_MATCHES:
        emit(lower, KEST_OP_TEXT_MATCHES, span);
        return;
    case KEST_IR_TEXT_FIND:
        emit(lower, KEST_OP_TEXT_FIND, span);
        return;
    case KEST_IR_TEXT_OF: {
        uint8_t does = writes_text(op->type);
        emit(lower, does, span);
        if (does == KEST_OP_TEXT_FLAGS || does == KEST_OP_TEXT_VALUE) {
            emit_u16(lower, op->imm[0], span);
        }
        return;
    }
    case KEST_IR_TEXT_JOIN:
        emit(lower, KEST_OP_CONCAT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TEXT_FROM:
        emit(lower, KEST_OP_TEXT_FROM, span);
        return;

    case KEST_IR_ARRAY:
        emit(lower, KEST_OP_ARRAY, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_ARRAY_NEW:
        emit(lower, KEST_OP_MAKE_ARRAY, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_LEN:
        emit(lower, KEST_OP_LEN, span);
        return;
    case KEST_IR_APPEND:
        emit(lower, KEST_OP_PUSH, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_FIT:
        emit(lower, KEST_OP_FIT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    // A whole piece of text onto a run of bytes. No layout goes with it: the
    // element is a byte and the stride is one, which is the whole reason this
    // can be one move instead of a loop. See D1068.
    case KEST_IR_APPEND_TEXT:
        emit(lower, KEST_OP_PUSH_TEXT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_FIT_TEXT:
        emit(lower, KEST_OP_FIT_TEXT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_ROOM:
        emit(lower, KEST_OP_ROOM, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_POP_LAST:
        emit(lower, KEST_OP_POP_LAST, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TAKE:
        emit(lower, KEST_OP_TAKE, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_CLEAR:
        emit(lower, KEST_OP_CLEAR, span);
        return;

    case KEST_IR_STORE_NEW:
        emit(lower, KEST_OP_NEW_STORE, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_STORE_ADD:
        emit(lower, KEST_OP_ADD, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_GET:
        emit(lower, KEST_OP_GET, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_SET:
        emit(lower, KEST_OP_SET, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_REMOVE:
        emit(lower, KEST_OP_REMOVE, span);
        return;
    case KEST_IR_STORE_COUNT:
        emit(lower, KEST_OP_COUNT, span);
        return;
    case KEST_IR_STORE_REF:
        emit(lower, KEST_OP_STORE_REF, span);
        return;
    case KEST_IR_NEXT:
        emit(lower, kest_is_unsigned(op->type) ? KEST_OP_NEXT_LESS_U
                                               : KEST_OP_NEXT_LESS_I,
             span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        reaches_back(lower, op->target, 2, span);
        return;
    case KEST_IR_SEEK_FROM:
        emit(lower, KEST_OP_SEEK_FROM, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        waits_for(lower, op->target, span);
        return;
    case KEST_IR_SEEK_NEXT:
        emit(lower, KEST_OP_SEEK_NEXT, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        reaches_back(lower, op->target, 2, span);
        return;

    case KEST_IR_CALL:
        if (may_carry(lower, op->imm[0])) {
            uint16_t slot = 0;
            bool one = index + 2 < lower->body->op_count &&
                       puts_one_slot(lower->body, index + 1, &slot);
            carry(lower, op->imm[0], op->imm[1], one ? (int32_t)slot : -1,
                  index + 2, span);
            return;
        }
        emit(lower, KEST_OP_CALL, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_CALL_VALUE:
        emit(lower, KEST_OP_CALL_VALUE, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_CALL_HOST:
        emit(lower, KEST_OP_CALL_HOST, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;

    case KEST_IR_REGION_OPEN:
        emit(lower, KEST_OP_SCRATCH, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_REGION_CLOSE:
        emit(lower, KEST_OP_UNSCRATCH, span);
        emit_u16(lower, op->imm[0], span);
        return;

    case KEST_IR_GO:
        // Back to where a loop began, or on to somewhere not written yet.
        // Which of the two it is is which way it goes.
        if (op->target <= index) {
            emit(lower, KEST_OP_LOOP, span);
            reaches_back(lower, op->target, 2, span);
            return;
        }
        emit(lower, KEST_OP_JUMP, span);
        waits_for(lower, op->target, span);
        return;
    case KEST_IR_ASK: {
        uint8_t jump = asks(lower, op->imm[1] != 0);
        uint8_t against = fusing() ? weighed(jump, true) : 0;
        uint16_t slot = 0;
        uint16_t which = 0;
        if (against != 0 && local_and_constant_before(lower, &slot, &which)) {
            // The two values `load.k` pushed are never on the stack now, so
            // the compiler's reckoning may be two more than this body goes:
            // said as slack, the way D1012 says it, rather than taken off a
            // number whose deepest moment may be somewhere else.
            if (lower->chunk->fused_slots < 2) {
                lower->chunk->fused_slots = 2;
            }
            take_back(lower);
            emit(lower, against, span);
            emit_u16(lower, slot, span);
            emit_u16(lower, which, span);
        } else if (fusing() && weighed(jump, false) != 0 &&
                   one_operand_before(lower, KEST_OP_CONST, &which)) {
            // The constant is never on the stack now, so the reckoning may be
            // one more than this body goes; said as slack, as above.
            if (lower->chunk->fused_slots < 1) {
                lower->chunk->fused_slots = 1;
            }
            take_back(lower);
            uint16_t holds = 0;
            uint16_t at = 0;
            uint16_t layout = 0;
            uint8_t element = weighed_element(jump);
            if (element != 0 &&
                element_before(lower, &holds, &at, &layout)) {
                // Nor is the element: `index.ll` said as slack the two
                // slots it read in place, and this is fewer than that.
                take_back(lower);
                if (lower->chunk->fused_slots < 2) {
                    lower->chunk->fused_slots = 2;
                }
                emit(lower, element, span);
                emit_u16(lower, holds, span);
                emit_u16(lower, at, span);
                emit_u16(lower, layout, span);
                emit_u16(lower, which, span);
            } else {
                emit(lower, weighed(jump, false), span);
                emit_u16(lower, which, span);
            }
        } else {
            emit(lower, jump, span);
        }
        waits_for(lower, op->target, span);
        return;
    }
    case KEST_IR_GIVE:
        emit(lower, KEST_OP_RETURN, span);
        emit_u16(lower, op->imm[0], span);
        return;

    case KEST_IR_OP_COUNT:
        break;
    }
    kest_diags_disagree(lower->program->diags, span,
                        "`%s` is an operation with no instruction",
                        kest_ir_word((KestIrKind)op->kind));
}

// How wide each argument is, kept beside how wide they are together: a host
// filling a frame asks where the second one starts rather than working it out
// from the first one's fields.
static bool remember_takes(Lower *lower, const KestType *signature) {
    if (signature == NULL) {
        return true;
    }
    if (signature->result != NULL && signature->result->tag != KEST_T_VOID) {
        int32_t gives = kest_module_layout(lower->module, signature->result);
        if (gives < 0) {
            return false;
        }
        lower->chunk->gives = (uint16_t)gives;
    }
    if (signature->param_count == 0) {
        return true;
    }
    uint16_t *widths = KEST_ARENA_ARRAY(lower->module->arena, uint16_t,
                                        signature->param_count);
    if (widths == NULL) {
        return false;
    }
    for (uint32_t p = 0; p < signature->param_count; p++) {
        int32_t wide = kest_module_layout(lower->module, signature->params[p]);
        if (wide < 0) {
            return false;
        }
        widths[p] = (uint16_t)wide;
    }
    lower->chunk->takes = widths;
    lower->chunk->takes_count = (uint16_t)signature->param_count;
    return true;
}

static bool lower_body(Lower *lower, const KestIrBody *body, KestChunk *chunk) {
    lower->body = body;
    lower->chunk = chunk;
    lower->wait_count = 0;
    lower->earlier = 0;
    lower->last_op = 0;
    lower->last_at = 0;
    lower->pointed_at = 0;
    lower->carried_slots = 0;
    lower->carried_stack = 0;
    lower->carried_slack = 0;

    // In the bodies' own arena: what is worked out here is read while this one
    // body is written and by nothing after it.
    KestArena *arena = lower->arena;
    uint32_t count = body->op_count == 0 ? 1 : body->op_count;
    lower->at = KEST_ARENA_ARRAY(arena, uint32_t, count);
    lower->landed_on = KEST_ARENA_ARRAY(arena, bool, count);
    lower->waiting = KEST_ARENA_ARRAY(arena, uint32_t, count);
    lower->leads_to = KEST_ARENA_ARRAY(arena, uint32_t, count);
    if (lower->at == NULL || lower->landed_on == NULL ||
        lower->waiting == NULL || lower->leads_to == NULL) {
        lower->out_of_memory = true;
        return false;
    }

    // Where anything branches to, read before anything is written: two
    // instructions are only made into one where nothing lands between them.
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target < body->op_count) {
            lower->landed_on[op->target] = true;
        }
        // And past the write a carried body's answer goes to, which is where
        // one of its returns can land. See D1196.
        uint16_t slot = 0;
        if (op->kind == KEST_IR_CALL && i + 2 < body->op_count &&
            may_carry(lower, op->imm[0]) &&
            puts_one_slot(body, i + 1, &slot)) {
            lower->landed_on[i + 2] = true;
        }
    }

    chunk->param_slots = body->param_slots;
    if (!remember_takes(lower, body->signature)) {
        lower->out_of_memory = true;
        return false;
    }

    for (uint32_t i = 0; i < body->op_count; i++) {
        lower->at[i] = chunk->code_count;
        if (lower->landed_on[i]) {
            lower->pointed_at = chunk->code_count;
        }
        lower_op(lower, i, &body->ops[i]);
        if (lower->out_of_memory) {
            return false;
        }
    }
    fill_in_branches(lower);

    // What the bodies carried here need of the frame: their slots above this
    // body's own, and their stack said as slack beside the reckoning, because
    // where a carried body runs is below the deepest moment of this one or
    // above it and nothing here says which. See D1156.
    chunk->slot_count = (uint16_t)(body->slot_count + lower->carried_slots);
    chunk->stack_needed = (uint16_t)(body->stack_needed + lower->carried_stack);
    if (lower->carried_slack > chunk->fused_slots) {
        chunk->fused_slots = lower->carried_slack;
    }
    chunk->folded = body->folded;
    chunk->folded_slots = body->folded_slots;
    return !lower->out_of_memory;
}

KestLower *kest_lower_new(KestProgram *program, KestModule *module,
                          KestArena *arena) {
    KestLower *lower = KEST_ARENA_NEW(arena, KestLower);
    if (lower == NULL) {
        return NULL;
    }
    lower->program = program;
    lower->module = module;
    lower->arena = arena;
    return lower;
}

bool kest_lower_body(void *reading, const KestIrBody *body) {
    Lower *lower = reading;
    if (lower->next >= lower->module->count) {
        return false;
    }
    kest_diags_in(lower->program->diags, body->source);
    // A body that is not what a body is would be written as instructions that
    // mean something else, so it is refused rather than written.
    const char *wrong = kest_ir_verify(body);
    if (wrong != NULL) {
        kest_diags_disagree(lower->program->diags, body->declared, "%s", wrong);
        return false;
    }
    KestChunk *into = lower->module->functions[lower->next++];
    // What the optimizer took out before this read the body, which the
    // compiler's reckoning of how deep the stack goes still counts. It sits
    // beside what this file's own fusions save and is held the same way. See
    // D1012 and D1025.
    if (body->took_slots > into->fused_slots) {
        into->fused_slots = body->took_slots;
    }
    // What the body called its slots, carried through so a stopped machine can
    // say `hungry` rather than `slot 4`. The IR has kept them since D962
    // because the escape pass wanted them; this is the second reader. See
    // D991.
    for (uint32_t i = 0; i < body->name_count; i++) {
        const KestIrName *one = &body->names[i];
        kest_chunk_names(lower->module, into, one->name, one->slot,
                         one->slots,
                         one->type == NULL ? (uint8_t)KEST_L_WORD
                                           : kest_scalar_of(one->type),
                         one->by_address);
    }
    return lower_body(lower, body, into);
}
