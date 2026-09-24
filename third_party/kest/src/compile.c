#include "compile.h"

#include "check.h"
#include "ir.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// What there is a most of, in one place, because a number a program can run
// into belongs where somebody can read it and not only where it is enforced.
// Every one of them is a message with the number in it, never a wrap or a
// quiet truncation, and `docs/language.md` says the same numbers.
// How many names a program may ask the host for. An extern is named in the
// instruction that calls it, in two bytes, so the sixty-five-thousand-and-
// thirty-seventh would be called as whichever one that number wraps to: the
// host's, with the program's arguments, and nothing said. See D326.
#define MAX_EXTERNS 65536
_Static_assert(MAX_EXTERNS <= (uint32_t)UINT16_MAX + 1,
               "an extern is named in an instruction in two bytes");

#define MAX_LOCALS 256
#define MAX_LOOPS 16
#define MAX_BREAKS 32
#define MAX_DEFERS 32
// How many working-memory blocks may be open at once in one body. Nesting is
// lexical, so this is a number a program can be refused for where it is
// written rather than one it can meet while running.
#define MAX_REGIONS 8
// A jump and a loop carry how far as two bytes, so this is how much code there
// can be between one and where it lands.
#define MAX_REACH UINT16_MAX

typedef struct {
    const char *name;
    // What it holds. A place says what is in it, so that a walk over the body
    // can answer that without going back to the file.
    const KestType *type;
    uint16_t slot;
    // A struct value occupies a run of slots, so a name is a place and a
    // width rather than a single index.
    uint16_t size;
    uint32_t depth;
    // The slot holds where the value is rather than the value. A walk binds
    // its name this way when the body only ever reads fields of it, so
    // reading one field of a wide struct does not copy the rest. See D052.
    bool is_address;
    // Where the name this body wrote down for it is, so that a thing known
    // after the name was written -- whether the slot holds an address -- can
    // be written into it rather than lost. See D1081.
    uint32_t name_at;
    // The name is a value the chunk holds rather than a place in the frame: a
    // `let` worked out where it was written whose name the body never assigns
    // to. The frame neither builds it nor keeps it, and every reading of the
    // name is a constant. NULL for every other name. See D887.
    const KestValue *folded;
    const KestType *holds;
    // Where it was written, which is what a body says about a name when it is
    // asked which one a place belongs to.
    KestSpan span;
} Local;

typedef struct {
    uint32_t start;
    // How many deferred statements were outstanding when the loop opened, so
    // a `break` knows which of them it is leaving.
    uint16_t deferred;
    // And how many working-memory blocks, for the same reason.
    uint16_t regions;
    uint32_t breaks[MAX_BREAKS];
    uint32_t break_count;
    // `continue` jumps forward to a pad placed after the body, because in a
    // `for` the step comes after the body and jumping to the top would skip
    // it.
    uint32_t continues[MAX_BREAKS];
    uint32_t continue_count;
} Loop;

typedef struct {
    KestProgram *program;
    KestModule *module;
    // What is being written, and where in it. A body is one concrete function:
    // one declaration, or one copy of a generic.
    KestIrProgram *ir;
    KestIrBody *body;
    // The function whose body is being compiled, which is told when it is
    // finished whether it keeps runs. See D1188.
    KestChunk *compiling;
    // The values this walk has made and not yet read, innermost last. It is
    // the shape the tree has: what an expression makes is read by the
    // expression it is written inside.
    KestIrRef *values;
    uint32_t value_count;
    uint32_t value_capacity;
    // Where what is filled in goes when there is no body to fill it into.
    KestIrOp nowhere;
    // The files, so a name that is not a local can be looked up rather than
    // becoming a load from somewhere.
    const KestUnits *units;

    Local locals[MAX_LOCALS];
    uint16_t local_count;
    uint16_t next_slot;
    uint16_t slot_high_water;
    uint32_t depth;

    Loop loops[MAX_LOOPS];
    uint32_t loop_count;
    uint32_t unit;

    // What has been deferred and not yet run, innermost last. A block runs
    // what it added when it ends; a `return` runs everything; a `break` runs
    // what the loop it is leaving added. See D061.
    const KestExpr *deferred[MAX_DEFERS];
    uint16_t defer_count;

    // The working-memory blocks this walk is inside, innermost last, each
    // with the slot holding which one it is. A `return`, a `break` or a
    // `continue` out of one closes it on the way; every other way out is the
    // machine's, because a body that is refused has no code left to run. See
    // D966.
    uint16_t regions[MAX_REGIONS];
    uint16_t region_count;

    // Compiling an expression always leaves one value behind and compiling a
    // statement leaves none, so following the emit sites gives the exact
    // depth rather than a bound. Since D809 that is held rather than said:
    // `hold_width` at every expression, and `hold_empty` at every statement.
    uint16_t stack_depth;
    uint16_t stack_high_water;
    // Whether the count was taken below nothing since the last statement.
    // Taking more off than was put on used to stop at nought and carry on,
    // which is a count that is wrong and looks right again by the end of the
    // statement — the one way either of the two above could be kept by a
    // compiler that had lost track. See D810.
    bool lost_count;

    // A reported problem does not stop the walk: D008 wants one run to report
    // the whole file. Only running out of memory stops it, because after that
    // nothing further is true, and that is what this says.
    bool out_of_memory;

    // Whether a place in an array is wanted as what it is made of rather than
    // as an address. A statement that writes through a place and evaluates
    // something in between asks for this; everything else takes the address,
    // which is a slot fewer and is safe where nothing runs in between. See
    // D931.
    bool place_apart;

    // How many walks written out once a turn this is inside. A name such a
    // walk counts with is a value the body holds, so a question about it has
    // an answer where it is asked. See D1231.
    uint16_t unrolling;
} Compiler;

static void refuse(Compiler *compiler, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_addv(compiler->program->diags, KEST_SEVERITY_ERROR, code, span,
                    format, args);
    va_end(args);
}

// What the checker allowed and this cannot emit. Reaching one of these means
// the two halves of this compiler disagree about what a program is, which is
// this project's mistake and not the program's — so it says so, in the words
// the emitted-code proof uses for the same kind of news. A diagnostic rather
// than an assert, because a program that trips it should be told rather than
// stopped.
static void fault(Compiler *compiler, KestSpan span, const char *what) {
    kest_diags_disagree(compiler->program->diags, span, "%s", what);
}

static void stack_push(Compiler *compiler, uint16_t count) {
    compiler->stack_depth += count;
    if (compiler->stack_depth > compiler->stack_high_water) {
        compiler->stack_high_water = compiler->stack_depth;
    }
}

static void stack_pop(Compiler *compiler, uint16_t count) {
    // Stopping at nought rather than going under it, and remembering that it
    // had to: what is under nothing is not a depth, and a count that clamps
    // is one that comes back to the right answer by the end of the statement
    // whatever it did in the middle. `hold_empty` reads this. See D810.
    if (compiler->stack_depth < count) {
        compiler->lost_count = true;
    }
    compiler->stack_depth =
        compiler->stack_depth >= count ? compiler->stack_depth - count : 0;
}

// What this walk writes is a body: values, places, and operations that read
// values and leave one behind them. Nothing here writes an instruction. What a
// machine is and what a program means used to be the one file, which is why a
// second machine would have had to ask the tree again; now both read the body
// this writes. See `ir.h`.

// Where an operation is now. The list moves when it grows, so nothing holds a
// pointer into it across a write. A body that ran out of memory has nowhere to
// write, and what is filled in afterwards goes into a scrap nothing reads: the
// body is abandoned either way, and a walk that has to ask whether it is still
// writing at every place it fills something in is a walk with the question in
// forty places instead of one.
static KestIrOp *ir_at(Compiler *compiler, uint32_t at) {
    if (compiler->body == NULL || at >= compiler->body->op_count) {
        return &compiler->nowhere;
    }
    return &compiler->body->ops[at];
}

static void ir_leaves(Compiler *compiler, KestIrRef ref) {
    if (compiler->value_count == compiler->value_capacity) {
        uint32_t grown =
            compiler->value_capacity == 0 ? 16 : compiler->value_capacity * 2;
        KestIrRef *moved =
            KEST_ARENA_ARRAY(compiler->ir->arena, KestIrRef, grown);
        if (moved == NULL) {
            compiler->out_of_memory = true;
            return;
        }
        if (compiler->value_count > 0) {
            memcpy(moved, compiler->values,
                   sizeof(KestIrRef) * compiler->value_count);
        }
        compiler->values = moved;
        compiler->value_capacity = grown;
    }
    compiler->values[compiler->value_count++] = ref;
}

// One operation. It reads the top `takes` values and leaves one of `gives`
// that is `slots` wide, or leaves nothing when `slots` is nought. Answers
// where it is, so that what it carries can be filled in and a branch can be
// told where it lands.
static uint32_t ir_emit(Compiler *compiler, KestIrKind kind,
                        const KestType *type, uint16_t takes,
                        const KestType *gives, uint16_t slots, KestSpan span) {
    if (takes > compiler->value_count) {
        // Reading more than the body has made. The walk and this are out of
        // step, which is this project's mistake rather than the program's.
        fault(compiler, span, "an operation reads more than the body has made");
        takes = (uint16_t)compiler->value_count;
    }
    compiler->value_count -= takes;
    uint32_t at = kest_ir_op(compiler->ir, compiler->body, kind, type,
                             compiler->values + compiler->value_count, takes,
                             span);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
        return at;
    }
    if (slots > 0) {
        KestIrRef ref =
            kest_ir_value_add(compiler->ir, compiler->body, gives, slots);
        if (compiler->ir->out_of_memory) {
            compiler->out_of_memory = true;
            return at;
        }
        ir_at(compiler, at)->dest = ref;
        ir_leaves(compiler, ref);
    }
    return at;
}

static void ir_carries(Compiler *compiler, uint32_t at, uint16_t first,
                       uint16_t second, uint16_t third) {
    KestIrOp *op = ir_at(compiler, at);
    op->imm[0] = first;
    op->imm[1] = second;
    op->imm[2] = third;
}

// A branch, with where it lands left until the walk gets there. Answers where
// the branch is, which is what `ir_lands` is given.
static uint32_t ir_go(Compiler *compiler, KestSpan span) {
    return ir_emit(compiler, KEST_IR_GO, NULL, 0, NULL, 0, span);
}

// A branch taken when the answer on top is the one named. It reads that
// answer, which is why which answer is a number the operation carries rather
// than two operations.
static uint32_t ir_ask(Compiler *compiler, bool when_true, KestSpan span) {
    uint32_t at = ir_emit(compiler, KEST_IR_ASK, NULL, 1, NULL, 0, span);
    ir_carries(compiler, at, 0, when_true ? 1 : 0, 0);
    return at;
}

// A branch that reads the top slot of a value wider than one: an `if let`
// asks the tag an optional carries and what it holds stays where it is. What
// comes back is the branch, and the value it left is on top.
static uint32_t ir_ask_leaving(Compiler *compiler, bool when_true,
                               const KestType *held, uint16_t slots,
                               KestSpan span) {
    if (slots == 0) {
        return ir_ask(compiler, when_true, span);
    }
    uint32_t at = ir_emit(compiler, KEST_IR_ASK, held, 1, held, slots, span);
    ir_carries(compiler, at, 0, when_true ? 1 : 0, slots);
    return at;
}

static void ir_lands(Compiler *compiler, uint32_t branch) {
    kest_ir_lands_here(compiler->body, branch);
}

// A branch back to where a loop began.
static void ir_go_back(Compiler *compiler, uint32_t to, KestSpan span) {
    uint32_t at = ir_go(compiler, span);
    ir_at(compiler, at)->target = to;
}

// Where the branches that leave a condition are, so that whatever the
// condition is in can send all of them to the same place. There is one per
// `&&` and `||` in it and one at the end, and no most: a condition is written
// as long as somebody writes it, and a number here would be a number that
// changes what is written without refusing anything, which is the one kind
// nobody can see.
typedef struct {
    uint32_t *at;
    uint32_t count;
    uint32_t capacity;
} Exits;

static void take_exit(Compiler *compiler, Exits *exits, uint32_t at) {
    if (exits->count == exits->capacity) {
        uint32_t grown = exits->capacity == 0 ? 8 : exits->capacity * 2;
        uint32_t *moved =
            KEST_ARENA_ARRAY(compiler->ir->arena, uint32_t, grown);
        if (moved == NULL) {
            compiler->out_of_memory = true;
            return;
        }
        if (exits->count > 0) {
            memcpy(moved, exits->at, sizeof(uint32_t) * exits->count);
        }
        exits->at = moved;
        exits->capacity = grown;
    }
    exits->at[exits->count++] = at;
}

static Exits one_exit(Compiler *compiler, uint32_t at) {
    Exits exits = {NULL, 0, 0};
    take_exit(compiler, &exits, at);
    return exits;
}

// Telling a list of branches where they land. There are two lists — the ones
// a condition left and the ones a `continue` left — and one walk over them.
static void land_each(Compiler *compiler, const uint32_t *at, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        ir_lands(compiler, at[i]);
    }
}

static void land_exits(Compiler *compiler, const Exits *exits) {
    land_each(compiler, exits->at, exits->count);
}

static const char *span_text(Compiler *compiler, KestSpan span) {
    return kest_span_text(compiler->program->source, span);
}

static Local *find_local(Compiler *compiler, KestSpan span) {
    const char *name = span_text(compiler, span);
    for (uint16_t i = compiler->local_count; i > 0; i--) {
        Local *local = &compiler->locals[i - 1];
        if (kest_word_same(local->name, name, span.length)) {
            return local;
        }
    }
    return NULL;
}

// Locals are a stack, so a slot is the position and a scope is dropped by
// rewinding the count. The high water mark is the frame size.
static uint16_t type_slots(const KestType *type) {
    return type == NULL || type->slots == 0 ? 1 : type->slots;
}

// A slot with no name, for what `for` needs to keep between iterations.
static uint16_t reserve_slot(Compiler *compiler, uint16_t size) {
    uint16_t slot = compiler->next_slot;
    compiler->next_slot += size;
    if (compiler->next_slot > compiler->slot_high_water) {
        compiler->slot_high_water = compiler->next_slot;
    }
    return slot;
}

// A name for slots that are already somewhere, which is what a match arm
// gives what the case it answered was carrying.
static void bind_local(Compiler *compiler, KestSpan span, uint16_t slot,
                       uint16_t size) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return;
    }
    Local *local = &compiler->locals[compiler->local_count++];
    memset(local, 0, sizeof *local);
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    if (local->name == NULL) {
        // A local is found by its name, so one without a name is a slot
        // nothing can reach and a pointer everything that looks for it would
        // read. See D510.
        compiler->out_of_memory = true;
        return;
    }
    local->slot = slot;
    local->size = size;
    local->depth = compiler->depth;
}

// A name for a value the chunk holds. It takes no slot, so the frame is the
// size it would be if the name had not been written -- which is the point:
// nothing builds it and nothing keeps it. See D887.
static void hold_local(Compiler *compiler, KestSpan span, const KestType *type,
                       const KestValue *values, uint16_t slots) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return;
    }
    Local *local = &compiler->locals[compiler->local_count++];
    memset(local, 0, sizeof *local);
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    if (local->name == NULL) {
        compiler->out_of_memory = true;
        return;
    }
    local->size = slots;
    local->depth = compiler->depth;
    local->folded = values;
    local->holds = type;
}

// A name the body declared, written down beside the places that reach it, so
// that a walk over a body can say which name a slot belongs to without going
// back to the file. Nothing lowering does reads them; what reads them is
// anything asking a body about itself.
static uint32_t remember_name(Compiler *compiler, const Local *local) {
    KestIrName name = {0};
    name.name = local->name;
    name.type = local->type != NULL ? local->type : local->holds;
    name.slot = local->slot;
    name.slots = local->size;
    name.by_address = local->is_address;
    name.span = local->span;
    uint32_t at = kest_ir_name_add(compiler->ir, compiler->body, &name);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
    }
    return at;
}

static uint16_t declare_local(Compiler *compiler, KestSpan span,
                              const KestType *type) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return 0;
    }
    // Slots are reused between scopes and between functions, so everything a
    // name holds is written here rather than left over from the last one.
    Local *local = &compiler->locals[compiler->local_count++];
    memset(local, 0, sizeof *local);
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    if (local->name == NULL) {
        compiler->out_of_memory = true;
        return 0;
    }
    local->slot = compiler->next_slot;
    local->type = type;
    local->size = type_slots(type);
    local->depth = compiler->depth;

    compiler->next_slot += local->size;
    if (compiler->next_slot > compiler->slot_high_water) {
        compiler->slot_high_water = compiler->next_slot;
    }
    local->span = span;
    local->name_at = remember_name(compiler, local);
    return local->slot;
}

// How many slots a value of this type occupies on the stack. Nothing is zero
// except a call that returns nothing.
static uint16_t value_slots(const KestType *type) {
    if (type == NULL || type->tag == KEST_T_VOID) {
        return 0;
    }
    return type->slots == 0 ? 1 : type->slots;
}

// Where the module keeps this type's memory layout, which is what an array
// element is and what a write through an address writes.
static uint16_t layout_of(Compiler *compiler, const KestType *type) {
    int32_t index = kest_module_layout(compiler->module, type);
    if (index < 0) {
        compiler->out_of_memory = true;
        return 0;
    }
    return (uint16_t)index;
}

// The type a slot the walk made for itself holds: a count, an index, how many
// there are. Every place says what it holds, and one the program did not write
// still holds something.
static const KestType *whole_type(Compiler *compiler) {
    return kest_find_type(compiler->program, "i64", 3);
}

static const KestType *truth_type(Compiler *compiler) {
    return kest_find_type(compiler->program, "bool", 4);
}

// A run of slots in the frame. A name is one, and so is a field of one,
// because a struct is laid out flat.
static uint32_t slot_place(Compiler *compiler, uint16_t slot, uint16_t size,
                           const KestType *type, KestSpan span) {
    KestIrPlace place = {0};
    place.kind = KEST_IR_PLACE_SLOT;
    place.type = type;
    place.slots = size;
    place.slot = slot;
    place.span = span;
    uint32_t at = kest_ir_place_add(compiler->ir, compiler->body, &place);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
    }
    return at;
}

static void ir_place_of(Compiler *compiler, uint32_t at, uint32_t place) {
    ir_at(compiler, at)->place = place;
}

// A value this walk has made and not yet read, counted from the top.
static KestIrRef ir_top(Compiler *compiler, uint32_t back) {
    if (back >= compiler->value_count) {
        return KEST_IR_NONE;
    }
    return compiler->values[compiler->value_count - 1 - back];
}

// One of a fixed run in the frame, at an index worked out while running.
static uint32_t run_place(Compiler *compiler, uint16_t slot, uint16_t stride,
                          uint16_t count, const KestType *element,
                          KestSpan span) {
    KestIrPlace place = {0};
    place.kind = KEST_IR_PLACE_RUN;
    place.type = element;
    place.slots = stride;
    place.slot = slot;
    place.stride = stride;
    place.count = count;
    // A run in the frame is slots rather than bytes, so there is no layout to
    // name: what a layout says is how a host lays a value out in memory.
    place.layout = 0;
    place.index = ir_top(compiler, 0);
    place.base = KEST_IR_NONE;
    place.span = span;
    uint32_t at = kest_ir_place_add(compiler->ir, compiler->body, &place);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
    }
    return at;
}

// Inside memory the host laid out: an address, how far into it, and what is
// there. `index` is a value when one of a run is wanted and nothing when the
// address is already the one.
static uint32_t at_place(Compiler *compiler, KestIrRef base, KestIrRef index,
                         uint16_t offset, uint16_t stride, uint16_t count,
                         uint16_t layout, const KestType *type, KestSpan span) {
    KestIrPlace place = {0};
    place.kind = KEST_IR_PLACE_AT;
    place.type = type;
    place.slots = value_slots(type);
    place.layout = layout;
    place.offset = offset;
    place.stride = stride;
    place.count = count;
    place.base = base;
    place.index = index;
    place.span = span;
    uint32_t at = kest_ir_place_add(compiler->ir, compiler->body, &place);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
    }
    return at;
}

// One of an array or of a store, as the handle and which one. What reaches it
// is worked out where it is read or written and not before. See D931.
static uint32_t elem_place(Compiler *compiler, KestIrRef base, KestIrRef index,
                           uint16_t offset, const KestType *element,
                           KestSpan span) {
    KestIrPlace place = {0};
    place.kind = KEST_IR_PLACE_ELEM;
    place.type = element;
    place.slots = value_slots(element);
    place.layout = layout_of(compiler, element);
    place.offset = offset;
    place.base = base;
    place.index = index;
    place.span = span;
    uint32_t at = kest_ir_place_add(compiler->ir, compiler->body, &place);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
    }
    return at;
}

// Reading what is at an address, which is one value made of what was there.
static void load_at(Compiler *compiler, uint16_t offset, const KestType *type,
                    KestSpan span) {
    uint32_t held = at_place(compiler, ir_top(compiler, 0), KEST_IR_NONE,
                             offset, 0, 0, layout_of(compiler, type), type,
                             span);
    uint32_t at = ir_emit(compiler, KEST_IR_LOAD, type, 1, type,
                          value_slots(type), span);
    ir_place_of(compiler, at, held);
}

// The address of one of a run at an address: a step of that many bytes, with
// how many there are so that stepping past the end is a refusal rather than a
// read.
static void offset_addr(Compiler *compiler, uint16_t stride, uint16_t count,
                        const KestType *element, KestSpan span) {
    // A step to one of a run is a stride and how many there are; nothing is
    // read here, so no layout is asked for.
    uint32_t held = at_place(compiler, ir_top(compiler, 1), ir_top(compiler, 0),
                             0, stride, count, 0, element, span);
    uint32_t at = ir_emit(compiler, KEST_IR_ADDR, element, 2, NULL, 1, span);
    ir_place_of(compiler, at, held);
}

// Reading a run of slots into a value, and writing a value into one. What the
// machine does about a run of one and a run of many, and about two runs that
// sit next to each other, is the backend's: here a read is a read.
static void load_slots(Compiler *compiler, uint16_t slot, uint16_t size,
                       const KestType *type, KestSpan origin) {
    uint32_t place = slot_place(compiler, slot, size, type, origin);
    uint32_t at = ir_emit(compiler, KEST_IR_LOAD, type, 0, type, size, origin);
    ir_place_of(compiler, at, place);
}

static void store_slots(Compiler *compiler, uint16_t slot, uint16_t size,
                        const KestType *type, KestSpan origin) {
    uint32_t place = slot_place(compiler, slot, size, type, origin);
    uint32_t at = ir_emit(compiler, KEST_IR_PUT, type, 1, NULL, 0, origin);
    ir_place_of(compiler, at, place);
}

static const KestMember *find_member(const KestType *type, const char *name,
                                     size_t length) {
    if (type == NULL || type->tag != KEST_T_STRUCT) {
        return NULL;
    }
    for (uint32_t i = 0; i < type->member_count; i++) {
        if (kest_word_same(type->members[i].name, name, length)) {
            return &type->members[i];
        }
    }
    return NULL;
}

// A place is a run of slots that a name reaches by arithmetic. `v` is one and
// so is `v.a.b`, because a struct is laid out flat, so reading a field costs
// an addition rather than a load.
// An index written down, or -1 when it was not. How many of them is known, so
// one of them at a written place is a slot like a field is.
static int64_t written_index(Compiler *compiler, const KestExpr *expr) {
    if (expr->kind != KEST_EXPR_INT) {
        return -1;
    }
    const char *digits = span_text(compiler, expr->span);
    int64_t at = 0;
    for (uint32_t i = 0; i < expr->span.length; i++) {
        at = at * 10 + (digits[i] - '0');
        if (at > 65535) {
            return -1;
        }
    }
    return at;
}

static bool resolve_place(Compiler *compiler, const KestExpr *expr,
                          uint16_t *slot, uint16_t *size) {
    // One of that many, at a place written down, is where the run is plus how
    // far in: the same arithmetic a field of a struct is.
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        const KestType *run = expr->index.object->type;
        int64_t at = written_index(compiler, expr->index.index);
        uint16_t base = 0;
        uint16_t run_size = 0;
        if (at >= 0 && (uint64_t)at < run->count &&
            resolve_place(compiler, expr->index.object, &base, &run_size)) {
            uint16_t stride = value_slots(run->element);
            *slot = (uint16_t)(base + at * stride);
            *size = stride;
            return true;
        }
        return false;
    }
    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL || local->is_address || local->folded != NULL) {
            return false;
        }
        *slot = local->slot;
        *size = local->size;
        return true;
    }
    if (expr->kind != KEST_EXPR_FIELD) {
        return false;
    }

    uint16_t base = 0;
    uint16_t base_size = 0;
    if (!resolve_place(compiler, expr->field.object, &base, &base_size)) {
        return false;
    }
    const KestMember *member =
        find_member(expr->field.object->type,
                    span_text(compiler, expr->field.name),
                    expr->field.name.length);
    if (member == NULL) {
        return false;
    }
    *slot = base + member->offset;
    *size = value_slots(member->type);
    return true;
}



// Which operation an operator that compares is. What instruction that becomes
// is the backend's question, because the answer depends on what is being
// compared rather than on what was written.
static KestIrKind compare_kind(KestTokenKind op) {
    switch (op) {
    case KEST_TOK_LT:
        return KEST_IR_LT;
    case KEST_TOK_LTEQ:
        return KEST_IR_LE;
    case KEST_TOK_GT:
        return KEST_IR_GT;
    case KEST_TOK_GTEQ:
        return KEST_IR_GE;
    case KEST_TOK_EQEQ:
        return KEST_IR_EQ;
    case KEST_TOK_BANGEQ:
        return KEST_IR_NE;
    default:
        return KEST_IR_OP_COUNT;
    }
}

static bool dividing_can_leave(const KestType *type) {
    return type != NULL && type->tag == KEST_T_INT && type->is_signed;
}

// Whether every value of one type is a value of another, which is what says a
// cast between them has nothing to cut. A `bool` is nought or one and fits in
// all of them. An unsigned value is nought upwards, so it fits an unsigned type
// of at least its width and a signed one wider than it; a signed value can be
// under nought, which no unsigned type holds. See D867.
static bool every_value_fits(const KestType *from, const KestType *to) {
    if (from == NULL || to == NULL || to->tag != KEST_T_INT) {
        return false;
    }
    if (from->tag == KEST_T_BOOL) {
        return true;
    }
    if (from->tag != KEST_T_INT) {
        return false;
    }
    if (!from->is_signed) {
        return to->is_signed ? from->width < to->width
                             : from->width <= to->width;
    }
    return to->is_signed && from->width <= to->width;
}

// A result wider than its type is not the answer the type describes, so it is
// cut back. Sixty-four bits is the slot, so nothing is cut there. Whether the
// arithmetic before it takes it into itself is the backend's question and is
// asked there. See D868.
static void ir_narrow(Compiler *compiler, const KestType *type,
                      KestSpan span) {
    if (type == NULL || type->width == 64 ||
        (type->tag != KEST_T_INT && type->tag != KEST_T_FLAGS)) {
        return;
    }
    uint32_t at = ir_emit(compiler, KEST_IR_NARROW, type, 1, type, 1, span);
    ir_carries(compiler, at, kest_scalar_of(type), 0, 0);
}

// Copies the content of a string, resolving escapes. The span is the
// characters between the quotes, or one run of them when the string was
// written with holes in it.
// What a string literal holds, and what a number literal is worth. Both are
// the lexer's to know, because both are about how a thing is spelled.
static const char *literal_text(Compiler *compiler, KestSpan span,
                               size_t *length) {
    return kest_literal_text(compiler->program->arena, compiler->program->source,
                             span, length);
}

static double parse_real(Compiler *compiler, KestSpan span) {
    return kest_literal_real(compiler->program->source, span);
}

static void emit_constant(Compiler *compiler, KestValue value,
                          KestConstClass class, const KestType *type,
                          KestSpan span);
static void close_regions(Compiler *compiler, uint16_t from, bool leaving,
                          KestSpan span);
static void compile_expr(Compiler *compiler, const KestExpr *expr);
static void compile_block(Compiler *compiler, const KestBlock *block);
static void run_deferred(Compiler *compiler, uint16_t from, KestSpan span);
static void bind_local(Compiler *compiler, KestSpan span, uint16_t slot,
                       uint16_t size);
static uint16_t reserve_slot(Compiler *compiler, uint16_t size);

// A constant is written into every use of it rather than loaded, which is
// what makes it a constant rather than a variable nobody assigns to.
// A function named where a value is wanted is which function it is. The
// checker settled which one, and its symbol is what it was compiled under, so
// nothing is chosen twice.
static bool compile_function_value(Compiler *compiler, const KestExpr *expr) {
    if (expr->type == NULL || expr->type->tag != KEST_T_FN ||
        expr->type->symbol == NULL) {
        return false;
    }
    int32_t index = kest_module_find(compiler->module, expr->type->symbol);
    if (index < 0) {
        fault(compiler, expr->span, "this names a function that was never "
                                    "compiled");
        return true;
    }
    // Written down as one that a call through a value may enter. What such a
    // call costs is the worst of these and nothing else, because this is the
    // only place a function becomes a value. See D814.
    compiler->module->functions[index]->as_value = true;
    KestValue which = {0};
    which.integer = index;
    emit_constant(compiler, which, KEST_CONST_INT, expr->type, expr->span);
    return true;
}

// A value is laid out flat, so a constant that is a struct is a push a scalar,
// each with what its bits mean beside it: the machine never reads that and the
// disassembler does.
static void value_classes(const KestType *type, uint8_t *classes,
                          uint32_t *at) {
    if (type != NULL && type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            value_classes(type->members[i].type, classes, at);
        }
        return;
    }
    if (type != NULL && type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            value_classes(type->element, classes, at);
        }
        return;
    }
    // A piece of text is two slots: what it is made of, and how many bytes
    // that is. See D964.
    if (type != NULL && type->tag == KEST_T_TEXT) {
        classes[(*at)++] = KEST_CONST_TEXT;
        classes[(*at)++] = KEST_CONST_INT;
        return;
    }
    classes[(*at)++] = type != NULL && type->tag == KEST_T_FLOAT
                           ? KEST_CONST_FLOAT
                           : KEST_CONST_INT;
}

// The run put in the chunk beside the code, with what each of its slots means
// beside it, and where it starts.
static bool constant_run(Compiler *compiler, const KestType *type,
                         const KestValue *values, uint16_t slots,
                         uint32_t *first) {
    // Worked out into this rather than into a block of the arena, which is
    // what D676 did for the values these describe and did not do for the
    // description: `kest_chunk_constant_run` copies what it is handed into the
    // chunk, so nothing here outlives the call, and a block taken for it is a
    // block nobody reads and nothing gives back. Sixteen for the same reason
    // the values take sixteen, which is that a run wider than that is a table
    // rather than a value. See D754.
    // Nought to start with, because that is what a block of the arena arrives
    // as and what `value_classes` leaves the slots it does not reach: a
    // description read past what was written into it is a constant whose kind
    // is whatever the stack held, and two builds of one program would mark
    // differently.
    uint8_t held[16] = {0};
    uint8_t *classes = held;
    if (slots > (uint16_t)(sizeof(held) / sizeof(held[0]))) {
        classes = KEST_ARENA_ARRAY(compiler->ir->arena, uint8_t, slots);
        if (classes == NULL) {
            compiler->out_of_memory = true;
            return false;
        }
    }
    uint32_t at = 0;
    value_classes(type, classes, &at);
    *first = kest_ir_constants_add(compiler->ir, compiler->body, values,
                                   classes, slots);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
        return false;
    }
    return true;
}

// What was worked out, counted where it is put into the chunk. Three places
// give a chunk a value it did not have to build -- an expression written where
// it stands, one of a run read at a position, and a name the frame does not
// hold -- and a count kept in three places is three answers the day one of
// them moves. See D887.
static void counted_fold(Compiler *compiler, uint16_t slots) {
    if (compiler->body == NULL) {
        return;
    }
    compiler->body->folded++;
    compiler->body->folded_slots += slots;
}

static void emit_value_slots(Compiler *compiler, const KestType *type,
                             const KestValue *values, uint16_t slots,
                             KestSpan span) {
    // One of them is one push. A run of them is one instruction and one copy,
    // because a table of sixty-four numbers should not cost sixty-four
    // instructions every time it is read.
    uint32_t first = 0;
    if (!constant_run(compiler, type, values, slots, &first)) {
        return;
    }
    stack_push(compiler, slots);
    uint32_t at = ir_emit(compiler, KEST_IR_CONST, type, 0, type, slots, span);
    ir_carries(compiler, at, (uint16_t)first, slots, 0);
}

// A piece of text the body holds, which is two values: what it is made of and
// how many bytes that is. See D964.
static void emit_text_constant(Compiler *compiler, const char *bytes,
                               size_t length, const KestType *type,
                               KestSpan span) {
    KestValue held[2] = {{0}, {0}};
    held[0].text = bytes == NULL ? "" : bytes;
    held[1].integer = bytes == NULL ? 0 : (int64_t)length;
    emit_value_slots(compiler, type, held, 2, span);
}

// One value the body holds. The same operation as a run of them, because one
// is a run of one and a backend that writes a shorter instruction for it is
// the one that knows that.
static void emit_constant(Compiler *compiler, KestValue value,
                          KestConstClass class, const KestType *type,
                          KestSpan span) {
    uint8_t held = (uint8_t)class;
    uint32_t first = kest_ir_constants_add(compiler->ir, compiler->body,
                                           &value, &held, 1);
    if (compiler->ir->out_of_memory) {
        compiler->out_of_memory = true;
        return;
    }
    stack_push(compiler, 1);
    uint32_t at = ir_emit(compiler, KEST_IR_CONST, type, 0, type, 1, span);
    ir_carries(compiler, at, (uint16_t)first, 1, 0);
}

// A value of the right width and nothing in it, for a place a value belongs
// and the program was already refused for what is there. Every stage after the
// one that refused counts slots, so a hole left where a value goes is the
// compiler telling a reader about its own arithmetic rather than about their
// program. See D888.
static void emit_nothing_wide(Compiler *compiler, const KestType *type,
                              KestSpan span) {
    uint16_t slots = value_slots(type);
    if (slots == 0) {
        slots = 1;
    }
    KestValue nothing[16] = {{0}};
    if (slots > (uint16_t)(sizeof(nothing) / sizeof(nothing[0]))) {
        stack_push(compiler, slots);
        return;
    }
    emit_value_slots(compiler, type, nothing, slots, span);
}

// Anything that is a constant when it is written down: a name, a field of one,
// an element of one. Nothing is copied into slots to be read back out.
static bool compile_folded(Compiler *compiler, const KestExpr *expr) {
    uint16_t slots = value_slots(expr->type);
    if (expr->type == NULL || slots == 0) {
        return false;
    }
    // A name that is a constant was worked out where it was declared, so this
    // reads what came of that rather than working it out again. Without it a
    // wide constant read forty times was folded forty times, which the count
    // of folds says out loud. See D675.
    if (expr->kind == KEST_EXPR_NAME || expr->kind == KEST_EXPR_FIELD) {
        const KestSymbol *named = kest_lookup_global(
            compiler->program, span_text(compiler, expr->span),
            expr->span.length);
        if (named != NULL && named->is_const && named->folded != NULL &&
            named->folded_slots == slots) {
            emit_value_slots(compiler, expr->type, named->folded, slots,
                             expr->span);
            return true;
        }
    }
    // Worked out into this rather than into a block of the arena. Most of what
    // reaches here is not a constant at all — ninety-one askings to nineteen
    // answers in one example — and a block taken for each of those is a block
    // nobody reads and nothing gives back. What comes of a fold is written
    // into the chunk, so nothing needs to outlive this. See D676.
    KestValue held[16];
    KestValue *values = held;
    if (slots > (uint16_t)(sizeof(held) / sizeof(held[0]))) {
        values = KEST_ARENA_ARRAY(compiler->ir->arena, KestValue, slots);
        if (values == NULL) {
            return false;
        }
    }
    const char *why = NULL;
    if (kest_fold_const(compiler->program, expr, values, slots, &why,
                        NULL) != slots) {
        return false;
    }
    counted_fold(compiler, slots);
    emit_value_slots(compiler, expr->type, values, slots, expr->span);
    return true;
}

static void compile_constant(Compiler *compiler, const KestExpr *expr) {
    const char *name = span_text(compiler, expr->span);

    if (compile_function_value(compiler, expr)) {
        return;
    }

    const KestSymbol *symbol =
        kest_lookup_global(compiler->program, name, expr->span.length);
    // Worked out where it was declared, so every use of it reads what is
    // already there: a constant read five times was folded five times before
    // D674, and what was wrong with one was said as many times as it was read.
    if (symbol != NULL && symbol->is_const && symbol->folded != NULL) {
        emit_value_slots(compiler, symbol->type, symbol->folded,
                         (uint16_t)symbol->folded_slots, expr->span);
        return;
    }
    if (symbol != NULL && symbol->is_const && symbol->would_not_fold) {
        // Said once, at the declaration. A use of a constant that could not be
        // worked out is not a second thing wrong with the program -- but it is
        // still a value where a value is wanted, and leaving nothing behind
        // left the stage after this one disagreeing with itself and saying so
        // in the words it keeps for a fault of its own, about a program
        // somebody had merely written wrongly. What goes there is nothing of
        // the right width: the program is refused, so nothing runs it, and
        // what it is worth is that everything after it counts. See D888.
        emit_nothing_wide(compiler, symbol->type, expr->span);
        return;
    }
    if (symbol != NULL && symbol->is_const) {
        uint16_t slots = value_slots(symbol->type);
        KestValue *values =
            KEST_ARENA_ARRAY(compiler->ir->arena, KestValue,
                             slots == 0 ? 1 : slots);
        const char *why = NULL;
        if (values == NULL) {
            compiler->out_of_memory = true;
            return;
        }
        // Worked out in the file it was written in. What a literal is worth is
        // read out of the source at the span it stands at, and a constant from
        // another module has spans into that module's file — read against this
        // one they name whatever bytes happen to be at those offsets, which is
        // a number nobody wrote. See D665.
        const KestSource *reading = compiler->program->source;
        if (symbol->source != NULL) {
            compiler->program->source = symbol->source;
        }
        bool never = false;
        uint32_t filled = kest_fold_const(compiler->program, symbol->value,
                                          values, slots, &why, &never);
        compiler->program->source = reading;
        if (filled != slots) {
            // Two refusals rather than one. A constant made of itself or
            // divided by nought is a mistake in what was written, and one that
            // asks for a choice or a call is a rule of this language — the
            // first is fixed where it is and the second is written another way
            // altogether. A reader is told either way; a tool sorting refusals
            // could not tell them apart while both were `K0504`. See D673.
            if (never) {
                refuse(compiler, expr->span, "K0510",
                       "`%.*s` is made while running, so it is not a constant",
                       (int)expr->span.length, name);
            } else {
                refuse(compiler, expr->span, "K0504",
                       "`%.*s` is not worked out where it is written",
                       (int)expr->span.length, name);
            }
            kest_diags_suggest(compiler->program->diags, "%s",
                               why != NULL
                                   ? why
                                   : "a constant is a number, a truth or a "
                                     "piece of text, and arithmetic on those "
                                     "and on other constants");
            return;
        }
        emit_value_slots(compiler, symbol->type, values, slots, expr->span);
        return;
    }

    fault(compiler, expr->span,
          "this is a name that is not a local, a constant or a function");
}

// Whether a name is used for nothing but reading fields of it. A walk binds
// its name to where the element is when that holds, so a body that wants one
// field of a wide struct does not copy the rest of it.
//
// It is the whole body or nothing: one use of the name on its own — passed,
// returned, compared, assigned to — and the name has to be a value.
static bool reads_only_fields(Compiler *compiler, const KestBlock *block,
                              const char *name, size_t length,
                              bool fields_are_fine);

// Both sides here are runs of the source with no nought at either end, which
// is why this is written out rather than asking `kest_word_same`: that one
// takes a name this compiler holds against a word a file wrote, and the walk
// this serves is comparing one word a file wrote against another. See D773.
static bool name_is(Compiler *compiler, const KestExpr *expr, const char *name,
                    size_t length) {
    return expr != NULL && expr->kind == KEST_EXPR_NAME &&
           expr->span.length == length &&
           memcmp(span_text(compiler, expr->span), name, length) == 0;
}

static bool expr_reads_only_fields(Compiler *compiler, const KestExpr *expr,
                                   const char *name, size_t length,
                                   bool fields_are_fine) {
    if (expr == NULL) {
        return true;
    }
    if (name_is(compiler, expr, name, length)) {
        return false;
    }
    switch (expr->kind) {
    case KEST_EXPR_FIELD:
        // The one shape that is allowed: the name, and a field of it. Asked
        // the other way, with `fields_are_fine` off, no shape is allowed and
        // the answer is whether the name is mentioned at all.
        if (name_is(compiler, expr->field.object, name, length)) {
            return fields_are_fine;
        }
        return expr_reads_only_fields(compiler, expr->field.object, name, length,
                                      fields_are_fine);
    case KEST_EXPR_UNARY:
        return expr_reads_only_fields(compiler, expr->unary.operand, name, length,
                                      fields_are_fine);
    case KEST_EXPR_BINARY:
        return expr_reads_only_fields(compiler, expr->binary.left, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, expr->binary.right, name, length,
                                      fields_are_fine);
    case KEST_EXPR_CALL:
        if (!expr_reads_only_fields(compiler, expr->call.callee, name, length,
                                      fields_are_fine)) {
            return false;
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            if (!expr_reads_only_fields(compiler, expr->call.args[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_INDEX:
        return expr_reads_only_fields(compiler, expr->index.object, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, expr->index.index, name, length,
                                      fields_are_fine);
    case KEST_EXPR_ARRAY:
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (!expr_reads_only_fields(compiler, expr->array.items[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_TEXT:
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (!expr_reads_only_fields(compiler, expr->text.parts[i].value, name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_MATCH: {
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            if (!expr_reads_only_fields(compiler, expr->choose->subjects[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        for (uint32_t a = 0; a < expr->choose->arm_count; a++) {
            const KestArm *arm = &expr->choose->arms[a];
            if (!expr_reads_only_fields(compiler, arm->value, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &arm->body, name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    }
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        return expr_reads_only_fields(compiler, branch->condition, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->then_value, name, length,
                                      fields_are_fine) &&
               reads_only_fields(compiler, &branch->then_body, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->otherwise, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->else_value, name, length,
                                      fields_are_fine) &&
               reads_only_fields(compiler, &branch->else_body, name, length,
                                      fields_are_fine);
    }
    default:
        return true;
    }
}

static bool reads_only_fields(Compiler *compiler, const KestBlock *block,
                              const char *name, size_t length,
                              bool fields_are_fine) {
    for (uint32_t i = 0; i < block->count; i++) {
        const KestStmt *stmt = block->items[i];
        switch (stmt->kind) {
        case KEST_STMT_LET:
            // A name declared over the top of it makes what follows about
            // something else, which this does not try to tell apart.
            if (stmt->let.name.length == length &&
                memcmp(span_text(compiler, stmt->let.name), name, length) ==
                    0) {
                return false;
            }
            if (!expr_reads_only_fields(compiler, stmt->let.value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_ASSIGN:
            // Writing a field of it is writing, not reading.
            if (stmt->assign.target != NULL &&
                stmt->assign.target->kind == KEST_EXPR_FIELD &&
                name_is(compiler, stmt->assign.target->field.object, name,
                        length)) {
                return false;
            }
            if (!expr_reads_only_fields(compiler, stmt->assign.target, name, length,
                                      fields_are_fine) ||
                !expr_reads_only_fields(compiler, stmt->assign.value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_EXPR:
            if (!expr_reads_only_fields(compiler, stmt->value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_WHILE:
            if (!expr_reads_only_fields(compiler, stmt->loop.condition, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &stmt->loop.body, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_FOR:
            if (!expr_reads_only_fields(compiler, stmt->each->sequence, name, length,
                                      fields_are_fine) ||
                !expr_reads_only_fields(compiler, stmt->each->until, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &stmt->each->body, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_RETURN:
            if (!expr_reads_only_fields(compiler, stmt->result, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_BLOCK:
        // A block of working memory is a block: what is written inside one is
        // written. It fell into a `default` here, so a body that named the
        // thing being walked inside a `scratch { }` was read as naming
        // nothing and the loop bound its element by address. Written out with
        // no `default` now, the way every list here that has to be complete
        // is, so a statement added to the language stops the build rather
        // than going quietly missing from two walks. See D1075.
        case KEST_STMT_SCRATCH:
            if (!reads_only_fields(compiler, &stmt->block, name, length,
                                 fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_DEFER:
            if (!expr_reads_only_fields(compiler, stmt->value, name, length,
                                        fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_BREAK:
        case KEST_STMT_CONTINUE:
            break;
        }
    }
    return true;
}

// Whether anything in here could write into an array. There is no global
// mutable state in this language (D002's rule for the implementation is the
// language's rule too), so a write reaches an array only through a name in
// scope or through a call that was handed one. Both are refused rather than
// told apart, because telling two handles apart is a question this compiler
// does not ask.
static bool writes_no_arrays(Compiler *compiler, const KestBlock *block);

// Whether a value of this type can reach an array, a store or a reference --
// anywhere inside it, not only at the top. A call handed one of those can write
// what the loop above is walking, and a call handed a struct that holds one can
// do exactly the same thing: `Holder { items: [Item] }` is a handle wearing a
// struct. The walk used to stop at the top, so a body that passed the holder
// was read as touching nothing and the loop bound its element by address. The
// element then changed under the body's copy, which is the one thing a value
// is promised not to do. See D932.
static bool holds_a_handle(const KestType *type, uint32_t depth) {
    // A shape can name itself through a reference, so the walk is bounded.
    // Anything that deep holds one of these long before it gets here.
    if (type == NULL || depth > 8) {
        return type != NULL;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_REF:
        return true;
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (holds_a_handle(type->members[i].type, depth + 1)) {
                return true;
            }
        }
        return false;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (holds_a_handle(type->cases[c].payload[p], depth + 1)) {
                    return true;
                }
            }
        }
        return false;
    case KEST_T_OPTIONAL:
    case KEST_T_FIXED:
        return holds_a_handle(type->element, depth + 1);
    default:
        return false;
    }
}

static bool expr_writes_no_arrays(Compiler *compiler, const KestExpr *expr) {
    if (expr == NULL) {
        return true;
    }
    switch (expr->kind) {
    case KEST_EXPR_CALL:
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            if (holds_a_handle(expr->call.args[i]->type, 0)) {
                return false;
            }
            if (!expr_writes_no_arrays(compiler, expr->call.args[i])) {
                return false;
            }
        }
        return expr_writes_no_arrays(compiler, expr->call.callee);
    case KEST_EXPR_UNARY:
        return expr_writes_no_arrays(compiler, expr->unary.operand);
    case KEST_EXPR_BINARY:
        return expr_writes_no_arrays(compiler, expr->binary.left) &&
               expr_writes_no_arrays(compiler, expr->binary.right);
    case KEST_EXPR_FIELD:
        return expr_writes_no_arrays(compiler, expr->field.object);
    case KEST_EXPR_INDEX:
        return expr_writes_no_arrays(compiler, expr->index.object) &&
               expr_writes_no_arrays(compiler, expr->index.index);
    case KEST_EXPR_ARRAY:
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->array.items[i])) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_TEXT:
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->text.parts[i].value)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_MATCH:
        for (uint32_t i = 0; i < expr->choose->subject_count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->choose->subjects[i])) {
                return false;
            }
        }
        for (uint32_t a = 0; a < expr->choose->arm_count; a++) {
            if (!expr_writes_no_arrays(compiler, expr->choose->arms[a].value) ||
                !writes_no_arrays(compiler, &expr->choose->arms[a].body)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        return expr_writes_no_arrays(compiler, branch->condition) &&
               expr_writes_no_arrays(compiler, branch->then_value) &&
               writes_no_arrays(compiler, &branch->then_body) &&
               expr_writes_no_arrays(compiler, branch->otherwise) &&
               expr_writes_no_arrays(compiler, branch->else_value) &&
               writes_no_arrays(compiler, &branch->else_body);
    }
    default:
        return true;
    }
}

static bool writes_no_arrays(Compiler *compiler, const KestBlock *block) {
    for (uint32_t i = 0; i < block->count; i++) {
        const KestStmt *stmt = block->items[i];
        switch (stmt->kind) {
        case KEST_STMT_LET:
            if (!expr_writes_no_arrays(compiler, stmt->let.value)) {
                return false;
            }
            break;
        case KEST_STMT_ASSIGN:
            // Only a plain name can be written, because anything with an
            // index in it is a write into memory something else may be
            // walking.
            if (stmt->assign.target == NULL ||
                stmt->assign.target->kind != KEST_EXPR_NAME) {
                return false;
            }
            if (!expr_writes_no_arrays(compiler, stmt->assign.value)) {
                return false;
            }
            break;
        case KEST_STMT_EXPR:
            if (!expr_writes_no_arrays(compiler, stmt->value)) {
                return false;
            }
            break;
        case KEST_STMT_WHILE:
            if (!expr_writes_no_arrays(compiler, stmt->loop.condition) ||
                !writes_no_arrays(compiler, &stmt->loop.body)) {
                return false;
            }
            break;
        case KEST_STMT_FOR:
            if (!expr_writes_no_arrays(compiler, stmt->each->sequence) ||
                !expr_writes_no_arrays(compiler, stmt->each->until) ||
                !writes_no_arrays(compiler, &stmt->each->body)) {
                return false;
            }
            break;
        case KEST_STMT_RETURN:
            if (!expr_writes_no_arrays(compiler, stmt->result)) {
                return false;
            }
            break;
        case KEST_STMT_BLOCK:
        case KEST_STMT_SCRATCH:
            if (!writes_no_arrays(compiler, &stmt->block)) {
                return false;
            }
            break;
        case KEST_STMT_DEFER:
            if (!expr_writes_no_arrays(compiler, stmt->value)) {
                return false;
            }
            break;
        case KEST_STMT_BREAK:
        case KEST_STMT_CONTINUE:
            break;
        }
    }
    return true;
}

// Whether an address can be worked out for this, asked before anything is
// emitted. `compile_address` emits as it goes, so a caller that has somewhere
// else to fall back to has to know beforehand rather than find out halfway.
static bool can_address(Compiler *compiler, const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        int64_t at = written_index(compiler, expr->index.index);
        return at >= 0 &&
               (uint64_t)at < expr->index.object->type->count &&
               can_address(compiler, expr->index.object);
    }
    if (expr->kind == KEST_EXPR_FIELD) {
        return can_address(compiler, expr->field.object) &&
               find_member(expr->field.object->type,
                           span_text(compiler, expr->field.name),
                           expr->field.name.length) != NULL;
    }
    // A name that holds where something is rather than the thing itself.
    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        return local != NULL && local->is_address;
    }
    if (expr->kind != KEST_EXPR_INDEX) {
        return false;
    }
    const KestType *sequence = expr->index.object->type;
    return sequence != NULL && sequence->tag == KEST_T_ARRAY;
}

static bool compile_address(Compiler *compiler, const KestExpr *expr,
                            uint16_t *offset) {
    if (expr->kind == KEST_EXPR_FIELD) {
        if (!compile_address(compiler, expr->field.object, offset)) {
            return false;
        }
        const KestMember *member =
            find_member(expr->field.object->type,
                        span_text(compiler, expr->field.name),
                        expr->field.name.length);
        if (member == NULL) {
            return false;
        }
        // Bytes, because an address points into memory laid out the way the
        // host lays it out, not into slots.
        *offset = (uint16_t)(*offset + member->byte_offset);
        return true;
    }

    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL || !local->is_address) {
            return false;
        }
        stack_push(compiler, 1);
        load_slots(compiler, local->slot, 1, NULL, expr->span);
        *offset = 0;
        return true;
    }

    // One of that many, at a place written down, is a byte offset into the
    // run rather than a step worked out while running.
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        const KestType *run = expr->index.object->type;
        int64_t at = written_index(compiler, expr->index.index);
        if (at < 0 || (uint64_t)at >= run->count ||
            !compile_address(compiler, expr->index.object, offset)) {
            return false;
        }
        *offset = (uint16_t)(*offset + at * run->element->byte_size);
        return true;
    }

    if (expr->kind != KEST_EXPR_INDEX) {
        return false;
    }

    const KestType *sequence = expr->index.object->type;
    if (sequence == NULL || sequence->tag != KEST_T_ARRAY) {
        return false;
    }

    // What is being indexed and where is read as a value, even while the place
    // this is working out is being held apart: `held[0].cools[1] = 7` is a
    // place inside a value inside a place, and the value in the middle has to
    // be one. Held apart all the way down, the element read left the array and
    // the index on the stack instead of the handle, and the statement ended
    // two slots deep -- which the build that checks itself called a fault in
    // the compiler, because that is what it is. See D1091.
    bool apart = compiler->place_apart;
    compiler->place_apart = false;
    compile_expr(compiler, expr->index.object);
    compile_expr(compiler, expr->index.index);
    compiler->place_apart = apart;
    if (compiler->place_apart) {
        // Left as the array and the index. What reads or writes it works the
        // address out at that moment, so anything the program does in between
        // -- growing that very array, most of all -- cannot leave this holding
        // a block nothing will read again. See D931.
        *offset = 0;
        return true;
    }
    stack_pop(compiler, 1);
    uint32_t where = elem_place(compiler, ir_top(compiler, 1),
                                ir_top(compiler, 0), 0, sequence->element,
                                expr->span);
    uint32_t at = ir_emit(compiler, KEST_IR_ADDR, sequence->element, 2, NULL, 1,
                          expr->span);
    ir_place_of(compiler, at, where);
    *offset = 0;
    return true;
}

// What the folder asks about a name while a walk is written out once a turn:
// the innermost name the body declared by that spelling, and the value it
// holds, if it holds one. See D1231.
static bool held_by_body(void *context, const char *name, uint32_t length,
                         const KestValue **value, uint32_t *slots) {
    Compiler *compiler = context;
    for (uint16_t i = compiler->local_count; i > 0; i--) {
        const Local *local = &compiler->locals[i - 1];
        if (kest_word_same(local->name, name, length)) {
            *value = local->folded;
            *slots = local->size;
            return true;
        }
    }
    return false;
}

// A fold asked for what it answers rather than as a value a body is given:
// the counts the costs are read from are left as they were. See D1231.
static bool fold_quietly(Compiler *compiler, const KestExpr *expr,
                         KestValue *out) {
    KestProgram *program = compiler->program;
    uint32_t folds = program->folds;
    uint32_t nothing = program->asked_for_nothing;
    bool never = program->fold_never;
    const char *why = NULL;
    bool worked = kest_fold_const(program, expr, out, 1, &why, NULL) == 1;
    program->folds = folds;
    program->asked_for_nothing = nothing;
    program->fold_never = never;
    return worked;
}

// Whether a truth is settled where it is written, inside a walk written out
// once a turn and nowhere else: `way == 0` in the turn where `way` is two.
// `a && b` and `a || b` are settled by the half that settles them, and by the
// other half where the first does not. See D1231.
static bool known_truth(Compiler *compiler, const KestExpr *expr,
                        bool *truth) {
    if (compiler->unrolling == 0 || expr == NULL || expr->type == NULL ||
        expr->type->tag != KEST_T_BOOL) {
        return false;
    }
    if (expr->kind == KEST_EXPR_UNARY && expr->unary.op == KEST_TOK_BANG) {
        bool turned;
        if (!known_truth(compiler, expr->unary.operand, &turned)) {
            return false;
        }
        *truth = !turned;
        return true;
    }
    if (expr->kind == KEST_EXPR_BINARY &&
        (expr->binary.op == KEST_TOK_AMPAMP ||
         expr->binary.op == KEST_TOK_PIPEPIPE)) {
        bool either = expr->binary.op == KEST_TOK_PIPEPIPE;
        bool left;
        if (!known_truth(compiler, expr->binary.left, &left)) {
            return false;
        }
        if (left == either) {
            *truth = left;
            return true;
        }
        return known_truth(compiler, expr->binary.right, truth);
    }
    KestValue value = {0};
    if (!fold_quietly(compiler, expr, &value)) {
        return false;
    }
    *truth = value.integer != 0;
    return true;
}

// A condition compiled for where it goes rather than for what it is. The
// answer to `a || b` in the place a jump reads is never built: each half
// jumps, so the `true` that was pushed and the jump over it are not there at
// all, and the comparison at the end of each half goes into its own jump.
//
// `when_true` says which way the jumps this leaves are taken. What falls
// through is the other answer.
static void branch_when(Compiler *compiler, const KestExpr *expr,
                        bool when_true, Exits *out) {
    // Settled where it is written: a jump that is always taken, or none.
    bool known;
    if (known_truth(compiler, expr, &known)) {
        if (known == when_true) {
            take_exit(compiler, out, ir_go(compiler, expr->span));
        }
        return;
    }
    if (expr != NULL && expr->kind == KEST_EXPR_UNARY &&
        expr->unary.op == KEST_TOK_BANG) {
        // Turning the question round is not an instruction here: it is asking
        // the other one.
        branch_when(compiler, expr->unary.operand, !when_true, out);
        return;
    }
    if (expr != NULL && expr->kind == KEST_EXPR_BINARY &&
        (expr->binary.op == KEST_TOK_PIPEPIPE ||
         expr->binary.op == KEST_TOK_AMPAMP)) {
        bool either = expr->binary.op == KEST_TOK_PIPEPIPE;
        // A first half settled where it is written and not settling the
        // whole leaves the whole to the second.
        bool first;
        if (known_truth(compiler, expr->binary.left, &first)) {
            branch_when(compiler, expr->binary.right, when_true, out);
            return;
        }
        if (either == when_true) {
            // `a || b` leaving when true, or `a && b` leaving when false:
            // either half decides it on its own, so both leave the same way.
            branch_when(compiler, expr->binary.left, when_true, out);
            branch_when(compiler, expr->binary.right, when_true, out);
            return;
        }
        // The other way round: the left side can only settle it by going the
        // other way, and that lands where the whole thing falls through.
        Exits settled = {0};
        branch_when(compiler, expr->binary.left, !when_true, &settled);
        branch_when(compiler, expr->binary.right, when_true, out);
        land_exits(compiler, &settled);
        return;
    }

    compile_expr(compiler, expr);
    stack_pop(compiler, 1);
    take_exit(compiler, out, ir_ask(compiler, when_true, expr->span));
}

// The condition of an `if` or a `while`, which is the only place a boolean is
// wanted for where it goes rather than for what it is. An `if let` is not one
// of these: what it leaves on the stack is the value it bound.
static Exits compile_condition(Compiler *compiler, const KestExpr *expr,
                               bool binding) {
    Exits out = {NULL, 0, 0};
    if (!binding) {
        branch_when(compiler, expr, false, &out);
        return out;
    }
    // What an `if let` leaves on the stack is the value it bound, so the
    // branch that reads the tag is the one way out and the binding is under
    // it.
    compile_expr(compiler, expr);
    stack_pop(compiler, 1);
    const KestType *optional = expr->type;
    uint16_t held = optional == NULL ? 0 : (uint16_t)(value_slots(optional) - 1);
    take_exit(compiler, &out,
              ir_ask_leaving(compiler, false,
                             optional == NULL ? NULL : optional->element, held,
                             expr->span));
    return out;
}

static void compile_binary(Compiler *compiler, const KestExpr *expr) {
    KestTokenKind op = expr->binary.op;
    KestSpan span = expr->span;

    // Short circuiting is control flow, not an operator: the right side is
    // only reached when the left did not already decide the answer.
    if (op == KEST_TOK_AMPAMP || op == KEST_TOK_PIPEPIPE) {
        // A first half settled where it is written that did not settle the
        // whole -- `compile_expr` answers the whole where it did -- leaves the
        // answer to the second. See D1231.
        bool first;
        if (known_truth(compiler, expr->binary.left, &first)) {
            compile_expr(compiler, expr->binary.right);
            return;
        }
        compile_expr(compiler, expr->binary.left);
        if (op == KEST_TOK_PIPEPIPE) {
            ir_emit(compiler, KEST_IR_NOT, expr->type, 1, expr->type, 1, span);
        }
        stack_pop(compiler, 1);
        uint32_t skip = ir_ask(compiler, false, span);
        compile_expr(compiler, expr->binary.right);
        uint32_t done = ir_go(compiler, span);
        ir_lands(compiler, skip);
        // The branch arrives here having discarded the left side, and this
        // makes the answer in its place, so the depth is unchanged.
        ir_emit(compiler, op == KEST_TOK_PIPEPIPE ? KEST_IR_TRUE : KEST_IR_FALSE,
                expr->type, 0, expr->type, 1, span);
        ir_lands(compiler, done);
        // Whichever way it went, one value is here.
        ir_emit(compiler, KEST_IR_MEET, expr->type, 2, expr->type, 1, span);
        return;
    }

    // An optional against `none`, which asks the flag beside the value and
    // nothing else: the other side is not compiled at all, because what it
    // holds is not part of the question. What is left of the one that is
    // compiled is its last slot, which is the flag — rotated to the bottom of
    // the run and the rest dropped. See D727.
    if ((op == KEST_TOK_EQEQ || op == KEST_TOK_BANGEQ) &&
        (expr->binary.left->kind == KEST_EXPR_NONE) !=
            (expr->binary.right->kind == KEST_EXPR_NONE)) {
        const KestExpr *held = expr->binary.left->kind == KEST_EXPR_NONE
                                   ? expr->binary.right
                                   : expr->binary.left;
        if (held->type != NULL && held->type->tag == KEST_T_OPTIONAL) {
            uint16_t wide = value_slots(held->type);
            compile_expr(compiler, held);
            if (wide > 1) {
                uint32_t turned = ir_emit(compiler, KEST_IR_TURN, held->type, 1,
                                          held->type, wide, span);
                ir_carries(compiler, turned, wide, 0, 0);
                uint32_t kept = ir_emit(compiler, KEST_IR_PART, expr->type, 1,
                                        expr->type, 1, span);
                ir_carries(compiler, kept, 0, 1, wide);
                stack_pop(compiler, (uint16_t)(wide - 1));
            }
            // The flag says it holds something, which is what `!= none` asks.
            if (op == KEST_TOK_EQEQ) {
                ir_emit(compiler, KEST_IR_NOT, expr->type, 1, expr->type, 1,
                        span);
            }
            return;
        }
    }

    compile_expr(compiler, expr->binary.left);
    compile_expr(compiler, expr->binary.right);
    stack_pop(compiler, 1);

    // The operands decide what the operation is about, not the result: a
    // comparison answers `bool` whatever it compared.
    const KestType *operand = expr->binary.left->type;

    // One list of operators, and what each does to the width beside what it
    // does. It was two lists — which operation, and then which of them leave
    // the width — and the second had a `default` under it, so an operator
    // added to the first would leave the width without anybody deciding it
    // should. `&`, `|`, `^` and `>>` need no narrowing: every bit they
    // produce was already in range (D018). Which of `f32`, `f64` and a whole
    // number the arithmetic is on is the type's to say and the backend's to
    // read, so there is one addition here and not three.
    switch (op) {
    case KEST_TOK_PLUS:
        ir_emit(compiler, KEST_IR_ADD, operand, 2, operand, 1, span);
        ir_narrow(compiler, operand, span);
        break;
    case KEST_TOK_MINUS:
        ir_emit(compiler, KEST_IR_SUB, operand, 2, operand, 1, span);
        ir_narrow(compiler, operand, span);
        break;
    case KEST_TOK_STAR:
        ir_emit(compiler, KEST_IR_MUL, operand, 2, operand, 1, span);
        ir_narrow(compiler, operand, span);
        break;
    case KEST_TOK_SLASH:
        ir_emit(compiler, KEST_IR_DIV, operand, 2, operand, 1, span);
        // Once, and only for the pair at the end of the range: the least
        // number over minus one is one past the top of the width.
        if (dividing_can_leave(operand)) {
            ir_narrow(compiler, operand, span);
        }
        break;
    case KEST_TOK_PERCENT:
        ir_emit(compiler, KEST_IR_MOD, operand, 2, operand, 1, span);
        break;
    case KEST_TOK_AMP:
        ir_emit(compiler, KEST_IR_AND, operand, 2, operand, 1, span);
        break;
    case KEST_TOK_PIPE:
        ir_emit(compiler, KEST_IR_OR, operand, 2, operand, 1, span);
        break;
    case KEST_TOK_CARET:
        ir_emit(compiler, KEST_IR_XOR, operand, 2, operand, 1, span);
        break;
    case KEST_TOK_LTLT:
        ir_emit(compiler, KEST_IR_SHL, operand, 2, operand, 1, span);
        ir_narrow(compiler, operand, span);
        break;
    case KEST_TOK_GTGT:
        // What shifts in on the right is the sign when there is one, and
        // nought when there is not, which is what the two types mean, and is
        // read off the type rather than chosen here.
        ir_emit(compiler, KEST_IR_SHR, operand, 2, operand, 1, span);
        break;
    // Both are a run of slots and the answer is one, so the depth after is
    // one below where a scalar compare would leave it.
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GT:
    case KEST_TOK_GTEQ: {
        KestIrKind how = compare_kind(op);
        if (how == KEST_IR_OP_COUNT) {
            fault(compiler, span, "this is an operator with no instruction");
            return;
        }
        // Two values of one width come off and one truth goes on, whatever
        // the width is: a piece of text is two slots now and an enum is as
        // many as its widest case. See D964.
        uint16_t wide = value_slots(operand);
        if (wide > 1) {
            stack_pop(compiler, (uint16_t)((wide - 1) * 2));
        }
        bool whole = operand != NULL && (operand->tag == KEST_T_ENUM ||
                                         operand->tag == KEST_T_STRUCT ||
                                         operand->tag == KEST_T_FIXED);
        uint32_t at = ir_emit(compiler, how, operand, 2, expr->type, 1, span);
        if (whole) {
            ir_carries(compiler, at, layout_of(compiler, operand), 0, 0);
        }
        break;
    }
    default:
        fault(compiler, span, "this is an operator with no instruction");
        return;
    }

}

// Whether what is being put on a run is a whole piece of text rather than one
// of what the run holds. It is a run of bytes taking a piece of text and
// nothing else: a run of *text* taking a piece of text is one element going
// on the end the ordinary way, and asking only whether what was handed over
// was text made `push(words, "alpha")` on a `[text]` copy the bytes of the
// piece into the run of handles. See D1068.
static bool appends_text(const KestExpr *expr) {
    if (expr->call.arg_count != 2 || expr->call.args[1]->type == NULL ||
        expr->call.args[1]->type->tag != KEST_T_TEXT) {
        return false;
    }
    const KestType *array = expr->call.args[0]->type;
    return array != NULL && array->tag == KEST_T_ARRAY &&
           array->element != NULL && array->element->tag == KEST_T_INT &&
           array->element->width == 8 && !array->element->is_signed;
}

// The arguments are already on the stack in the order they were written, so
// each of these is one instruction over them.
static bool compile_builtin(Compiler *compiler, const KestExpr *expr,
                            const char *name, size_t length) {
    if (kest_word_same("len", name, length)) {
        const KestType *subject =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        // How many of them is written in the type, so the answer is a
        // constant and what was counted is dropped.
        if (subject != NULL && subject->tag == KEST_T_FIXED) {
            uint16_t held = value_slots(subject);
            stack_pop(compiler, held);
            ir_emit(compiler, KEST_IR_DROP, subject, 1, NULL, 0, expr->span);
            KestValue how_many = {0};
            how_many.integer = subject->count;
            emit_constant(compiler, how_many, KEST_CONST_INT,
                          whole_type(compiler), expr->span);
            return true;
        }
        KestIrKind counts = KEST_IR_LEN;
        if (subject != NULL && subject->tag == KEST_T_STORE) {
            counts = KEST_IR_STORE_COUNT;
        } else if (subject != NULL && subject->tag == KEST_T_TEXT) {
            counts = KEST_IR_TEXT_LEN;
        }
        stack_pop(compiler, value_slots(subject));
        stack_push(compiler, 1);
        ir_emit(compiler, counts, subject, 1, expr->type, 1, expr->span);
        return true;
    }

    // A piece of text is two slots, so what these take and leave is counted
    // rather than written down as one each. See D964.
    if (kest_word_same("slice", name, length)) {
        stack_pop(compiler, 4);
        stack_push(compiler, 2);
        ir_emit(compiler, KEST_IR_TEXT_SLICE, expr->type, 3, expr->type, 2,
                expr->span);
        return true;
    }

    if (kest_word_same("matches", name, length)) {
        stack_pop(compiler, 5);
        stack_push(compiler, 1);
        ir_emit(compiler, KEST_IR_TEXT_MATCHES, expr->type, 3, expr->type, 1,
                expr->span);
        return true;
    }

    if (kest_word_same("rest", name, length)) {
        stack_pop(compiler, 3);
        stack_push(compiler, 2);
        ir_emit(compiler, KEST_IR_TEXT_REST, expr->type, 2, expr->type, 2,
                expr->span);
        return true;
    }

    if (kest_word_same("find", name, length)) {
        // Where to look from, which is the beginning when it was not said.
        // The operation takes three either way, so there is one of it.
        if (expr->call.arg_count < 3) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler),
                          expr->span);
        }
        stack_pop(compiler, 5);
        stack_push(compiler, 2);
        ir_emit(compiler, KEST_IR_TEXT_FIND, expr->type, 3, expr->type, 2,
                expr->span);
        return true;
    }

    if (kest_word_same("array", name, length)) {
        const KestType *element =
            expr->type == NULL ? NULL : expr->type->element;
        // An empty one has nothing to fill it with, and the operation reads a
        // fill whether it uses it or not, so it gets a nought of the right
        // width and never looks at it.
        if (expr->call.arg_count == 0) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler),
                          expr->span);
            for (uint16_t i = 0; i < value_slots(element); i++) {
                emit_constant(compiler, zero, KEST_CONST_INT,
                              whole_type(compiler), expr->span);
            }
        }
        stack_pop(compiler, (uint16_t)(1 + value_slots(element)));
        stack_push(compiler, 1);
        // The fill was written as a value a slot at a time when it was not
        // written at all, so it is made one value here before it is read.
        uint16_t parts = expr->call.arg_count == 0 ? value_slots(element) : 1;
        if (parts != 1) {
            ir_emit(compiler, KEST_IR_MAKE, element, parts, element,
                    value_slots(element), expr->span);
        }
        uint32_t at = ir_emit(compiler, KEST_IR_ARRAY_NEW, expr->type, 2,
                              expr->type, 1, expr->span);
        ir_carries(compiler, at, layout_of(compiler, element), 0, 0);
        return true;
    }

    const KestType *shrinking =
        expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
    // `remove` answers for a store too, further down, so this asks what it was
    // handed rather than only what it was called.
    if ((kest_word_same("pop", name, length) ||
         kest_word_same("remove", name, length) ||
         kest_word_same("clear", name, length)) &&
        shrinking != NULL && shrinking->tag == KEST_T_ARRAY) {
        const KestType *array = shrinking;
        const KestType *element = array->element;
        if (kest_word_same("clear", name, length)) {
            stack_pop(compiler, 1);
            ir_emit(compiler, KEST_IR_CLEAR, array, 1, NULL, 0, expr->span);
            return true;
        }
        bool taking = kest_word_same("remove", name, length);
        stack_pop(compiler, taking ? 2 : 1);
        uint16_t gives =
            (uint16_t)(value_slots(element) + (taking ? 0 : 1));
        stack_push(compiler, gives);
        uint32_t at = ir_emit(compiler, taking ? KEST_IR_TAKE : KEST_IR_POP_LAST,
                              array, taking ? 2 : 1, expr->type, gives,
                              expr->span);
        ir_carries(compiler, at, layout_of(compiler, element), 0, 0);
        return true;
    }

    // A float as its bits and back. The operation is typed by the float at
    // either end, and carries which way it goes. See D1171.
    if (kest_word_same("bits", name, length) ||
        kest_word_same("float", name, length)) {
        bool to_bits = kest_word_same("bits", name, length);
        const KestType *real =
            to_bits ? (expr->call.arg_count > 0 ? expr->call.args[0]->type
                                                : NULL)
                    : expr->type;
        stack_pop(compiler, 1);
        stack_push(compiler, 1);
        uint32_t at = ir_emit(compiler, KEST_IR_BITS, real, 1, expr->type, 1,
                              expr->span);
        ir_carries(compiler, at, to_bits ? 0 : 1, 0, 0);
        return true;
    }

    if (kest_word_same("hash", name, length)) {
        const KestType *of =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        // A reference goes the same way, though it is one slot: what it is
        // made of is a place and the number the process handed out, and only
        // the place may be hashed -- so the walk that knows what a value is
        // made of does it rather than the instruction that hashes a whole
        // number. See D1054.
        bool whole = of != NULL && (of->tag == KEST_T_ENUM ||
                                    of->tag == KEST_T_STRUCT ||
                                    of->tag == KEST_T_FIXED ||
                                    of->tag == KEST_T_REF);
        stack_pop(compiler, value_slots(of));
        stack_push(compiler, 1);
        uint32_t at =
            ir_emit(compiler, KEST_IR_HASH, of, 1, expr->type, 1, expr->span);
        if (whole) {
            ir_carries(compiler, at, layout_of(compiler, of), 0, 0);
        }
        return true;
    }

    if (kest_word_same("push", name, length)) {
        const KestType *array =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        const KestType *element = array == NULL ? NULL : array->element;
        // A whole piece of text onto a run of bytes is one move rather than
        // the loop a program would write. Which it is comes from what was
        // handed over, because the checker has already said the two are the
        // only shapes there are. See D1068.
        if (appends_text(expr)) {
            stack_pop(compiler, 3);
            uint32_t whole = ir_emit(compiler, KEST_IR_APPEND_TEXT, array, 2,
                                     NULL, 0, expr->span);
            ir_carries(compiler, whole, layout_of(compiler, element), 0, 0);
            return true;
        }
        stack_pop(compiler, (uint16_t)(1 + value_slots(element)));
        uint32_t at = ir_emit(compiler, KEST_IR_APPEND, array, 2, NULL, 0,
                              expr->span);
        ir_carries(compiler, at, layout_of(compiler, element), 0, 0);
        return true;
    }

    if (kest_word_same("fit", name, length)) {
        const KestType *array =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        const KestType *element = array == NULL ? NULL : array->element;
        if (appends_text(expr)) {
            stack_pop(compiler, 3);
            stack_push(compiler, 1);
            uint32_t whole = ir_emit(compiler, KEST_IR_FIT_TEXT, array, 2,
                                     expr->type, 1, expr->span);
            ir_carries(compiler, whole, layout_of(compiler, element), 0, 0);
            return true;
        }
        stack_pop(compiler, (uint16_t)(1 + value_slots(element)));
        stack_push(compiler, 1);
        uint32_t at = ir_emit(compiler, KEST_IR_FIT, array, 2, expr->type, 1,
                              expr->span);
        ir_carries(compiler, at, layout_of(compiler, element), 0, 0);
        return true;
    }

    if (kest_word_same("room", name, length)) {
        const KestType *array =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        const KestType *element = array == NULL ? NULL : array->element;
        stack_pop(compiler, 2);
        uint32_t at =
            ir_emit(compiler, KEST_IR_ROOM, array, 2, NULL, 0, expr->span);
        ir_carries(compiler, at, layout_of(compiler, element), 0, 0);
        return true;
    }

    if (kest_word_same("store", name, length)) {
        uint16_t stride = expr->type == NULL || expr->type->element == NULL
                              ? 1
                              : value_slots(expr->type->element);
        // And which type a place in it holds, written beside the stride so the
        // machine can say what a handle is of. A stride says how far apart two
        // places are and nothing about what is in one. See D927.
        uint16_t holds = layout_of(compiler, expr->type == NULL
                                                 ? NULL
                                                 : expr->type->element);
        // The operation reads how much room to make either way, so one that
        // was not asked for gets a nought, the way an empty `array()` gets a
        // fill it never looks at.
        if (expr->call.arg_count == 0) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler),
                          expr->span);
        }
        stack_pop(compiler, 1);
        stack_push(compiler, 1);
        uint32_t at = ir_emit(compiler, KEST_IR_STORE_NEW, expr->type, 1,
                              expr->type, 1, expr->span);
        ir_carries(compiler, at, stride, holds, 0);
        return true;
    }

    bool adding = kest_word_same("add", name, length);
    bool getting = kest_word_same("get", name, length);
    bool setting = kest_word_same("set", name, length);
    bool removing = kest_word_same("remove", name, length);
    if (!adding && !getting && !setting && !removing) {
        return false;
    }

    const KestType *store =
        expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
    uint16_t stride = store == NULL || store->element == NULL
                          ? 1
                          : value_slots(store->element);

    if (adding) {
        stack_pop(compiler, (uint16_t)(1 + stride));
        stack_push(compiler, 1);
        uint32_t at = ir_emit(compiler, KEST_IR_STORE_ADD, store, 2,
                              expr->type, 1, expr->span);
        ir_carries(compiler, at, stride, 0, 0);
    } else if (getting) {
        stack_pop(compiler, 2);
        stack_push(compiler, (uint16_t)(stride + 1));
        uint32_t at = ir_emit(compiler, KEST_IR_STORE_GET, store, 2,
                              expr->type, (uint16_t)(stride + 1), expr->span);
        ir_carries(compiler, at, stride, 0, 0);
    } else if (setting) {
        stack_pop(compiler, (uint16_t)(2 + stride));
        stack_push(compiler, 1);
        uint32_t at = ir_emit(compiler, KEST_IR_STORE_SET, store, 3,
                              expr->type, 1, expr->span);
        ir_carries(compiler, at, stride, 0, 0);
    } else {
        stack_pop(compiler, 2);
        stack_push(compiler, 1);
        ir_emit(compiler, KEST_IR_STORE_REMOVE, store, 2, expr->type, 1,
                expr->span);
    }
    return true;
}

// The argument is already on the stack, so a conversion is what has to happen
// to it and nothing else.
static void compile_conversion(Compiler *compiler, const KestExpr *expr,
                               const KestType *to) {
    if (expr->call.arg_count != 1) {
        return;
    }
    const KestType *from = expr->call.args[0]->type;
    if (from == NULL) {
        return;
    }
    bool from_real = from->tag == KEST_T_FLOAT;

    if (to->tag == KEST_T_INT) {
        if (from_real) {
            uint32_t at = ir_emit(compiler, KEST_IR_TO_WHOLE, to, 1, to, 1,
                                  expr->span);
            ir_carries(compiler, at, kest_scalar_of(to), 0, 0);
        } else {
            // A `bool` is already nought or one, and an integer only has to
            // be cut to the width it is going into — and not even that where
            // that width already holds every value it has. See D867.
            if (!every_value_fits(from, to)) {
                ir_narrow(compiler, to, expr->span);
            }
        }
        return;
    }

    if (!from_real) {
        // Whether what it came from had a sign is the type's to say.
        ir_emit(compiler, KEST_IR_TO_FLOAT, from, 1, to, 1, expr->span);
    }
    // A slot holds a double either way, so widening is nothing and narrowing
    // is a rounding.
    if (to->width == 32) {
        ir_emit(compiler, KEST_IR_TO_F32, to, 1, to, 1, expr->span);
    }
}

static const KestVariantType *case_named(const KestType *choice,
                                         const char *name, size_t length) {
    for (uint32_t i = 0; i < choice->case_count; i++) {
        if (kest_word_same(choice->cases[i].name, name, length)) {
            return &choice->cases[i];
        }
    }
    return NULL;
}

// A case is its tag and its payload, padded out to whatever the widest case
// needs, because every case of one enum is the same size.
static void compile_case_tail(Compiler *compiler, const KestExpr *expr,
                              const KestType *choice, KestSpan name) {
    const KestVariantType *variant =
        case_named(choice, span_text(compiler, name), name.length);
    if (variant == NULL) {
        return;
    }

    uint16_t carried = 0;
    for (uint32_t i = 0; i < variant->payload_count; i++) {
        carried += value_slots(variant->payload[i]);
    }
    uint16_t slack = (uint16_t)(choice->slots - 1 - carried);
    KestValue zero = {0};
    for (uint16_t i = 0; i < slack; i++) {
        emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler), expr->span);
    }

    KestValue tag = {0};
    tag.integer = (int64_t)(variant - choice->cases);
    emit_constant(compiler, tag, KEST_CONST_INT, whole_type(compiler), expr->span);

    // The tag was made last and belongs first, so the whole value is turned
    // over: what was made is payload then tag, and what a slot run is is tag
    // then payload.
    stack_pop(compiler, (uint16_t)(carried + slack + 1));
    stack_push(compiler, choice->slots);
    uint16_t parts = (uint16_t)(variant->payload_count + slack + 1);
    ir_emit(compiler, KEST_IR_MAKE, choice, parts, choice, choice->slots,
            expr->span);
    uint32_t turned = ir_emit(compiler, KEST_IR_TURN, choice, 1, choice,
                              choice->slots, expr->span);
    ir_carries(compiler, turned, choice->slots, 0, 0);
}

// Through a value: the arguments are on the stack, then which function it is,
// which the instruction takes off the top. What it promises is in its type, so
// a cost contract holds without knowing which function it will be.
static void compile_value_call(Compiler *compiler, const KestExpr *expr) {
    uint16_t through = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        through += value_slots(expr->call.args[i]->type);
    }
    const KestType *shape = expr->call.callee->type;
    compile_expr(compiler, expr->call.callee);
    stack_pop(compiler, (uint16_t)(through + 1));
    // What it gives rather than what the expression is, for the reason above.
    uint16_t coming_back = shape != NULL && shape->tag == KEST_T_FN
                               ? value_slots(shape->result)
                               : value_slots(expr->type);
    stack_push(compiler, coming_back);
    uint32_t at = ir_emit(compiler, KEST_IR_CALL_VALUE, shape,
                          (uint16_t)(expr->call.arg_count + 1), expr->type,
                          coming_back, expr->span);
    // How wide what it takes is, and what this call is expecting back, which
    // the machine has no other way to know: which function it enters is a
    // number, and a number a host wrote may name one of another shape.
    // See D835.
    //
    // And whether what it enters promises to reach no heap, which is what
    // says a call inside a working-memory block cannot grow what it was
    // handed. A promise is part of a function's type, so reading it off the
    // shape here is reading it off the type. See D1075.
    ir_carries(compiler, at, through, coming_back,
               shape != NULL && shape->no_alloc ? 1 : 0);
}

static void compile_call(Compiler *compiler, const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;

    // A call whose answer cannot be anything else is a value rather than work.
    // The folder has always known three of them -- `hash` over something
    // written down, `len` of a run whose size the type says, and a conversion
    // of a number -- which is what makes `const H: u64 = hash("abc")` a number
    // before the program starts. Nothing asked it about the same call written
    // in a body, so a program that hashed a name of three letters hashed them
    // again on every frame that went past. The reference says a hash of a
    // piece of text is a value a frame does not pay for; it is one now.
    //
    // Asked only where the answer is a number, which is what all three of
    // them give back. The folder can also work out a shape built from values
    // written down -- `Vec3(0.0, 1.0, 0.0)` is as settled as a hash is -- and
    // that is a wider change than this one: it makes a struct with nothing in
    // it a constant, which is the one thing that reaches the branch below
    // holding what an empty one is built as. See D886.
    if (expr->type != NULL &&
        (expr->type->tag == KEST_T_INT || expr->type->tag == KEST_T_FLOAT) &&
        compile_folded(compiler, expr)) {
        return;
    }

    // A function is a value, so it is reached the way a value is: out of an
    // array, out of a store, out of whatever holds it. Only a name and a
    // dotted name are looked up as names, and what is left is called through
    // what it is.
    if (callee->kind != KEST_EXPR_NAME && callee->kind != KEST_EXPR_FIELD &&
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        !callee->type->is_foreign) {
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            compile_expr(compiler, expr->call.args[i]);
        }
        compile_value_call(compiler, expr);
        return;
    }

    // A dotted callee is a function in another module, or an extern named for
    // its host type. Both are one name with a dot in it.
    if (callee->kind != KEST_EXPR_NAME && callee->kind != KEST_EXPR_FIELD) {
        fault(compiler, callee->span, "this calls something that is not a "
                                     "function");
        return;
    }

    // `len` of a `[T; N]` is a number in the type, and what it was given is
    // dropped. A name has nothing to do but be loaded, so a run of two
    // hundred and fifty-six slots was copied onto the stack to be thrown
    // away. Anything else is still worked out: a call in there is the point
    // of the line as often as not.
    const KestExpr *only = expr->call.arg_count == 1 ? expr->call.args[0] : NULL;
    bool is_a_function =
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        callee->type->symbol != NULL &&
        kest_module_find(compiler->module, callee->type->symbol) >= 0;
    if (!is_a_function && only != NULL && only->kind == KEST_EXPR_NAME &&
        only->type != NULL && only->type->tag == KEST_T_FIXED &&
        callee->kind == KEST_EXPR_NAME &&
        kest_word_same("len", span_text(compiler, callee->span),
                       callee->span.length)) {
        KestValue how_many = {0};
        how_many.integer = only->type->count;
        emit_constant(compiler, how_many, KEST_CONST_INT, whole_type(compiler), expr->span);
        return;
    }

    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        compile_expr(compiler, expr->call.args[i]);
    }

    // Building a struct costs nothing. Its fields were made in declaration
    // order, which is the layout, so the value is what they are together.
    //
    // Except one with no fields, which is a slot all the same: a struct is at
    // least one slot wide so that a value of one is a value, and every other
    // place in this compiler believes that width — the frame it is passed in,
    // the slots it is stored to, what a constant of one has to fill. Pushing
    // nothing here left every argument after it a slot low, so
    // `takes(Empty(), 3, 4)` read its numbers out of whatever the stack was
    // holding. See D807.
    if (callee->type != NULL && callee->type->tag == KEST_T_STRUCT) {
        if (callee->type->member_count == 0) {
            KestValue nothing = {0};
            emit_constant(compiler, nothing, KEST_CONST_INT, callee->type,
                          expr->span);
        } else if (expr->call.arg_count != 1) {
            ir_emit(compiler, KEST_IR_MAKE, callee->type,
                    (uint16_t)expr->call.arg_count, callee->type,
                    value_slots(callee->type), expr->span);
        }
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_ENUM) {
        // The arguments are already on the stack where the payload goes; the
        // rest of the value is the tag under them and nothing above.
        compile_case_tail(compiler, expr, callee->type,
                          callee->kind == KEST_EXPR_FIELD ? callee->field.name
                                                          : callee->span);
        return;
    }
    if (callee->type != NULL && (callee->type->tag == KEST_T_INT ||
                                 callee->type->tag == KEST_T_FLOAT)) {
        compile_conversion(compiler, expr, callee->type);
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_FLAGS) {
        if (expr->call.arg_count == 0) {
            KestValue empty = {0};
            emit_constant(compiler, empty, KEST_CONST_INT, callee->type, expr->span);
        }
        // A set made from a number of the same width is the same bits, so
        // there is nothing to emit over what is already on the stack.
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_TEXT &&
        expr->call.arg_count == 1) {
        stack_pop(compiler, 1);
        stack_push(compiler, 2);
        ir_emit(compiler, KEST_IR_TEXT_FROM, callee->type, 1, callee->type, 2,
                expr->span);
        return;
    }

    // Through a value: the arguments are on the stack, then which function it
    // is, which the instruction takes off the top.
    // A local holds a function rather than being one, and a function type
    // with no symbol is a value rather than a declaration. `io.print` is a
    // declaration with a dot in its name and goes the other way.
    bool through_value =
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        !callee->type->is_foreign &&
        (callee->type->symbol == NULL ||
         (callee->kind == KEST_EXPR_NAME &&
          find_local(compiler, callee->span) != NULL));
    if (through_value) {
        compile_value_call(compiler, expr);
        return;
    }

    const char *name = span_text(compiler, callee->span);
    uint16_t argument_slots = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        argument_slots += value_slots(expr->call.args[i]->type);
    }
    // What the function gives, which is not always what the expression is: a
    // value standing where an optional is wanted is widened by the checker and
    // the tag is emitted after the call, so asking the expression here would
    // count the tag twice — and a host call carries this number, so it would
    // write over the slot beside its answer.
    uint16_t result_slots =
        callee->type != NULL && callee->type->tag == KEST_T_FN
            ? value_slots(callee->type->result)
            : value_slots(expr->type);

    // The checker already settled which function this is, and its symbol is
    // what it was compiled under, so nothing is chosen twice. A file that
    // declares its own `find` gets that one here for the same reason it got
    // it there.
    int32_t index = -1;
    if (callee->type != NULL && callee->type->tag == KEST_T_FN &&
        callee->type->symbol != NULL) {
        index = kest_module_find(compiler->module, callee->type->symbol);
    }
    if (index < 0 &&
        compile_builtin(compiler, expr, name, callee->span.length)) {
        return;
    }
    if (index >= 0) {
        stack_pop(compiler, argument_slots);
        stack_push(compiler, result_slots);
        uint32_t at = ir_emit(compiler, KEST_IR_CALL, callee->type,
                              (uint16_t)expr->call.arg_count, expr->type,
                              result_slots, expr->span);
        // And whether it promises to reach no heap, which is what says a
        // call inside a working-memory block cannot grow what it was handed.
        // See D1075.
        ir_carries(compiler, at, (uint16_t)index, argument_slots,
                   callee->type != NULL && callee->type->no_alloc ? 1 : 0);
        return;
    }

    // Not defined here, so it is declared: the host provides it, and which
    // one it is is settled by name before the program runs.
    const KestType *foreign = callee->type;
    if (foreign == NULL || foreign->tag != KEST_T_FN || !foreign->is_foreign) {
        fault(compiler, callee->span,
              "this calls a function with no body and no host to provide it");
        return;
    }

    const KestSymbol *declared =
        kest_lookup_global(compiler->program, name, callee->span.length);
    int32_t slot = kest_module_extern(
        compiler->module, foreign->foreign_name,
        declared == NULL ? callee->span : declared->span,
        declared == NULL ? compiler->program->source : declared->source,
        foreign->no_alloc, foreign->deterministic);
    if (slot < 0) {
        compiler->out_of_memory = true;
        return;
    }
    if (slot >= MAX_EXTERNS) {
        // The one below is written into the call in two bytes, so this is the
        // last one there is room to name.
        refuse(compiler, callee->span, "K0502",
               "a program asks the host for at most %d names", MAX_EXTERNS);
        return;
    }
    // What the program expects to cross, written down where a host can read
    // it: the same layouts a function of the program's own carries, because a
    // crossing is the same shape whichever way it goes.
    if (foreign->param_count > 0 || foreign->result != NULL) {
        uint16_t *widths = NULL;
        if (foreign->param_count > 0) {
            widths = KEST_ARENA_ARRAY(compiler->module->arena, uint16_t,
                                      foreign->param_count);
            if (widths == NULL) {
                compiler->out_of_memory = true;
                return;
            }
            for (uint32_t p = 0; p < foreign->param_count; p++) {
                widths[p] = layout_of(compiler, foreign->params[p]);
            }
        }
        bool gives_value =
            foreign->result != NULL && foreign->result->tag != KEST_T_VOID;
        kest_module_extern_shape(
            compiler->module, (uint32_t)slot, widths,
            (uint16_t)foreign->param_count,
            gives_value ? layout_of(compiler, foreign->result) : 0,
            gives_value);
    }
    stack_pop(compiler, argument_slots);
    stack_push(compiler, result_slots);
    uint32_t at = ir_emit(compiler, KEST_IR_CALL_HOST, foreign,
                          (uint16_t)expr->call.arg_count, expr->type,
                          result_slots, expr->span);
    ir_carries(compiler, at, (uint16_t)slot, argument_slots, result_slots);
}

static void compile_expr_kind(Compiler *compiler, const KestExpr *expr) {
    switch (expr->kind) {
    case KEST_EXPR_INT: {
        KestValue value = {0};
        // The lexer's reader, which the checker also uses, because a `u64`
        // literal does not fit in the signed accumulator this used to have.
        bool overflow = false;
        value.integer = (int64_t)kest_token_integer(
            span_text(compiler, expr->span), expr->span.length, &overflow);
        emit_constant(compiler, value, KEST_CONST_INT, expr->type, expr->span);
        break;
    }
    case KEST_EXPR_FLOAT: {
        KestValue value = {0};
        value.real = parse_real(compiler, expr->span);
        // An `f32` literal is the nearest `f32`, not the nearest double that
        // happens to be spelled the same way.
        if (kest_is_narrow(expr->type)) {
            value.real = (float)value.real;
        }
        emit_constant(compiler, value, KEST_CONST_FLOAT, expr->type, expr->span);
        break;
    }
    case KEST_EXPR_STRING: {
        KestValue value = {0};
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        size_t length = 0;
        value.text = literal_text(compiler, content, &length);
        emit_text_constant(compiler, value.text, length, expr->type,
                           expr->span);
        break;
    }
    case KEST_EXPR_BYTE: {
        // The same escapes a string has, read the same way, so a byte written
        // in one and a byte written on its own are one spelling.
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        const char *held = literal_text(compiler, content, NULL);
        KestValue value = {0};
        value.integer = (unsigned char)held[0];
        emit_constant(compiler, value, KEST_CONST_INT, expr->type, expr->span);
        break;
    }
    case KEST_EXPR_BOOL:
        stack_push(compiler, 1);
        ir_emit(compiler, expr->boolean ? KEST_IR_TRUE : KEST_IR_FALSE,
                expr->type, 0, expr->type, 1, expr->span);
        break;

    case KEST_EXPR_NONE: {
        // An optional is what it holds with a tag after it, so the missing
        // case is that many slots of nothing and a tag that says so.
        uint16_t size = value_slots(expr->type);
        KestValue zero = {0};
        for (uint16_t i = 1; i < size; i++) {
            emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler),
                          expr->span);
        }
        stack_push(compiler, 1);
        ir_emit(compiler, KEST_IR_FALSE, expr->type, 0, expr->type, 1,
                expr->span);
        if (size != 1) {
            ir_emit(compiler, KEST_IR_MAKE, expr->type, size, expr->type, size,
                    expr->span);
        }
        break;
    }
    case KEST_EXPR_NAME: {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL) {
            compile_constant(compiler, expr);
            break;
        }
        if (local->folded != NULL) {
            emit_value_slots(compiler, local->holds, local->folded,
                             local->size, expr->span);
            break;
        }
        stack_push(compiler, local->size);
        load_slots(compiler, local->slot, local->size, local->type, expr->span);
        break;
    }
    case KEST_EXPR_UNARY:
        compile_expr(compiler, expr->unary.operand);
        if (expr->unary.op == KEST_TOK_BANG) {
            ir_emit(compiler, KEST_IR_NOT, expr->type, 1, expr->type, 1,
                    expr->span);
        } else if (expr->unary.op == KEST_TOK_TILDE) {
            ir_emit(compiler, KEST_IR_FLIP, expr->type, 1, expr->type, 1,
                    expr->span);
            // `~0` is every bit of the width it is declared at, so a `u8` one
            // is 255 rather than the slot's -1.
            ir_narrow(compiler, expr->type, expr->span);
        } else {
            ir_emit(compiler, KEST_IR_NEG, expr->type, 1, expr->type, 1,
                    expr->span);
            ir_narrow(compiler, expr->type, expr->span);
        }
        break;
    case KEST_EXPR_BINARY:
        // Arithmetic on what is written down is worked out here rather than
        // on every run: there is no negative literal, so every `0 - 1` in a
        // body was two constants and a subtraction each time it ran. The
        // folder answers what the machine answers, wrapping included, and
        // does not work out a division by nought, so that stays a refusal
        // where it runs. See D1160.
        if (expr->type != NULL &&
            (expr->type->tag == KEST_T_INT || expr->type->tag == KEST_T_FLOAT) &&
            compile_folded(compiler, expr)) {
            break;
        }
        compile_binary(compiler, expr);
        break;
    case KEST_EXPR_CALL:
        compile_call(compiler, expr);
        break;
    case KEST_EXPR_FIELD: {
        if (compile_folded(compiler, expr)) {
            break;
        }
        // `sort.ascending` is one name with a dot in it, not a field of a
        // `sort`, and where a value is wanted it is which function it is.
        if (compile_function_value(compiler, expr)) {
            break;
        }
        // `box.CELLS` is one name with a dot in it, the same way
        // `sort.ascending` is: a constant another module declared, which
        // crosses out of the file it is in. See D665.
        const KestSymbol *elsewhere = kest_lookup_global(
            compiler->program, span_text(compiler, expr->span),
            expr->span.length);
        if (elsewhere != NULL && elsewhere->is_const) {
            compile_constant(compiler, expr);
            break;
        }
        // A named bit is a constant: which bit it is, is where it was
        // written, so nothing is stored and nothing can drift.
        if (expr->field.object->type != NULL &&
            expr->field.object->type->tag == KEST_T_FLAGS) {
            const KestType *set = expr->field.object->type;
            const KestVariantType *bit =
                case_named(set, span_text(compiler, expr->field.name),
                           expr->field.name.length);
            KestValue value = {0};
            if (bit != NULL) {
                value.integer = (int64_t)1 << (bit - set->cases);
            }
            emit_constant(compiler, value, KEST_CONST_INT, expr->type, expr->span);
            break;
        }
        if (expr->field.object->type != NULL &&
            expr->field.object->type->tag == KEST_T_ENUM) {
            compile_case_tail(compiler, expr, expr->field.object->type,
                              expr->field.name);
            break;
        }
        uint16_t slot = 0;
        uint16_t size = 0;
        if (resolve_place(compiler, expr, &slot, &size)) {
            stack_push(compiler, size);
            load_slots(compiler, slot, size, expr->type, expr->span);
            break;
        }
        const KestMember *member =
            find_member(expr->field.object->type,
                        span_text(compiler, expr->field.name),
                        expr->field.name.length);
        if (member == NULL) {
            fault(compiler, expr->span,
                  "this reads a field the type does not have");
            break;
        }
        // A field of something that has an address is read from that address.
        // Otherwise the whole value would be unpacked out of the host's bytes
        // to keep one piece of it, which is what a frame reads most.
        //
        // What comes off is the member, which is what the struct holds and
        // not what the expression is: a value standing where an optional is
        // wanted is widened by the checker, and read at the width of the
        // widened type this took the member and the bytes after it out of a
        // struct that has no tag in it. The tag goes on afterwards, the same
        // as it does after a call and after one of a run. See D809 and D1055.
        uint16_t offset = 0;
        if (can_address(compiler, expr) &&
            compile_address(compiler, expr, &offset)) {
            stack_pop(compiler, 1);
            stack_push(compiler, value_slots(member->type));
            load_at(compiler, offset, member->type, expr->span);
            break;
        }
        // The struct is not in a slot, so it has to be built on the stack and
        // the member kept out of it.
        uint16_t total = value_slots(expr->field.object->type);
        uint16_t kept = value_slots(member->type);
        compile_expr(compiler, expr->field.object);
        stack_pop(compiler, (uint16_t)(total - kept));
        uint32_t part = ir_emit(compiler, KEST_IR_PART, expr->type, 1,
                                expr->type, kept, expr->span);
        ir_carries(compiler, part, member->offset, kept, total);
        break;
    }
    case KEST_EXPR_INDEX: {
        if (compile_folded(compiler, expr)) {
            break;
        }
        const KestType *object = expr->index.object->type;
        // That many of something is a value, so one of them is at a slot the
        // index works out rather than behind a handle.
        if (object != NULL && object->tag == KEST_T_FIXED) {
            uint16_t stride = value_slots(object->element);
            uint16_t slot = 0;
            uint16_t size = 0;
            // One of a run the chunk holds. The folder cannot see a local, so
            // what says the run is settled is the name rather than another
            // fold -- and a position written down is one value rather than a
            // reach into the run. See D887.
            Local *by_name = expr->index.object->kind == KEST_EXPR_NAME
                                 ? find_local(compiler, expr->index.object->span)
                                 : NULL;
            if (by_name != NULL && by_name->folded != NULL) {
                int64_t at = written_index(compiler, expr->index.index);
                if (at >= 0 && (uint64_t)at < object->count) {
                    emit_value_slots(compiler, object->element,
                                     by_name->folded + at * stride, stride,
                                     expr->span);
                    break;
                }
                uint32_t first = 0;
                if (!constant_run(compiler, object, by_name->folded,
                                  by_name->size, &first)) {
                    break;
                }
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                uint32_t one = ir_emit(compiler, KEST_IR_CONST_AT,
                                       object->element, 1, object->element,
                                       stride, expr->span);
                ir_carries(compiler, one, (uint16_t)first, stride,
                           (uint16_t)object->count);
                break;
            }
            // An index written down is a slot, the same way a field is, or a
            // byte offset where the run is memory the host laid out.
            if (resolve_place(compiler, expr, &slot, &size)) {
                stack_push(compiler, size);
                load_slots(compiler, slot, size, expr->type, expr->span);
                break;
            }
            uint16_t written = 0;
            if (can_address(compiler, expr) &&
                compile_address(compiler, expr, &written)) {
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                load_at(compiler, written, object->element, expr->span);
                break;
            }
            if (resolve_place(compiler, expr->index.object, &slot, &size)) {
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                uint32_t held = run_place(compiler, slot, stride,
                                          (uint16_t)object->count,
                                          object->element, expr->span);
                uint32_t one = ir_emit(compiler, KEST_IR_LOAD, object->element,
                                       1, object->element, stride, expr->span);
                ir_place_of(compiler, one, held);
                break;
            }
            uint16_t offset = 0;
            if (can_address(compiler, expr->index.object) &&
                compile_address(compiler, expr->index.object, &offset)) {
                // The address of the run, then one step into it.
                if (offset > 0) {
                    KestValue nothing = {0};
                    nothing.integer = 0;
                    emit_constant(compiler, nothing, KEST_CONST_INT,
                                  whole_type(compiler), expr->span);
                    stack_push(compiler, 1);
                    stack_pop(compiler, 1);
                    offset_addr(compiler, offset, 1, object->element,
                                expr->span);
                }
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                offset_addr(compiler, object->element->byte_size,
                            (uint16_t)object->count, object->element,
                            expr->span);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                load_at(compiler, 0, object->element, expr->span);
                break;
            }
            // The run is a constant, so it is in the chunk already: one of
            // it is read there rather than copied into slots to be read back.
            uint16_t wide = object->slots;
            KestValue *held =
                KEST_ARENA_ARRAY(compiler->ir->arena, KestValue,
                                 wide == 0 ? 1 : wide);
            const char *why = NULL;
            if (held != NULL && wide > 0 &&
                kest_fold_const(compiler->program, expr->index.object, held,
                                wide, &why, NULL) == wide) {
                uint32_t first = 0;
                if (!constant_run(compiler, object, held, wide, &first)) {
                    break;
                }
                counted_fold(compiler, wide);
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                uint32_t one = ir_emit(compiler, KEST_IR_CONST_AT,
                                       object->element, 1, object->element,
                                       stride, expr->span);
                ir_carries(compiler, one, (uint16_t)first, stride,
                           (uint16_t)object->count);
                break;
            }

            // Not a place and not an address, which is what a value where
            // it stands is: what a call gave back. It goes into slots of its
            // own and is indexed there, the same way a walk of one copies it
            // before walking it.
            if (object->slots > 0) {
                uint16_t kept = reserve_slot(compiler, object->slots);
                compile_expr(compiler, expr->index.object);
                stack_pop(compiler, object->slots);
                store_slots(compiler, kept, object->slots, object, expr->span);
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                uint32_t where = run_place(compiler, kept, stride,
                                           (uint16_t)object->count,
                                           object->element, expr->span);
                uint32_t one = ir_emit(compiler, KEST_IR_LOAD, object->element,
                                       1, object->element, stride, expr->span);
                ir_place_of(compiler, one, where);
                break;
            }
            fault(compiler, expr->span, "this indexes a run of nothing");
            break;
        }
        compile_expr(compiler, expr->index.object);
        compile_expr(compiler, expr->index.index);
        // What comes off is one element, which is what the run holds and not
        // what the expression is: a value standing where an optional is
        // wanted is widened by the checker, and read at the width of the
        // widened type this took the element and the bytes after it out of a
        // run that has no tag in it. The tag goes on afterwards, the same as
        // it does after a call. See D809.
        const KestType *one = object == NULL || object->element == NULL
                                  ? expr->type
                                  : object->element;
        stack_pop(compiler, (uint16_t)(value_slots(object) + 1));
        if (object != NULL && object->tag == KEST_T_TEXT) {
            stack_push(compiler, 1);
            ir_emit(compiler, KEST_IR_TEXT_AT, object, 2, expr->type, 1,
                    expr->span);
            break;
        }
        stack_push(compiler, value_slots(one));
        uint32_t held = elem_place(compiler, ir_top(compiler, 1),
                                   ir_top(compiler, 0), 0, one, expr->span);
        uint32_t read = ir_emit(compiler, KEST_IR_LOAD, one, 2, one,
                                value_slots(one), expr->span);
        ir_place_of(compiler, read, held);
        break;
    }

    case KEST_EXPR_TEXT: {
        for (uint32_t i = 0; i < expr->text.count; i++) {
            const KestTextPart *part = &expr->text.parts[i];
            if (part->value == NULL) {
                KestValue value = {0};
                size_t length = 0;
                value.text = literal_text(compiler, part->text, &length);
                emit_text_constant(compiler, value.text, length, expr->type,
                                   expr->span);
                continue;
            }
            compile_expr(compiler, part->value);
            const KestType *type = part->value->type;
            if (type == NULL || type->tag == KEST_T_TEXT) {
                continue;
            }
            // A set of bits is written the way it is built, so the names it
            // holds have to come with it. The layout already carries the
            // type, so nothing new is stored for it.
            // A run of slots whose text is a piece of text, so what the
            // walk of the parts counts has to come back to the two slots one
            // is. An optional is the same shape: what it holds, and a tag
            // after it. See D964.
            bool laid_out = type->tag == KEST_T_FLAGS ||
                            type->tag == KEST_T_ENUM ||
                            type->tag == KEST_T_OPTIONAL ||
                            type->tag == KEST_T_STRUCT ||
                            type->tag == KEST_T_FIXED;
            stack_pop(compiler, value_slots(type));
            stack_push(compiler, 2);
            uint32_t written = ir_emit(compiler, KEST_IR_TEXT_OF, type, 1,
                                       expr->type, 2, expr->span);
            if (laid_out) {
                ir_carries(compiler, written, layout_of(compiler, type), 0, 0);
            }
        }
        stack_pop(compiler, (uint16_t)(expr->text.count * 2));
        stack_push(compiler, 2);
        uint32_t joined = ir_emit(compiler, KEST_IR_TEXT_JOIN, expr->type,
                                  (uint16_t)expr->text.count, expr->type, 2,
                                  expr->span);
        ir_carries(compiler, joined, (uint16_t)expr->text.count, 0, 0);
        break;
    }

    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        uint16_t gives = branch->gives ? value_slots(expr->type) : 0;

        // A condition settled where it is written: the arm that runs is the
        // only one compiled, and it leaves what the `if` would. See D1231.
        bool settled;
        if (branch->binding.length == 0 &&
            known_truth(compiler, branch->condition, &settled)) {
            if (settled) {
                if (branch->then_value != NULL) {
                    compile_expr(compiler, branch->then_value);
                } else {
                    compile_block(compiler, &branch->then_body);
                }
            } else if (branch->otherwise != NULL) {
                compile_expr(compiler, branch->otherwise);
            } else if (branch->else_value != NULL) {
                compile_expr(compiler, branch->else_value);
            } else if (branch->has_else) {
                compile_block(compiler, &branch->else_body);
            }
            break;
        }

        Exits otherwise =
            compile_condition(compiler, branch->condition,
                              branch->binding.length > 0);

        // `if let` leaves what the optional held below the tag the jump
        // consumed. The taken arm binds it; the other arm drops it.
        uint16_t held = 0;
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        // What the condition left, which the arm that runs binds and the one
        // that does not drops. It is one value read on either way out.
        KestIrRef bound = KEST_IR_NONE;
        if (branch->binding.length > 0) {
            const KestType *optional = branch->condition->type;
            held = (uint16_t)(value_slots(optional) - 1);
            compiler->depth++;
            uint16_t slot = declare_local(
                compiler, branch->binding,
                optional == NULL ? NULL : optional->element);
            bound = ir_top(compiler, 0);
            stack_pop(compiler, held);
            store_slots(compiler, slot, held,
                        optional == NULL ? NULL : optional->element,
                        expr->span);
        }

        uint16_t ways = 0;
        if (branch->then_value != NULL) {
            compile_expr(compiler, branch->then_value);
            // Both ways leave the same thing, so the depth after the `if` is
            // the depth after either one of them.
            stack_pop(compiler, gives);
            ways = (uint16_t)(ways + (gives > 0 ? 1 : 0));
        } else {
            compile_block(compiler, &branch->then_body);
        }

        if (branch->binding.length > 0) {
            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
        }

        if (!branch->has_else && held == 0) {
            land_exits(compiler, &otherwise);
            break;
        }
        uint32_t done = ir_go(compiler, expr->span);
        land_exits(compiler, &otherwise);
        if (held > 0) {
            ir_leaves(compiler, bound);
            ir_emit(compiler, KEST_IR_DROP, NULL, 1, NULL, 0, expr->span);
        }
        if (branch->otherwise != NULL) {
            compile_expr(compiler, branch->otherwise);
            stack_pop(compiler, gives);
            ways = (uint16_t)(ways + (gives > 0 ? 1 : 0));
        } else if (branch->else_value != NULL) {
            compile_expr(compiler, branch->else_value);
            stack_pop(compiler, gives);
            ways = (uint16_t)(ways + (gives > 0 ? 1 : 0));
        } else if (branch->has_else) {
            compile_block(compiler, &branch->else_body);
        }
        ir_lands(compiler, done);
        stack_push(compiler, gives);
        if (ways > 0) {
            ir_emit(compiler, KEST_IR_MEET, expr->type, ways, expr->type, gives,
                    expr->span);
        }
        break;
    }

    case KEST_EXPR_MATCH: {
        const KestChoose *choose = expr->choose;
        uint32_t count = choose->subject_count;
        if (count > 8) {
            fault(compiler, expr->span,
                  "this matches more things at once than there are "
                  "instructions for");
            break;
        }
        const KestType *chosen[8];
        uint16_t subject[8];
        for (uint32_t i = 0; i < count; i++) {
            chosen[i] = choose->subjects[i]->type;
            if (chosen[i] == NULL || chosen[i]->tag != KEST_T_ENUM) {
                fault(compiler, expr->span, "this matches something that is "
                                            "not an enum");
                return;
            }
        }
        uint16_t gives = value_slots(expr->type);

        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        compiler->depth++;

        // Each subject goes into slots of its own, so an arm can name what a
        // case was carrying without moving anything.
        for (uint32_t i = 0; i < count; i++) {
            subject[i] = reserve_slot(compiler, chosen[i]->slots);
            compile_expr(compiler, choose->subjects[i]);
            stack_pop(compiler, chosen[i]->slots);
            store_slots(compiler, subject[i], chosen[i]->slots, chosen[i],
                        expr->span);
        }

        uint32_t leaves[MAX_BREAKS];
        uint32_t leave_count = 0;
        // How many arms left a value behind them, which is how many ways
        // there are of arriving at what the match is worth.
        uint16_t ways = 0;
        for (uint32_t a = 0; a < choose->arm_count; a++) {
            const KestArm *arm = &choose->arms[a];
            bool blanket =
                arm->part_count == 1 && arm->parts[0].name.length == 0;

            // One test per position that names a case. A position that says
            // `else` tests nothing, so an arm of them tests nothing at all.
            // Nor does the last arm: the checker refuses a `match` that does
            // not answer every combination, and a tag is a case of its type
            // wherever it came from -- a host's is asked at the crossing and
            // one read out of memory where it is read -- so a run that got
            // past every other arm is in this one. See D1192.
            bool last = a + 1 == choose->arm_count;
            uint32_t nexts[8];
            uint32_t next_count = 0;
            bool unknown = false;
            for (uint32_t p = 0;
                 !blanket && !last && p < arm->part_count && p < count; p++) {
                const KestArmPart *part = &arm->parts[p];
                if (part->name.length == 0) {
                    continue;
                }
                const KestVariantType *variant =
                    case_named(chosen[p], span_text(compiler, part->name),
                               part->name.length);
                if (variant == NULL) {
                    unknown = true;
                    break;
                }
                stack_push(compiler, 1);
                load_slots(compiler, subject[p], 1, whole_type(compiler),
                           expr->span);
                KestValue tag = {0};
                tag.integer = (int64_t)(variant - chosen[p]->cases);
                emit_constant(compiler, tag, KEST_CONST_INT,
                              whole_type(compiler), expr->span);
                stack_pop(compiler, 1);
                ir_emit(compiler, KEST_IR_EQ, whole_type(compiler), 2,
                        truth_type(compiler), 1, expr->span);
                stack_pop(compiler, 1);
                nexts[next_count++] = ir_ask(compiler, false, expr->span);
            }
            if (unknown) {
                continue;
            }

            uint16_t arm_names = compiler->local_count;
            compiler->depth++;
            for (uint32_t p = 0; !blanket && p < arm->part_count && p < count;
                 p++) {
                const KestArmPart *part = &arm->parts[p];
                if (part->name.length == 0) {
                    continue;
                }
                const KestVariantType *variant =
                    case_named(chosen[p], span_text(compiler, part->name),
                               part->name.length);
                for (uint32_t b = 0;
                     b < part->binding_count && variant != NULL &&
                     b < variant->payload_count;
                     b++) {
                    bind_local(compiler, part->bindings[b],
                               (uint16_t)(subject[p] + variant->offsets[b]),
                               value_slots(variant->payload[b]));
                }
            }
            if (arm->value != NULL) {
                compile_expr(compiler, arm->value);
                // Every arm leaves the same thing, so the depth after the
                // match is the depth after any one of them.
                stack_pop(compiler, gives);
                ways = (uint16_t)(ways + (gives > 0 ? 1 : 0));
            } else {
                compile_block(compiler, &arm->body);
            }
            compiler->depth--;
            compiler->local_count = arm_names;

            // And the last arm falls through to where the others meet, the
            // way an `if`'s second arm does: a branch from it would land on
            // the operation after itself.
            if (!last && leave_count < MAX_BREAKS) {
                leaves[leave_count++] = ir_go(compiler, expr->span);
            }
            for (uint32_t i = 0; i < next_count; i++) {
                ir_lands(compiler, nexts[i]);
            }
        }
        for (uint32_t i = 0; i < leave_count; i++) {
            ir_lands(compiler, leaves[i]);
        }
        stack_push(compiler, gives);
        if (ways > 0) {
            ir_emit(compiler, KEST_IR_MEET, expr->type, ways, expr->type, gives,
                    expr->span);
        }

        compiler->depth--;
        compiler->local_count = names;
        compiler->next_slot = slots;
        break;
    }

    case KEST_EXPR_ARRAY: {
        const KestType *element =
            expr->type == NULL ? NULL : expr->type->element;
        // That many of something is the values themselves, one after another,
        // and nothing is built: they are already where they belong.
        if (expr->type != NULL && expr->type->tag == KEST_T_FIXED) {
            for (uint32_t i = 0; i < expr->array.count; i++) {
                compile_expr(compiler, expr->array.items[i]);
            }
            if (expr->array.count != 1) {
                ir_emit(compiler, KEST_IR_MAKE, expr->type,
                        (uint16_t)expr->array.count, expr->type,
                        value_slots(expr->type), expr->span);
            }
            break;
        }
        uint16_t slots = value_slots(element);
        for (uint32_t i = 0; i < expr->array.count; i++) {
            compile_expr(compiler, expr->array.items[i]);
        }
        stack_pop(compiler, (uint16_t)(expr->array.count * slots));
        stack_push(compiler, 1);
        uint32_t made = ir_emit(compiler, KEST_IR_ARRAY, expr->type,
                                (uint16_t)expr->array.count, expr->type, 1,
                                expr->span);
        ir_carries(compiler, made, (uint16_t)expr->array.count,
                   layout_of(compiler, element), 0);
        break;
    }
    }
}

// What an expression left on the stack against what its type says it is. The
// compiler adds this up as it goes and reads it once, as `stack_high_water`,
// to say how much operand stack a body needs — and it is the same count the
// machine moves by, so the two parting company is either a body sized for a
// stack it does not use or a value read at the wrong width. D808 was the
// first, over a byte literal counted twice; D809 was the second, an element
// read at the width of the optional it was about to become. Held at every
// expression rather than measured afterwards, because the expression is what
// names it. See D809.
static void hold_width(Compiler *compiler, const KestExpr *expr,
                       uint16_t before) {
    if (compiler->out_of_memory) {
        return;
    }
    int32_t grew = (int32_t)compiler->stack_depth - (int32_t)before;
    uint16_t want = value_slots(expr->type);
    if (grew == (int32_t)want) {
        return;
    }
    refuse(compiler, expr->span, "K0505",
           "this leaves %d slot(s) of stack and is %u wide", grew, want);
    kest_diags_fault(compiler->program->diags,
                     "the compiler's count of the stack and the width of a "
                     "value disagree");
}

static void compile_expr(Compiler *compiler, const KestExpr *expr) {
    if (expr == NULL || compiler->out_of_memory) {
        return;
    }
    uint16_t before = compiler->stack_depth;
    // A truth settled where it is written, which is a question about the
    // count of a walk written out once a turn, is that truth. See D1231.
    bool settled;
    if (known_truth(compiler, expr, &settled)) {
        stack_push(compiler, 1);
        ir_emit(compiler, settled ? KEST_IR_TRUE : KEST_IR_FALSE, expr->type,
                0, expr->type, 1, expr->span);
    } else {
        compile_expr_kind(compiler, expr);
    }
    // The checker decided this value stands where an optional is wanted, so
    // the tag goes after it.
    if (expr->wrapped) {
        stack_push(compiler, 1);
        ir_emit(compiler, KEST_IR_TRUE, truth_type(compiler), 0,
                truth_type(compiler), 1, expr->span);
        ir_emit(compiler, KEST_IR_MAKE, expr->type, 2, expr->type,
                value_slots(expr->type), expr->span);
    }
    hold_width(compiler, expr, before);
}

static void compile_block(Compiler *compiler, const KestBlock *block);

// The operation that left `value` behind, or NULL.
static const KestIrOp *made_by(const KestIrBody *body, KestIrRef value) {
    if (value == KEST_IR_NONE) {
        return NULL;
    }
    for (uint32_t i = body->op_count; i > 0; i--) {
        if (body->ops[i - 1].dest == value) {
            return &body->ops[i - 1];
        }
    }
    return NULL;
}

// The slot a value was read out of, when it is one slot read from the frame,
// or -1.
static int32_t read_out_of(const KestIrBody *body, KestIrRef value) {
    const KestIrOp *op = made_by(body, value);
    if (op == NULL || op->kind != KEST_IR_LOAD ||
        op->place == KEST_IR_NO_PLACE) {
        return -1;
    }
    const KestIrPlace *place = &body->places[op->place];
    if (place->kind != KEST_IR_PLACE_SLOT || place->slots != 1) {
        return -1;
    }
    return place->slot;
}

// Whether a call is to a body that keeps runs: compiled already, and doing
// nothing that could make an array shorter. A body compiled later, and the
// body being compiled, are not known yet and are taken as not. See D1188.
static bool calls_a_keeper(const Compiler *compiler, const KestIrOp *op) {
    const KestModule *module = compiler->module;
    if (module == NULL || op->imm[0] >= module->count) {
        return false;
    }
    const KestChunk *callee = module->functions[op->imm[0]];
    return callee != NULL && callee != compiler->compiling &&
           callee->keeps_runs;
}

// Whether the body just written keeps runs, for the calls of it written
// after it.
static bool keeps_runs(const Compiler *compiler) {
    const KestIrBody *body = compiler->body;
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        switch ((KestIrKind)op->kind) {
        case KEST_IR_CALL:
            if (!calls_a_keeper(compiler, op)) {
                return false;
            }
            break;
        case KEST_IR_CALL_VALUE:
        case KEST_IR_CALL_HOST:
        case KEST_IR_FIT:
        case KEST_IR_FIT_TEXT:
        case KEST_IR_POP_LAST:
        case KEST_IR_TAKE:
        case KEST_IR_CLEAR:
        case KEST_IR_REGION_CLOSE:
            return false;
        default:
            break;
        }
    }
    return true;
}

// Whether nothing in the operations from `from` on can make an array shorter
// or take one away, or store into the slot `counter` is in: no call but to a
// body that keeps its arrays, since a body handed one may take from it; no
// taking, emptying or resizing; and no working memory closed. Growing an
// array is not among them: the bytes may move, and every read asks the
// handle where they are. See D1187 and D1188.
static bool walk_keeps(const Compiler *compiler, uint32_t from,
                       uint16_t counter) {
    const KestIrBody *body = compiler->body;
    for (uint32_t i = from; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind == KEST_IR_CALL && !calls_a_keeper(compiler, op)) {
            return false;
        }
        switch ((KestIrKind)op->kind) {
        case KEST_IR_CALL_VALUE:
        case KEST_IR_CALL_HOST:
        case KEST_IR_FIT:
        case KEST_IR_FIT_TEXT:
        case KEST_IR_POP_LAST:
        case KEST_IR_TAKE:
        case KEST_IR_CLEAR:
        case KEST_IR_REGION_CLOSE:
            return false;
        default:
            break;
        }
        if (op->kind == KEST_IR_PUT && op->place != KEST_IR_NO_PLACE) {
            const KestIrPlace *place = &body->places[op->place];
            if (place->kind == KEST_IR_PLACE_RUN ||
                (place->kind == KEST_IR_PLACE_SLOT &&
                 counter >= place->slot &&
                 counter < place->slot + place->slots)) {
                return false;
            }
        }
    }
    return true;
}

// Whether anything from `from` on stores into `slot`.
static bool stored_into(const Compiler *compiler, uint32_t from,
                        uint16_t slot) {
    const KestIrBody *body = compiler->body;
    for (uint32_t i = from; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind != KEST_IR_PUT || op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_SLOT && slot >= place->slot &&
            slot < place->slot + place->slots) {
            return true;
        }
    }
    return false;
}

// Every element read or written from `from` on at the count in `counter`, of
// an array held in a slot nothing from there on stores into, in a walk that
// counts from nought or more up to what `limit` holds and keeps its arrays:
// inside the array outright when the array is the one in `measured`, whose
// length the limit is (D1187), and inside it whenever that array is at least
// as long as the limit otherwise, which is asked once where the walk begins
// (D1189). `limit` is -1 for a walk whose limit is the length of `measured`
// by construction.
static void prove_walk(Compiler *compiler, uint32_t from, int32_t measured,
                       uint16_t counter, int32_t limit) {
    KestIrBody *body = compiler->body;
    if (body == NULL || compiler->ir->out_of_memory ||
        !walk_keeps(compiler, from, counter)) {
        return;
    }
    for (uint32_t i = from; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->kind != KEST_IR_LOAD && op->kind != KEST_IR_PUT &&
             op->kind != KEST_IR_ADDR) ||
            op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        KestIrPlace *place = &body->places[op->place];
        if (place->kind != KEST_IR_PLACE_ELEM || place->type == NULL ||
            read_out_of(body, place->index) != (int32_t)counter) {
            continue;
        }
        int32_t held = read_out_of(body, place->base);
        if (held < 0 || stored_into(compiler, from, (uint16_t)held)) {
            continue;
        }
        if (held == measured) {
            place->in_bounds = true;
        } else if (limit >= 0) {
            place->guarded = true;
            place->guard_held = (uint16_t)held;
            place->guard_counter = counter;
            place->guard_limit = (uint16_t)limit;
        }
    }
}

static Loop *open_loop(Compiler *compiler, KestSpan span) {
    if (compiler->loop_count == MAX_LOOPS) {
        refuse(compiler, span, "K0502", "loops nest more than %d deep",
               MAX_LOOPS);
        return NULL;
    }
    Loop *loop = &compiler->loops[compiler->loop_count++];
    loop->start = compiler->body->op_count;
    loop->deferred = compiler->defer_count;
    loop->regions = compiler->region_count;
    loop->break_count = 0;
    loop->continue_count = 0;
    return loop;
}

// The pad every `continue` lands on sits between the body and the step, which
// is why continuing runs the step rather than skipping it.
static void land_continues(Compiler *compiler, Loop *loop) {
    land_each(compiler, loop->continues, loop->continue_count);
}

// Where the loop ends: the test that let it be skipped and every `break` land
// here, whatever went back at the bottom.
static void land_exit(Compiler *compiler, Loop *loop, const Exits *exits) {
    land_exits(compiler, exits);
    for (uint32_t i = 0; i < loop->break_count; i++) {
        ir_lands(compiler, loop->breaks[i]);
    }
    compiler->loop_count--;
}

static void finish_loop(Compiler *compiler, Loop *loop, const Exits *exits,
                        KestSpan span) {
    ir_go_back(compiler, loop->start, span);
    land_exit(compiler, loop, exits);
}

static void close_loop(Compiler *compiler, Loop *loop, const Exits *exits,
                       KestSpan span) {
    land_continues(compiler, loop);
    finish_loop(compiler, loop, exits, span);
}

// What a walk keeps its place with. Every `for` in the language is this: a
// count in a slot, and either a limit beside it to count to or a store to look
// through. Which of the two is the only thing that differs between the five
// things a `for` can walk, so it is a field rather than four functions that
// agree because they were written in the same week.
typedef struct {
    uint16_t count;
    // Counting. The slot holding what the count is compared with.
    uint16_t limit;
    // What the count is, which says whether it has a sign.
    const KestType *counts;
    bool unsigned_count;
    // Looking. A store hands out slots that go dead, so there is no limit to
    // count to and the next live one is searched for.
    bool searching;
    uint16_t searched;
} Walk;

// The test that decides whether there is a first turn, written above the loop
// because every turn after it is decided at the bottom. Gives back where the
// way out is written, for `land_exit` to fill in.
static uint32_t open_walk(Compiler *compiler, Walk walk, KestSpan span) {
    if (walk.searching) {
        uint32_t at =
            ir_emit(compiler, KEST_IR_SEEK_FROM, NULL, 0, NULL, 0, span);
        ir_carries(compiler, at, walk.searched, walk.count, 0);
        return at;
    }

    stack_push(compiler, 1);
    load_slots(compiler, walk.count, 1, walk.counts, span);
    stack_push(compiler, 1);
    load_slots(compiler, walk.limit, 1, walk.counts, span);
    stack_pop(compiler, 1);
    ir_emit(compiler, KEST_IR_LT, walk.counts, 2, truth_type(compiler), 1,
            span);
    stack_pop(compiler, 1);
    return ir_ask(compiler, false, span);
}

// The turn: one instruction that counts or looks, decides, and goes back while
// there is another. `continue` lands on it, which is why continuing takes a
// turn rather than skipping one.
static void close_walk(Compiler *compiler, Loop *loop, uint32_t exit, Walk walk,
                       KestSpan span) {
    land_continues(compiler, loop);

    uint32_t at;
    if (walk.searching) {
        at = ir_emit(compiler, KEST_IR_SEEK_NEXT, NULL, 0, NULL, 0, span);
        ir_carries(compiler, at, walk.searched, walk.count, 0);
    } else {
        at = ir_emit(compiler, KEST_IR_NEXT, walk.counts, 0, NULL, 0, span);
        ir_carries(compiler, at, walk.count, walk.limit, 0);
    }
    ir_at(compiler, at)->target = loop->start;

    Exits exits = one_exit(compiler, exit);
    land_exit(compiler, loop, &exits);
}

// And what a statement leaves, which is nothing. A statement is where a value
// is dropped, stored or handed back, so the stack it stands on is the stack the
// next one stands on — every statement in this tree and in the library is
// compiled from nothing and leaves nothing. The count going under nothing on
// the way is the same fault seen earlier: what clamped at nought is a depth
// that is wrong and right again by the end. See D810.
static void hold_empty(Compiler *compiler, const KestStmt *stmt,
                       uint16_t before) {
    if (compiler->out_of_memory) {
        return;
    }
    if (compiler->lost_count) {
        compiler->lost_count = false;
        refuse(compiler, stmt->span,
               "K0505", "this takes more off the stack than it put on");
        kest_diags_fault(compiler->program->diags,
                         "the compiler's count of the stack went under "
                         "nothing");
        return;
    }
    if (compiler->stack_depth == before) {
        return;
    }
    refuse(compiler, stmt->span, "K0505",
           "this leaves the stack %u deep and a statement leaves it as it "
           "found it", compiler->stack_depth);
    kest_diags_fault(compiler->program->diags,
                     "the compiler's count of the stack and what a statement "
                     "is disagree");
}

// `for i in from..to`, counted in a slot nobody can name, turn by turn.
static void compile_count(Compiler *compiler, const KestStmt *stmt) {
    uint16_t names = compiler->local_count;
    uint16_t slots = compiler->next_slot;
    compiler->depth++;

    uint16_t end_slot = reserve_slot(compiler, 1);
    compile_expr(compiler, stmt->each->until);
    // Whether the end is how long an array held in a slot is, and
    // which slot: `0..len(xs)` counts through `xs`. See D1187.
    int32_t measured = -1;
    if (compiler->body != NULL && compiler->body->op_count >= 2 &&
        compiler->body->ops[compiler->body->op_count - 1].kind ==
            KEST_IR_LEN &&
        compiler->body->ops[compiler->body->op_count - 1].arg_count ==
            1) {
        const KestIrOp *len =
            &compiler->body->ops[compiler->body->op_count - 1];
        measured = read_out_of(
            compiler->body, compiler->body->args[len->first_arg]);
    }
    stack_pop(compiler, 1);
    store_slots(compiler, end_slot, 1,
                stmt->each->until->type, stmt->span);

    // The loop's own count stays where nobody can reach it and the
    // name is a copy of it, the same way a walk of an array works, so
    // assigning to that name cannot make the count go wrong.
    uint16_t index_slot = reserve_slot(compiler, 1);
    compile_expr(compiler, stmt->each->sequence);
    stack_pop(compiler, 1);
    store_slots(compiler, index_slot, 1, whole_type(compiler),
                stmt->span);

    Walk walk = {index_slot, end_slot, stmt->each->sequence->type,
                 kest_is_unsigned(stmt->each->sequence->type), false,
                 0};
    uint32_t exit = open_walk(compiler, walk, stmt->span);

    Loop *loop = open_loop(compiler, stmt->span);
    if (loop == NULL) {
        return;
    }

    // Where nothing assigns to it there is nothing to go wrong, and
    // the name is the count rather than a copy of it: a load and a
    // store off every turn, which for a loop whose body is small is
    // most of what the turn was. The checker is what knows, because
    // it is what resolved the name. See D866.
    if (stmt->each->name_written) {
        uint16_t counter = declare_local(compiler, stmt->each->name,
                                         stmt->each->sequence->type);
        stack_push(compiler, 1);
        load_slots(compiler, index_slot, 1, NULL, stmt->span);
        stack_pop(compiler, 1);
        store_slots(compiler, counter, 1, NULL, stmt->span);
    } else {
        bind_local(compiler, stmt->each->name, index_slot, 1);
    }

    compile_block(compiler, &stmt->each->body);
    // Counting from nought or more to a limit, by a count nothing names
    // but the loop: every element read at it is inside its array when
    // the array is as long as the limit -- which it is outright where
    // the limit is its length -- if nothing in the walk made it
    // shorter. See D1187 and D1189.
    if (!stmt->each->name_written &&
        written_index(compiler, stmt->each->sequence) >= 0) {
        prove_walk(compiler, loop->start, measured, index_slot,
                   end_slot);
    }
    close_walk(compiler, loop, exit, walk, stmt->span);

    compiler->depth--;
    compiler->local_count = names;
    compiler->next_slot = slots;
}

// A walk written out once a turn is at most this many turns. Four is the
// neighbours of a cell, which is the walk this is for. See D1231.
#define MOST_UNROLLED 4
// And its body at most this many operations as a walk, so what is written
// out stays a small part of what the body around it is.
#define MOST_UNROLLED_OPS 400

// Where a body's writing stood, so that a walk compiled one way can be taken
// back and compiled the other: everything in a body is a list written onto the
// end, so taking back is setting the counts back.
typedef struct {
    uint32_t ops;
    uint32_t args;
    uint32_t values;
    uint32_t places;
    uint32_t names;
    uint32_t constants;
    uint32_t folded;
    uint32_t folded_slots;
    uint32_t stack_values;
    uint32_t diags;
    uint32_t errors;
    uint16_t local_count;
    uint16_t next_slot;
    uint16_t stack_depth;
    uint32_t depth;
    uint32_t loop_count;
} Written;

static Written written_so_far(const Compiler *compiler) {
    const KestIrBody *body = compiler->body;
    return (Written){body->op_count,     body->arg_count,
                     body->value_count,  body->place_count,
                     body->name_count,   body->constant_count,
                     body->folded,       body->folded_slots,
                     compiler->value_count,
                     compiler->program->diags->count,
                     compiler->program->diags->error_count,
                     compiler->local_count, compiler->next_slot,
                     compiler->stack_depth, compiler->depth,
                     compiler->loop_count};
}

static void take_back(Compiler *compiler, const Written *to) {
    KestIrBody *body = compiler->body;
    body->op_count = to->ops;
    body->arg_count = to->args;
    body->value_count = to->values;
    body->place_count = to->places;
    body->name_count = to->names;
    body->constant_count = to->constants;
    body->folded = to->folded;
    body->folded_slots = to->folded_slots;
    compiler->value_count = to->stack_values;
    compiler->local_count = to->local_count;
    compiler->next_slot = to->next_slot;
    compiler->stack_depth = to->stack_depth;
    compiler->depth = to->depth;
    compiler->loop_count = to->loop_count;
}

// What a promise about a body reads of a run of its operations: every effect
// any of them has, and every body and door any of them calls. Two runs that
// read the same here are the same to every promise, so a walk written out
// once a turn is kept only where it reads the same as the walk it was. See
// D1231.
typedef struct {
    uint16_t effects;
    uint32_t count;
    bool overflowed;
    uint32_t called[64];
} Reaches;

static void reaches_of(const KestIrBody *body, uint32_t from, uint32_t to,
                       Reaches *out) {
    memset(out, 0, sizeof *out);
    for (uint32_t i = from; i < to; i++) {
        const KestIrOp *op = &body->ops[i];
        out->effects |= op->effects;
        if (op->kind != KEST_IR_CALL && op->kind != KEST_IR_CALL_VALUE &&
            op->kind != KEST_IR_CALL_HOST) {
            continue;
        }
        uint32_t named = ((uint32_t)op->kind << 16) |
                         (op->kind == KEST_IR_CALL_VALUE ? 0 : op->imm[0]);
        bool had = false;
        for (uint32_t c = 0; c < out->count; c++) {
            had = had || out->called[c] == named;
        }
        if (had) {
            continue;
        }
        if (out->count == sizeof out->called / sizeof out->called[0]) {
            out->overflowed = true;
            continue;
        }
        out->called[out->count++] = named;
    }
}

static bool reach_alike(const Reaches *one, const Reaches *two) {
    if (one->overflowed || two->overflowed || one->effects != two->effects ||
        one->count != two->count) {
        return false;
    }
    for (uint32_t i = 0; i < one->count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < two->count; j++) {
            found = found || one->called[i] == two->called[j];
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// The count's first value and how many turns there are, when both are
// written where the walk is and the turns are few. See D1231.
static bool counted_turns(Compiler *compiler, const KestStmt *stmt,
                          int64_t *first, uint32_t *turns) {
    const KestType *counts = stmt->each->sequence->type;
    if (compiler->body == NULL || stmt->each->name_written || counts == NULL ||
        counts->tag != KEST_T_INT || counts->slots > 1) {
        return false;
    }
    bool (*was)(void *, const char *, uint32_t, const KestValue **,
                uint32_t *) = compiler->program->held_name;
    void *was_context = compiler->program->held_context;
    compiler->program->held_name = held_by_body;
    compiler->program->held_context = compiler;
    KestValue from = {0};
    KestValue to = {0};
    bool known = fold_quietly(compiler, stmt->each->sequence, &from) &&
                 fold_quietly(compiler, stmt->each->until, &to);
    compiler->program->held_name = was;
    compiler->program->held_context = was_context;
    if (!known) {
        return false;
    }
    // Compared as the count's own kind of number, which is what the walk's
    // test does.
    if (kest_is_unsigned(counts)) {
        uint64_t low = (uint64_t)from.integer;
        uint64_t high = (uint64_t)to.integer;
        if (high <= low || high - low > MOST_UNROLLED) {
            return false;
        }
        *turns = (uint32_t)(high - low);
    } else {
        if (to.integer <= from.integer ||
            (uint64_t)to.integer - (uint64_t)from.integer > MOST_UNROLLED) {
            return false;
        }
        *turns = (uint32_t)((uint64_t)to.integer - (uint64_t)from.integer);
    }
    *first = from.integer;
    return true;
}

// The walk written out once a turn: the body as many times as there are
// turns, the count in each a value the body holds, a `continue` the end of
// its own turn and a `break` the end of the last. Nothing goes round, so
// nothing counts, tests or spends a step of a budget. See D1231.
static void compile_unrolled(Compiler *compiler, const KestStmt *stmt,
                             int64_t first, uint32_t turns) {
    uint16_t names = compiler->local_count;
    uint16_t slots = compiler->next_slot;
    compiler->depth++;
    bool (*was)(void *, const char *, uint32_t, const KestValue **,
                uint32_t *) = compiler->program->held_name;
    void *was_context = compiler->program->held_context;
    compiler->program->held_name = held_by_body;
    compiler->program->held_context = compiler;
    compiler->unrolling++;

    Loop *loop = open_loop(compiler, stmt->span);
    if (loop != NULL) {
        for (uint32_t turn = 0; turn < turns; turn++) {
            KestValue *count =
                KEST_ARENA_ARRAY(compiler->ir->arena, KestValue, 1);
            if (count == NULL) {
                compiler->out_of_memory = true;
                break;
            }
            count->integer = (int64_t)((uint64_t)first + turn);
            uint16_t turn_names = compiler->local_count;
            compiler->depth++;
            hold_local(compiler, stmt->each->name, stmt->each->sequence->type,
                       count, 1);
            compile_block(compiler, &stmt->each->body);
            compiler->depth--;
            compiler->local_count = turn_names;
            land_continues(compiler, loop);
            loop->continue_count = 0;
        }
        for (uint32_t i = 0; i < loop->break_count; i++) {
            ir_lands(compiler, loop->breaks[i]);
        }
        compiler->loop_count--;
    }

    compiler->unrolling--;
    compiler->program->held_name = was;
    compiler->program->held_context = was_context;
    compiler->depth--;
    compiler->local_count = names;
    compiler->next_slot = slots;
}

// `for i in from..to`. Compiled as a walk, and where the turns are few and
// written where the walk is, compiled again written out once a turn and kept
// that way if it says nothing the walk did not and reads the same to every
// promise. `KEST_NOOPT` keeps every walk a walk, which is what the gate and
// the fuzzer hold the written-out one to. See D1231.
static void compile_counting(Compiler *compiler, const KestStmt *stmt) {
    static int off = -1;
    int64_t first = 0;
    uint32_t turns = 0;
    if (kest_ir_asked_off("KEST_NOOPT", &off) ||
        !counted_turns(compiler, stmt, &first, &turns)) {
        compile_count(compiler, stmt);
        return;
    }
    Written before = written_so_far(compiler);
    compile_count(compiler, stmt);
    uint32_t walked_to = compiler->body->op_count;
    if (compiler->out_of_memory ||
        compiler->program->diags->count != before.diags ||
        walked_to - before.ops > MOST_UNROLLED_OPS) {
        return;
    }
    Reaches walked;
    reaches_of(compiler->body, before.ops, walked_to, &walked);
    uint16_t high_water = compiler->slot_high_water;

    take_back(compiler, &before);
    compile_unrolled(compiler, stmt, first, turns);
    Reaches unrolled;
    reaches_of(compiler->body, before.ops, compiler->body->op_count,
               &unrolled);
    if (!compiler->out_of_memory &&
        compiler->program->diags->count == before.diags &&
        reach_alike(&walked, &unrolled)) {
        return;
    }
    take_back(compiler, &before);
    compiler->program->diags->count = before.diags;
    compiler->program->diags->error_count = before.errors;
    compile_count(compiler, stmt);
    if (compiler->slot_high_water < high_water) {
        compiler->slot_high_water = high_water;
    }
}

static void compile_stmt_kind(Compiler *compiler, const KestStmt *stmt);

static void compile_stmt(Compiler *compiler, const KestStmt *stmt) {
    if (compiler->out_of_memory) {
        return;
    }
    uint16_t before = compiler->stack_depth;
    compile_stmt_kind(compiler, stmt);
    hold_empty(compiler, stmt, before);
}

static void compile_stmt_kind(Compiler *compiler, const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_LET: {
        const KestType *type = stmt->let.value == NULL
                                   ? NULL
                                   : stmt->let.value->type;
        uint16_t size = value_slots(type);
        // A value worked out where it is written whose name nothing assigns
        // to is a value the chunk holds. A run of four numbers in a body was
        // four pushes and a store on every call, and the same run written as
        // a `const` was read where it stood: the reference says a run of
        // numbers indexed by one is a value a frame does not pay for, and
        // where it was written decided whether that was true. Sixteen for the
        // reason every other fold stops there, which is that a value wider
        // than that is a table rather than a value. See D887.
        if (!stmt->let.name_written && stmt->let.value != NULL &&
            compiler->body != NULL && size > 0 && size <= 16) {
            KestValue held[16];
            const char *why = NULL;
            if (kest_fold_const(compiler->program, stmt->let.value, held, size,
                                &why, NULL) == size) {
                KestValue *kept = KEST_ARENA_ARRAY(compiler->ir->arena,
                                                   KestValue, size);
                if (kept == NULL) {
                    compiler->out_of_memory = true;
                    break;
                }
                memcpy(kept, held, sizeof(KestValue) * size);
                counted_fold(compiler, size);
                hold_local(compiler, stmt->let.name, type, kept, size);
                break;
            }
        }
        compile_expr(compiler, stmt->let.value);
        uint16_t slot = declare_local(compiler, stmt->let.name, type);
        stack_pop(compiler, size);
        store_slots(compiler, slot, size == 0 ? 1 : size, type, stmt->span);
        break;
    }

    case KEST_STMT_ASSIGN: {
        const KestExpr *target = stmt->assign.target;
        uint16_t size = value_slots(target->type);

        // Writing one of that many, where the run is in slots. The index is
        // worked out while running, so it goes on the stack under the value.
        uint16_t written_slot = 0;
        uint16_t written_size = 0;
        bool at_a_slot = resolve_place(compiler, target, &written_slot,
                                       &written_size);
        if (!at_a_slot && target->kind == KEST_EXPR_INDEX &&
            target->index.object->type != NULL &&
            target->index.object->type->tag == KEST_T_FIXED) {
            const KestType *run = target->index.object->type;
            uint16_t base = 0;
            uint16_t run_size = 0;
            if (stmt->assign.op == KEST_TOK_EQ &&
                resolve_place(compiler, target->index.object, &base,
                              &run_size)) {
                compile_expr(compiler, target->index.index);
                uint32_t where = run_place(compiler, base, size,
                                           (uint16_t)run->count,
                                           target->type, stmt->span);
                compile_expr(compiler, stmt->assign.value);
                stack_pop(compiler, (uint16_t)(size + 1));
                uint32_t at = ir_emit(compiler, KEST_IR_PUT, target->type, 2,
                                      NULL, 0, stmt->span);
                ir_place_of(compiler, at, where);
                break;
            }
        }

        uint16_t slot = 0;
        uint16_t place_size = 0;
        bool in_slots = resolve_place(compiler, target, &slot, &place_size);

        uint16_t offset = 0;
        if (!in_slots) {
            // Held apart: the array and the index rather than an address
            // worked out from them, because what is between here and the
            // store may move the block. See D931.
            compiler->place_apart = true;
            bool made = compile_address(compiler, target, &offset);
            compiler->place_apart = false;
            if (!made) {
                fault(compiler, target->span,
                      "this assigns to something that is not a place");
                break;
            }
        }
        if (stmt->assign.op != KEST_TOK_EQ) {
            // The operator applies to what is there, so the target is read
            // before it is written. Through an address that means keeping a
            // second copy of it, because storing consumes one.
            if (in_slots) {
                stack_push(compiler, 1);
                load_slots(compiler, slot, 1, target->type, stmt->span);
            } else {
                // The place stays where it is and the read is made from it,
                // because the write below wants it again.
                stack_push(compiler, 1);
                uint32_t where = elem_place(compiler, ir_top(compiler, 1),
                                            ir_top(compiler, 0), offset,
                                            target->type, stmt->span);
                uint32_t at = ir_emit(compiler, KEST_IR_LOAD, target->type, 0,
                                      target->type, 1, stmt->span);
                ir_place_of(compiler, at, where);
            }
        }

        compile_expr(compiler, stmt->assign.value);

        if (stmt->assign.op != KEST_TOK_EQ) {
            stack_pop(compiler, 1);
            KestIrKind does = KEST_IR_DIV;
            switch (stmt->assign.op) {
            case KEST_TOK_PLUSEQ:
                does = KEST_IR_ADD;
                break;
            case KEST_TOK_MINUSEQ:
                does = KEST_IR_SUB;
                break;
            case KEST_TOK_STAREQ:
                does = KEST_IR_MUL;
                break;
            default:
                break;
            }
            ir_emit(compiler, does, target->type, 2, target->type, 1,
                    stmt->span);
            if (stmt->assign.op != KEST_TOK_SLASHEQ ||
                dividing_can_leave(target->type)) {
                ir_narrow(compiler, target->type, stmt->span);
            }
        }

        if (in_slots) {
            stack_pop(compiler, size);
            store_slots(compiler, slot, size, target->type, stmt->span);
        } else {
            stack_pop(compiler, (uint16_t)(size + 2));
            uint32_t where = elem_place(compiler, ir_top(compiler, 2),
                                        ir_top(compiler, 1), offset,
                                        target->type, stmt->span);
            uint32_t at = ir_emit(compiler, KEST_IR_PUT, target->type, 3, NULL,
                                  0, stmt->span);
            ir_place_of(compiler, at, where);
        }
        break;
    }

    case KEST_STMT_EXPR:
        compile_expr(compiler, stmt->value);
        // A call that returns nothing left nothing behind to discard.
        {
            uint16_t size = stmt->value == NULL
                                ? 0
                                : value_slots(stmt->value->type);
            if (size > 0) {
                stack_pop(compiler, size);
                ir_emit(compiler, KEST_IR_DROP,
                        stmt->value == NULL ? NULL : stmt->value->type, 1,
                        NULL, 0, stmt->span);
            }
        }
        break;

    case KEST_STMT_WHILE: {
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        bool opening = stmt->loop.binding.length > 0;
        if (opening) {
            compiler->depth++;
        }
        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }
        Exits exit = compile_condition(compiler, stmt->loop.condition, opening);

        // `while let` leaves what the optional held below the tag the jump
        // consumed. The turn that ran binds it; the turn that stopped drops
        // it, which is why the way out is not where a `break` lands.
        uint16_t held = 0;
        KestIrRef bound = KEST_IR_NONE;
        if (opening) {
            const KestType *optional = stmt->loop.condition->type;
            held = (uint16_t)(value_slots(optional) - 1);
            uint16_t slot = declare_local(
                compiler, stmt->loop.binding,
                optional == NULL ? NULL : optional->element);
            bound = ir_top(compiler, 0);
            stack_pop(compiler, held);
            store_slots(compiler, slot, held,
                        optional == NULL ? NULL : optional->element,
                        stmt->span);
        }

        compile_block(compiler, &stmt->loop.body);

        if (held == 0) {
            close_loop(compiler, loop, &exit, stmt->span);
        } else {
            land_continues(compiler, loop);
            ir_go_back(compiler, loop->start, stmt->span);
            land_exits(compiler, &exit);
            ir_leaves(compiler, bound);
            ir_emit(compiler, KEST_IR_DROP, NULL, 1, NULL, 0, stmt->span);
            for (uint32_t i = 0; i < loop->break_count; i++) {
                ir_lands(compiler, loop->breaks[i]);
            }
            compiler->loop_count--;
        }

        if (opening) {
            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
        }
        break;
    }

    case KEST_STMT_FOR: {
        // `for x in a` is a walk written here rather than in the parser, so
        // the counter and the thing being walked sit in slots nobody can name
        // or assign to.
        // `for i in from..to` counts. The end is worked out once and kept in
        // a slot nobody can name, so a call in it happens once rather than
        // every turn.
        if (stmt->each->until != NULL) {
            compile_counting(compiler, stmt);
            break;
        }

        const KestType *sequence = stmt->each->sequence->type;
        bool over_store = sequence != NULL && sequence->tag == KEST_T_STORE;
        bool over_bits = sequence != NULL && sequence->tag == KEST_T_FLAGS;
        bool over_run = sequence != NULL && sequence->tag == KEST_T_FIXED;
        bool over_text = sequence != NULL && sequence->tag == KEST_T_TEXT;
        if (sequence == NULL ||
            (sequence->tag != KEST_T_ARRAY && !over_store && !over_bits &&
             !over_run && !over_text)) {
            fault(compiler, stmt->span,
                  "this walks something there is no walk for");
            break;
        }

        // That many of something is a value, so the walk is over a copy of
        // it. Walking it where it stands would let a write to it in the body
        // change what the walk reads, and D053 says the name is what the
        // element was when the turn began.
        if (over_run) {
            uint16_t stride = value_slots(sequence->element);
            uint16_t names = compiler->local_count;
            uint16_t slots = compiler->next_slot;
            compiler->depth++;

            uint16_t run_slot = reserve_slot(compiler, sequence->slots);
            compile_expr(compiler, stmt->each->sequence);
            stack_pop(compiler, sequence->slots);
            store_slots(compiler, run_slot, sequence->slots, NULL, stmt->span);

            uint16_t index_slot = reserve_slot(compiler, 1);
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler), stmt->span);
            stack_pop(compiler, 1);
            store_slots(compiler, index_slot, 1, whole_type(compiler),
                        stmt->span);

            // How many there are is written in the program, and it goes in a
            // slot beside the count anyway: a turn is then the one
            // instruction that counts and tests, the same as every other walk.
            uint16_t limit_slot = reserve_slot(compiler, 1);
            KestValue how_many = {0};
            how_many.integer = sequence->count;
            emit_constant(compiler, how_many, KEST_CONST_INT, whole_type(compiler), stmt->span);
            stack_pop(compiler, 1);
            store_slots(compiler, limit_slot, 1, whole_type(compiler),
                        stmt->span);

            Walk walk = {index_slot, limit_slot, whole_type(compiler), false,
                         false, 0};
            uint32_t exit = open_walk(compiler, walk, stmt->span);

            Loop *loop = open_loop(compiler, stmt->span);
            if (loop == NULL) {
                break;
            }

            if (stmt->each->index.length > 0) {
                if (stmt->each->index_written) {
                    uint16_t named =
                        declare_local(compiler, stmt->each->index, NULL);
                    stack_push(compiler, 1);
                    load_slots(compiler, index_slot, 1, whole_type(compiler),
                               stmt->span);
                    stack_pop(compiler, 1);
                    store_slots(compiler, named, 1, whole_type(compiler),
                                stmt->span);
                } else {
                    bind_local(compiler, stmt->each->index, index_slot, 1);
                }
            }

            stack_push(compiler, 1);
            load_slots(compiler, index_slot, 1, whole_type(compiler),
                       stmt->span);
            stack_pop(compiler, 1);
            stack_push(compiler, stride);
            uint32_t where = run_place(compiler, run_slot, stride,
                                       (uint16_t)sequence->count,
                                       sequence->element, stmt->span);
            uint32_t one = ir_emit(compiler, KEST_IR_LOAD, sequence->element, 1,
                                   sequence->element, stride, stmt->span);
            ir_place_of(compiler, one, where);

            uint16_t held =
                declare_local(compiler, stmt->each->name, sequence->element);
            stack_pop(compiler, stride);
            store_slots(compiler, held, stride, sequence->element,
                        stmt->span);

            compile_block(compiler, &stmt->each->body);
            close_walk(compiler, loop, exit, walk, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }
        uint16_t stride = over_store || over_bits || over_text
                              ? 1
                              : value_slots(sequence->element);

        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        compiler->depth++;

        // How wide the thing being walked is: a piece of text is two slots
        // and everything else a walk goes over is one. See D964.
        uint16_t walked = value_slots(sequence);
        uint16_t walked_slot = reserve_slot(compiler, walked);
        uint16_t index_slot = reserve_slot(compiler, 1);

        compile_expr(compiler, stmt->each->sequence);
        stack_pop(compiler, walked);
        store_slots(compiler, walked_slot, walked, sequence, stmt->span);

        KestValue zero = {0};
        emit_constant(compiler, zero, KEST_CONST_INT, whole_type(compiler), stmt->span);
        stack_pop(compiler, 1);
        store_slots(compiler, index_slot, 1, whole_type(compiler),
                    stmt->span);

        // What there is to walk, once. For an array that is how long it is:
        // a walk is over what it held when it began, so the body cannot
        // lengthen what it is walking by pushing to it. For a set of bits it
        // is how many names the set declares, which is known here. Either way
        // the limit is a slot beside the count, which is what makes a turn one
        // instruction. See D094.
        bool counted = !over_store;
        uint16_t limit_slot = 0;
        if (counted) {
            limit_slot = reserve_slot(compiler, 1);
            if (over_bits) {
                KestValue names_count = {0};
                names_count.integer = (int64_t)sequence->case_count;
                emit_constant(compiler, names_count, KEST_CONST_INT, whole_type(compiler),
                              stmt->span);
            } else {
                stack_push(compiler, walked);
                load_slots(compiler, walked_slot, walked, sequence,
                           stmt->span);
                stack_pop(compiler, walked);
                stack_push(compiler, 1);
                ir_emit(compiler,
                        over_text ? KEST_IR_TEXT_LEN : KEST_IR_LEN, sequence,
                        1, whole_type(compiler), 1, stmt->span);
            }
            stack_pop(compiler, 1);
            store_slots(compiler, limit_slot, 1, whole_type(compiler),
                        stmt->span);
        }

        Walk walk = {index_slot, limit_slot, whole_type(compiler), false,
                     !counted, walked_slot};
        uint32_t before = open_walk(compiler, walk, stmt->span);

        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }

        uint32_t exit = before;

        // The loop's own counter stays where nobody can reach it, and the
        // name the author asked for is a copy of it, so assigning to that
        // name cannot make the walk go wrong — unless nothing assigns to it,
        // when the name is the counter. See D866.
        if (stmt->each->index.length > 0) {
            if (stmt->each->index_written) {
                uint16_t named =
                    declare_local(compiler, stmt->each->index, NULL);
                stack_push(compiler, 1);
                load_slots(compiler, index_slot, 1, whole_type(compiler),
                           stmt->span);
                stack_pop(compiler, 1);
                store_slots(compiler, named, 1, whole_type(compiler),
                            stmt->span);
            } else {
                bind_local(compiler, stmt->each->index, index_slot, 1);
            }
        }

        uint32_t absent = 0;
        if (over_bits) {
            // The flag this turn is about, which is the bit at the counter.
            KestValue one = {0};
            one.integer = 1;
            emit_constant(compiler, one, KEST_CONST_INT, whole_type(compiler),
                          stmt->span);
            stack_push(compiler, 1);
            load_slots(compiler, index_slot, 1, whole_type(compiler),
                       stmt->span);
            stack_pop(compiler, 1);
            ir_emit(compiler, KEST_IR_SHL, whole_type(compiler), 2,
                    whole_type(compiler), 1, stmt->span);

            // It goes into the name first, so a bit that is not there leaves
            // nothing on the stack to clean up on the way past.
            uint16_t held = declare_local(compiler, stmt->each->name, sequence);
            stack_pop(compiler, 1);
            store_slots(compiler, held, 1, sequence, stmt->span);

            stack_push(compiler, 1);
            load_slots(compiler, held, 1, sequence, stmt->span);
            stack_push(compiler, 1);
            load_slots(compiler, walked_slot, 1, sequence, stmt->span);
            stack_pop(compiler, 1);
            ir_emit(compiler, KEST_IR_AND, sequence, 2, sequence, 1,
                    stmt->span);
            stack_pop(compiler, 1);
            absent = ir_ask(compiler, false, stmt->span);

            compile_block(compiler, &stmt->each->body);

            // A bit that is not set skips the body and lands on the step,
            // which is where `continue` lands too.
            ir_lands(compiler, absent);
            close_walk(compiler, loop, exit, walk, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }

        // A body that only ever reads fields of the element does not need the
        // element: where it is, is enough, and the fields it does not read are
        // never touched.
        //
        // And it must not be able to write what it is walking. A copy is what
        // the element was when the turn began; an address is what it is now,
        // and the two differ the moment the body writes the array. Nothing
        // here can tell whether a call would write it, so the rule is that
        // the body does not name the thing being walked at all. See D053.
        const KestExpr *root = stmt->each->sequence;
        while (root != NULL && (root->kind == KEST_EXPR_FIELD ||
                                root->kind == KEST_EXPR_INDEX)) {
            root = root->kind == KEST_EXPR_FIELD ? root->field.object
                                                 : root->index.object;
        }
        bool untouched =
            root != NULL && root->kind == KEST_EXPR_NAME &&
            reads_only_fields(compiler, &stmt->each->body,
                              span_text(compiler, root->span),
                              root->span.length, false) &&
            writes_no_arrays(compiler, &stmt->each->body);
        bool by_address =
            !over_store && untouched && sequence->element != NULL &&
            sequence->element->tag == KEST_T_STRUCT &&
            reads_only_fields(compiler, &stmt->each->body,
                              span_text(compiler, stmt->each->name),
                              stmt->each->name.length, true);

        // The byte the walk is on. It reads the two slots itself rather than
        // taking them off the stack, because the walk measured the text when
        // it began and nothing it does can move a byte.
        if (over_text) {
            stack_push(compiler, 1);
            uint32_t byte = ir_emit(compiler, KEST_IR_TEXT_IN, sequence, 0,
                                    whole_type(compiler), 1, stmt->span);
            ir_carries(compiler, byte, walked_slot, index_slot, 0);
        } else {
            stack_push(compiler, 1);
            load_slots(compiler, walked_slot, 1, sequence, stmt->span);
            stack_push(compiler, 1);
            load_slots(compiler, index_slot, 1, whole_type(compiler),
                       stmt->span);
            stack_pop(compiler, 2);
            stack_push(compiler, by_address ? 1 : stride);
        }
        if (over_text) {
            // Read already.
        } else if (over_store) {
            ir_emit(compiler, KEST_IR_STORE_REF, sequence, 2, NULL, 1,
                    stmt->span);
        } else if (by_address) {
            uint32_t where = elem_place(compiler, ir_top(compiler, 1),
                                        ir_top(compiler, 0), 0,
                                        sequence->element, stmt->span);
            uint32_t at = ir_emit(compiler, KEST_IR_ADDR, sequence->element, 2,
                                  NULL, 1, stmt->span);
            ir_place_of(compiler, at, where);
        } else {
            uint32_t where = elem_place(compiler, ir_top(compiler, 1),
                                        ir_top(compiler, 0), 0,
                                        sequence->element, stmt->span);
            uint32_t at = ir_emit(compiler, KEST_IR_LOAD, sequence->element, 2,
                                  sequence->element, stride, stmt->span);
            ir_place_of(compiler, at, where);
        }

        const KestType *bound =
            over_store || over_text ? NULL : sequence->element;
        uint16_t element_slot =
            declare_local(compiler, stmt->each->name, by_address ? NULL : bound);
        if (by_address) {
            Local *held = &compiler->locals[compiler->local_count - 1];
            held->is_address = true;
            // The name was written down as the local was declared, which is
            // before this was known. A slot that holds where the element is
            // says so to whatever asks the body about itself -- and what asks
            // is a host reading a stopped machine, which would otherwise be
            // handed an address as though it were a `Body`. See D1081.
            if (!compiler->ir->out_of_memory) {
                compiler->body->names[held->name_at].by_address = true;
            }
        }
        stack_pop(compiler, by_address ? 1 : stride);
        store_slots(compiler, element_slot, by_address ? 1 : stride,
                    by_address ? NULL : bound, stmt->span);

        compile_block(compiler, &stmt->each->body);
        // The element the walk reads each turn is inside the array it is
        // walking, which is held where nothing can name it, if nothing in
        // the walk made it shorter. See D1187.
        if (!over_store && !over_text && sequence->tag == KEST_T_ARRAY) {
            prove_walk(compiler, loop->start, walked_slot, index_slot, -1);
        }

        close_walk(compiler, loop, exit, walk, stmt->span);

        compiler->depth--;
        compiler->local_count = names;
        compiler->next_slot = slots;
        break;
    }

    case KEST_STMT_RETURN: {
        uint16_t size = 0;
        if (stmt->result != NULL) {
            compile_expr(compiler, stmt->result);
            size = value_slots(stmt->result->type);
        }
        // The answer is worked out first and then everything outstanding is
        // run, so what a deferred call sees is what the function decided —
        // and it works itself out above that answer, which is still on the
        // stack for the `return` to take. Counted the other way round, a body
        // that defers asked for its answer's width less room than it uses.
        // See D811.
        run_deferred(compiler, 0, stmt->span);
        close_regions(compiler, 0, true, stmt->span);
        stack_pop(compiler, size);
        uint32_t at = ir_emit(compiler, KEST_IR_GIVE,
                              stmt->result == NULL ? NULL : stmt->result->type,
                              size > 0 ? 1 : 0, NULL, 0, stmt->span);
        ir_carries(compiler, at, size, 0, 0);
        break;
    }

    case KEST_STMT_CONTINUE: {
        if (compiler->loop_count == 0) {
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        run_deferred(compiler, loop->deferred, stmt->span);
        close_regions(compiler, loop->regions, true, stmt->span);
        if (loop->continue_count == MAX_BREAKS) {
            refuse(compiler, stmt->span, "K0502",
                   "a loop holds at most %d continues", MAX_BREAKS);
            break;
        }
        loop->continues[loop->continue_count++] = ir_go(compiler, stmt->span);
        break;
    }

    case KEST_STMT_BREAK: {
        if (compiler->loop_count == 0) {
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        run_deferred(compiler, loop->deferred, stmt->span);
        close_regions(compiler, loop->regions, true, stmt->span);
        if (loop->break_count == MAX_BREAKS) {
            refuse(compiler, stmt->span, "K0502",
                   "a loop holds at most %d breaks", MAX_BREAKS);
            break;
        }
        loop->breaks[loop->break_count++] = ir_go(compiler, stmt->span);
        break;
    }

    case KEST_STMT_SCRATCH: {
        if (compiler->region_count == MAX_REGIONS) {
            refuse(compiler, stmt->span, "K0502",
                   "working-memory blocks nest more than %d deep",
                   MAX_REGIONS);
            break;
        }
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        uint16_t which = reserve_slot(compiler, 1);
        uint32_t opened = ir_emit(compiler, KEST_IR_REGION_OPEN, NULL, 0, NULL,
                                  0, stmt->span);
        // Which slot holds it, and where the names declared inside it start:
        // a value written into a name below that is a value kept past the
        // block, and the walk over the body reads both. See D966.
        ir_carries(compiler, opened, which, compiler->next_slot, 0);
        compiler->regions[compiler->region_count++] = which;
        compile_block(compiler, &stmt->block);
        close_regions(compiler, (uint16_t)(compiler->region_count - 1), false,
                      stmt->span);
        compiler->region_count--;
        compiler->local_count = names;
        compiler->next_slot = slots;
        break;
    }

    case KEST_STMT_BLOCK:
        compile_block(compiler, &stmt->block);
        break;

    case KEST_STMT_DEFER:
        if (compiler->defer_count == MAX_DEFERS) {
            refuse(compiler, stmt->span, "K0502",
                   "a function defers at most %d things", MAX_DEFERS);
            break;
        }
        compiler->deferred[compiler->defer_count++] = stmt->value;
        break;
    }
}

// What was deferred since `from`, in reverse: the last thing deferred is the
// first thing undone, which is what everyone means by it.
// Whether a statement leaves the block rather than falling off the end of it.
static bool leaves_early(const KestStmt *stmt) {
    return stmt->kind == KEST_STMT_RETURN || stmt->kind == KEST_STMT_BREAK ||
           stmt->kind == KEST_STMT_CONTINUE;
}

// Closing the working-memory blocks this leaves, innermost first. What is put
// back is the heap; what is not put back is anything the body worked out into
// a slot, which is why the answer is worked out before this runs.
static void close_regions(Compiler *compiler, uint16_t from, bool leaving,
                          KestSpan span) {
    for (uint16_t i = compiler->region_count; i > from; i--) {
        uint32_t at = ir_emit(compiler, KEST_IR_REGION_CLOSE, NULL, 0, NULL, 0,
                              span);
        // Whether this is the block ending or a way out of it. The one that
        // ends it is where the walk over the body stops counting; the ones a
        // `return`, a `break` or a `continue` write are the machine putting
        // the heap back on the way past, and the block is still open as far as
        // what may be kept goes. See D966.
        ir_carries(compiler, at, compiler->regions[i - 1], 0,
                   leaving ? 1 : 0);
    }
}

static void run_deferred(Compiler *compiler, uint16_t from, KestSpan span) {
    for (uint16_t i = compiler->defer_count; i > from; i--) {
        const KestExpr *call = compiler->deferred[i - 1];
        compile_expr(compiler, call);
        uint16_t left = value_slots(call->type);
        if (left > 0) {
            stack_pop(compiler, left);
            ir_emit(compiler, KEST_IR_DROP, call->type, 1, NULL, 0, span);
        }
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block) {
    uint16_t names = compiler->local_count;
    uint16_t slots = compiler->next_slot;
    uint16_t defers = compiler->defer_count;
    compiler->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        compile_stmt(compiler, block->items[i]);
    }
    // On the way out of the block, unless the block already left through a
    // `return`, a `break` or a `continue`, each of which ran them itself.
    if (block->count == 0 || !leaves_early(block->items[block->count - 1])) {
        run_deferred(compiler, defers,
                     block->count > 0 ? block->items[block->count - 1]->span
                                      : (KestSpan){0, 0});
    }
    compiler->defer_count = defers;
    compiler->depth--;
    // Dropping the scope frees its slots for the next one, which is why two
    // sibling blocks do not each widen the frame.
    compiler->local_count = names;
    compiler->next_slot = slots;
}

static uint32_t unit_index(const KestUnits *units, const KestUnitInfo *unit) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (&units->items[i] == unit) {
            return i;
        }
    }
    return 0;
}

// Starting a body, and finishing one. Everything a body keeps is set here
// rather than left over from the last one: slots are reused between functions,
// and a field nobody wrote is a field holding what the function before it had.
static bool open_body(Compiler *compiler, KestChunk *chunk,
                      const KestType *signature) {
    KestIrBody *body = kest_ir_body_begin(compiler->ir);
    body->symbol = chunk->name;
    body->source = chunk->source;
    body->declared = chunk->declared;
    body->signature = signature;
    // What the declaration crosses the boundary with, laid out here rather
    // than wherever a body first reached one: what a host can be handed is
    // what the program says it takes, and that is written in the declaration.
    // See D068.
    if (signature != NULL) {
        if (signature->result != NULL &&
            signature->result->tag != KEST_T_VOID) {
            layout_of(compiler, signature->result);
        }
        for (uint32_t p = 0; p < signature->param_count; p++) {
            layout_of(compiler, signature->params[p]);
        }
    }
    body->returns_value = chunk->returns_value;
    body->result_slots = chunk->result_slots;
    body->no_alloc = chunk->no_alloc;
    body->no_host = chunk->no_host;
    body->deterministic = chunk->deterministic;
    compiler->body = body;
    compiler->compiling = chunk;
    // The stack of values this walk keeps is in the bodies' arena too, so a
    // body that has been let go takes it with it: what is left over from the
    // last one is a pointer into memory that has been handed back.
    compiler->values = NULL;
    compiler->value_capacity = 0;
    compiler->value_count = 0;
    compiler->local_count = 0;
    compiler->next_slot = 0;
    compiler->slot_high_water = 0;
    compiler->stack_depth = 0;
    compiler->stack_high_water = 0;
    compiler->depth = 0;
    compiler->loop_count = 0;
    compiler->region_count = 0;
    return true;
}

static bool close_body(Compiler *compiler, const KestBlock *block,
                       KestSpan declared) {
    compiler->body->param_slots = compiler->next_slot;
    compile_block(compiler, block);
    // Every body ends by giving something back, so that nothing runs off the
    // end of one.
    uint32_t at = ir_emit(compiler, KEST_IR_GIVE, NULL, 0, NULL, 0, declared);
    ir_carries(compiler, at, 0, 0, 0);
    compiler->body->slot_count = compiler->slot_high_water;
    compiler->body->stack_needed = compiler->stack_high_water;
    // What a working-memory block keeps, followed over the whole body rather
    // than watched while it was written: the block is what says a value is
    // shorter-lived, and what it reaches is a question about the body. See
    // D966.
    KestSpan escaped = declared;
    const char *keeps = kest_ir_escapes(compiler->body, compiler->ir->arena,
                                        &escaped);
    if (keeps != NULL) {
        refuse(compiler, escaped, "K0507", "%s", keeps);
        kest_diags_suggest(compiler->program->diags,
                           "what a `scratch { }` block makes is gone when it "
                           "ends: copy out a number, or make the thing outside "
                           "the block");
    }
    if (compiler->compiling != NULL && !compiler->ir->out_of_memory) {
        compiler->compiling->keeps_runs = keeps_runs(compiler);
    }
    // Handed to the backend and let go. Nothing in it is read again, which is
    // what keeps one body's worth of memory alive rather than a program's.
    bool went = kest_ir_body_end(compiler->ir);
    compiler->body = NULL;
    if (!went) {
        compiler->out_of_memory = compiler->out_of_memory ||
                                  compiler->ir->out_of_memory;
    }
    return went;
}

// Two functions compiled under one name. Not a `fault` — that one takes a
// compiler, and this happens while the functions are being registered, before
// there is one — but the same kind of news, in the same words.
static void two_of_one_name(KestProgram *program, const char *symbol,
                            KestSpan where) {
    kest_diags_in(program->diags, program->source);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0505", where,
                   "two functions are compiled under `%s`, which the checker "
                   "allowed",
                   symbol);
    kest_diags_fault(program->diags,
                     "the two halves of the compiler disagree about what a "
                     "program is");
}

bool kest_compile(KestProgram *program, const KestUnits *units,
                  KestModule *module, KestIrProgram *ir) {
    Compiler compiler = {0};
    compiler.program = program;
    compiler.module = module;
    compiler.units = units;
    compiler.ir = ir;
    // The file that was named is the first one, and the whole of what it calls
    // itself is what a host has to be able to leave off: a name lives under
    // `a.math` and a host writes `twice`. The last part alone was enough while
    // that was where a name lived; it is not now. See D1039.
    if (units->count > 0) {
        module->alias = units->items[0].module;
    }

    // Every function in every file is registered before any body is emitted,
    // so a call can name one declared below it or in a file read later.
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        const KestUnit *unit = &units->items[u].unit;
        for (uint32_t i = 0; i < unit->count; i++) {
            const KestDecl *decl = unit->items[i];
            if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
                continue;
            }
            // Compiled under the symbol the checker gave it, which includes
            // what it takes, because two functions may share a name.
            KestSymbol *symbol =
                kest_symbol_at(program, program->source, decl->name);
            if (symbol == NULL || symbol->type->symbol == NULL) {
                continue;
            }
            // A generic function has no body of its own. Its copies are
            // registered below, one per set of types it was called with.
            if (symbol->type->type_param_count > 0) {
                continue;
            }
            KestChunk *chunk = kest_module_add(module, symbol->type->symbol);
            if (chunk == NULL) {
                if (module->out_of_room) {
                    return false;
                }
                two_of_one_name(program, symbol->type->symbol, decl->name);
                return false;
            }
            chunk->source = program->source;
            chunk->declared = symbol->span;
            chunk->returns_value = decl->function.result != NULL;
            chunk->result_slots = symbol->type->result == NULL
                                      ? 0
                                      : symbol->type->result->slots;
            chunk->no_alloc = symbol->type->no_alloc;
            chunk->no_host = symbol->type->no_host;
            chunk->deterministic = symbol->type->deterministic;
            chunk->param_slots = 0;
        }
    }

    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *instance = &program->instances[i];
        if (instance->symbol == NULL) {
            continue;
        }
        KestChunk *chunk = kest_module_add(module, instance->symbol);
        if (chunk == NULL) {
            if (module->out_of_room) {
                return false;
            }
            kest_program_in(program, instance->unit);
            two_of_one_name(program, instance->symbol,
                            instance->decl->name);
            return false;
        }
        chunk->source = &instance->unit->source;
        // The generic's own declaration, which every copy of it shares: that
        // is what says the copies are copies rather than two functions of a
        // name. See D612.
        chunk->declared = instance->decl->name;
        chunk->returns_value = instance->decl->function.result != NULL;
        chunk->result_slots = instance->type->result == NULL
                                  ? 0
                                  : instance->type->result->slots;
        chunk->no_alloc = instance->type->no_alloc;
        chunk->no_host = instance->type->no_host;
        chunk->deterministic = instance->type->deterministic;
    }

    // Every constant is worked out here, where it is declared, rather than at
    // each use of it. Three things come of that: a constant read five times is
    // folded once, a constant read no times is still worked out — a program
    // could carry one that divides by nought and nothing said so — and what is
    // wrong with one is said at the declaration, which is where a reader looks
    // for what a name is.
    //
    // Walked a file at a time, because what a constant is written as is read
    // out of the file it is written in and a name in it may leave off the
    // module it is under. See D674.
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        for (uint32_t i = 0; i < program->global_count; i++) {
            KestSymbol *symbol = &program->globals[i];
            if (!symbol->is_const || symbol->value == NULL ||
                symbol->type == NULL || symbol->source != program->source) {
                continue;
            }
            uint32_t slots = value_slots(symbol->type);
            KestValue *values = KEST_ARENA_ARRAY(program->arena, KestValue,
                                                 slots == 0 ? 1 : slots);
            if (values == NULL) {
                compiler.out_of_memory = true;
                return false;
            }
            const char *why = NULL;
            bool never = false;
            if (kest_fold_const(program, symbol->value, values, slots, &why,
                                &never) == slots) {
                symbol->folded = values;
                symbol->folded_slots = slots;
                continue;
            }
            symbol->would_not_fold = true;
            // Two refusals written out rather than one with a choice in it:
            // what a code can say is read out of this file, and a message
            // written under two codes at once is a wording neither of them
            // owns. See D673.
            if (never) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0510",
                               symbol->span,
                               "`%s` is made while running, so it is not a "
                               "constant",
                               symbol->name);
            } else {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0504",
                               symbol->span,
                               "`%s` is not worked out where it is written",
                               symbol->name);
            }
            kest_diags_suggest(program->diags, "%s",
                               why != NULL
                                   ? why
                                   : "a constant is a number, a truth or a "
                                     "piece of text, and arithmetic on those "
                                     "and on other constants");
        }
    }

    // One body per concrete function: every declaration with a body, and then
    // every copy of a generic. What comes out of this is what a backend reads,
    // and the walk below writes no instruction at all.
    uint32_t index = 0;
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        const KestUnit *unit = &units->items[u].unit;

        for (uint32_t i = 0; i < unit->count; i++) {
            const KestDecl *decl = unit->items[i];
            if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
                continue;
            }
            KestSymbol *declared =
                kest_symbol_at(program, program->source, decl->name);
            if (declared != NULL && declared->type->type_param_count > 0) {
                continue;
            }

            KestSymbol *symbol =
                kest_symbol_at(program, program->source, decl->name);
            KestChunk *chunk = module->functions[index++];
            if (!open_body(&compiler, chunk,
                           symbol == NULL ? NULL : symbol->type)) {
                return false;
            }
            compiler.unit = u;

            for (uint32_t p = 0; p < decl->function.param_count; p++) {
                const KestType *type =
                    symbol != NULL && p < symbol->type->param_count
                        ? symbol->type->params[p]
                        : NULL;
                declare_local(&compiler, decl->function.params[p]->name, type);
            }
            if (!close_body(&compiler, &decl->function.body, decl->name)) {
                return false;
            }
        }
    }

    // Each copy of a generic function, written from the same body with its
    // type names bound. Nothing about it is a special case except that -- and
    // the clock around it, which is what says what the rule costs in time
    // rather than in bytes. See D778 and D1026.
    uint64_t copying = kest_ir_ticked(ir->now, ir->now_context);
    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *instance = &program->instances[i];
        if (instance->symbol == NULL) {
            continue;
        }
        kest_program_in(program, (KestUnitInfo *)instance->unit);
        kest_diags_in(program->diags, program->source);
        KestInstance *made = &program->instances[i];
        if (!kest_retype_instance(program, made)) {
            return false;
        }
        kest_bind_types(program, made->names, made->bindings, made->count);

        const KestDecl *decl = instance->decl;
        if (!open_body(&compiler, module->functions[index++],
                       instance->type)) {
            return false;
        }
        compiler.unit = unit_index(units, instance->unit);

        for (uint32_t p = 0; p < decl->function.param_count; p++) {
            declare_local(&compiler, decl->function.params[p]->name,
                          p < instance->type->param_count
                              ? instance->type->params[p]
                              : NULL);
        }
        bool went = close_body(&compiler, &decl->function.body, decl->name);
        kest_unbind_types(program);
        if (!went) {
            return false;
        }
    }
    ir->copies += kest_ir_ticked(ir->now, ir->now_context) - copying;

    // Every element type a signature mentions gets a layout, whether or not a
    // body ever reached one. What a host can be handed is what the program
    // says it takes, and that is written in the declarations rather than in
    // what the bodies happened to compile to. See D068.
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestType *type = program->globals[i].type;
        if (type == NULL || type->tag != KEST_T_FN) {
            continue;
        }
        for (uint32_t p = 0; p <= type->param_count; p++) {
            const KestType *held =
                p == type->param_count ? type->result : type->params[p];
            // An array only. A store is a slot map with generations and a
            // free list, so nothing a host has is one, and saying its element
            // could be lent would be offering something with nowhere to go.
            if (held == NULL || held->tag != KEST_T_ARRAY) {
                continue;
            }
            if (kest_module_layout(module, held->element) < 0) {
                return false;
            }
        }
    }

    return !compiler.out_of_memory && !ir->out_of_memory;
}
