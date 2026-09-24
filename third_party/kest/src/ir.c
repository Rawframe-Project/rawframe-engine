#include "ir.h"

#include <stdlib.h>
#include <string.h>

// One row an operation: what it is called where a reader sees it, and what it
// does that a promise is about. Everything that can be answered from the
// operation alone is answered here rather than at the places that ask, so that
// a backend, the contract proof and a reader are reading one list.
// `check-tables.sh` holds it to the enum beside it, name for name.
static const struct {
    const char *word;
    uint16_t effects;
} IR_OPS[] = {
    [KEST_IR_CONST] = {"const", KEST_IR_EFFECT_NONE},
    [KEST_IR_CONST_AT] = {"const.at", KEST_IR_EFFECT_NONE},
    [KEST_IR_TRUE] = {"true", KEST_IR_EFFECT_NONE},
    [KEST_IR_FALSE] = {"false", KEST_IR_EFFECT_NONE},
    [KEST_IR_LOAD] = {"load", KEST_IR_EFFECT_NONE},
    [KEST_IR_PUT] = {"put", KEST_IR_EFFECT_WRITES},
    [KEST_IR_ADDR] = {"addr", KEST_IR_EFFECT_NONE},
    [KEST_IR_MAKE] = {"make", KEST_IR_EFFECT_NONE},
    [KEST_IR_PART] = {"part", KEST_IR_EFFECT_NONE},
    [KEST_IR_TURN] = {"turn", KEST_IR_EFFECT_NONE},
    [KEST_IR_DROP] = {"drop", KEST_IR_EFFECT_NONE},

    [KEST_IR_ADD] = {"add", KEST_IR_EFFECT_NONE},
    [KEST_IR_SUB] = {"sub", KEST_IR_EFFECT_NONE},
    [KEST_IR_MUL] = {"mul", KEST_IR_EFFECT_NONE},
    [KEST_IR_DIV] = {"div", KEST_IR_EFFECT_NONE},
    [KEST_IR_MOD] = {"mod", KEST_IR_EFFECT_NONE},
    [KEST_IR_NEG] = {"neg", KEST_IR_EFFECT_NONE},
    [KEST_IR_AND] = {"and", KEST_IR_EFFECT_NONE},
    [KEST_IR_OR] = {"or", KEST_IR_EFFECT_NONE},
    [KEST_IR_XOR] = {"xor", KEST_IR_EFFECT_NONE},
    [KEST_IR_FLIP] = {"flip", KEST_IR_EFFECT_NONE},
    [KEST_IR_SHL] = {"shl", KEST_IR_EFFECT_NONE},
    [KEST_IR_SHR] = {"shr", KEST_IR_EFFECT_NONE},
    [KEST_IR_NARROW] = {"narrow", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_FLOAT] = {"to.float", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_WHOLE] = {"to.whole", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_F32] = {"to.f32", KEST_IR_EFFECT_NONE},
    [KEST_IR_BITS] = {"bits", KEST_IR_EFFECT_NONE},

    [KEST_IR_LT] = {"lt", KEST_IR_EFFECT_NONE},
    [KEST_IR_LE] = {"le", KEST_IR_EFFECT_NONE},
    [KEST_IR_GT] = {"gt", KEST_IR_EFFECT_NONE},
    [KEST_IR_GE] = {"ge", KEST_IR_EFFECT_NONE},
    [KEST_IR_EQ] = {"eq", KEST_IR_EFFECT_NONE},
    [KEST_IR_NE] = {"ne", KEST_IR_EFFECT_NONE},
    [KEST_IR_NOT] = {"not", KEST_IR_EFFECT_NONE},
    [KEST_IR_HASH] = {"hash", KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_TEXT_LEN] = {"text.len", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_AT] = {"text.at", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_IN] = {"text.in", KEST_IR_EFFECT_NONE},
    [KEST_IR_TEXT_SLICE] = {"text.slice",
                            KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_REST] = {"text.rest", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_MATCHES] = {"text.matches", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_FIND] = {"text.find", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_OF] = {"text.of",
                         KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_JOIN] = {"text.join",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_FROM] = {"text.from",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_ARRAY] = {"array",
                       KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_ARRAY_NEW] = {"array.new",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_LEN] = {"len", KEST_IR_EFFECT_NONE},
    [KEST_IR_APPEND] = {"append", KEST_IR_EFFECT_ALLOCATES |
                                      KEST_IR_EFFECT_WRITES |
                                      KEST_IR_EFFECT_MOVES},
    [KEST_IR_FIT] = {"fit", KEST_IR_EFFECT_WRITES},
    [KEST_IR_APPEND_TEXT] = {"append.text", KEST_IR_EFFECT_ALLOCATES |
                                                KEST_IR_EFFECT_WRITES |
                                                KEST_IR_EFFECT_MOVES},
    [KEST_IR_FIT_TEXT] = {"fit.text", KEST_IR_EFFECT_WRITES},
    [KEST_IR_ROOM] = {"room", KEST_IR_EFFECT_ALLOCATES |
                                  KEST_IR_EFFECT_WRITES |
                                  KEST_IR_EFFECT_MOVES},
    [KEST_IR_POP_LAST] = {"pop.last", KEST_IR_EFFECT_WRITES},
    [KEST_IR_TAKE] = {"take",
                      KEST_IR_EFFECT_WRITES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_CLEAR] = {"clear", KEST_IR_EFFECT_WRITES},

    [KEST_IR_STORE_NEW] = {"store.new",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_STORE_ADD] = {"store.add", KEST_IR_EFFECT_ALLOCATES |
                                            KEST_IR_EFFECT_WRITES |
                                            KEST_IR_EFFECT_MOVES},
    [KEST_IR_STORE_GET] = {"store.get", KEST_IR_EFFECT_NONE},
    [KEST_IR_STORE_SET] = {"store.set", KEST_IR_EFFECT_WRITES},
    [KEST_IR_STORE_REMOVE] = {"store.remove", KEST_IR_EFFECT_WRITES},
    [KEST_IR_STORE_COUNT] = {"store.count", KEST_IR_EFFECT_NONE},
    [KEST_IR_STORE_REF] = {"store.ref", KEST_IR_EFFECT_NONE},
    [KEST_IR_NEXT] = {"next", KEST_IR_EFFECT_NONE},
    [KEST_IR_SEEK_FROM] = {"seek.from", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_SEEK_NEXT] = {"seek.next", KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_CALL] = {"call", KEST_IR_EFFECT_NONE},
    [KEST_IR_CALL_VALUE] = {"call.value", KEST_IR_EFFECT_NONE},
    [KEST_IR_CALL_HOST] = {"call.host",
                           KEST_IR_EFFECT_HOST | KEST_IR_EFFECT_UNSETTLED},

    [KEST_IR_REGION_OPEN] = {"region.open", KEST_IR_EFFECT_NONE},
    [KEST_IR_REGION_CLOSE] = {"region.close", KEST_IR_EFFECT_NONE},

    [KEST_IR_GO] = {"go", KEST_IR_EFFECT_NONE},
    [KEST_IR_ASK] = {"ask", KEST_IR_EFFECT_NONE},
    [KEST_IR_GIVE] = {"give", KEST_IR_EFFECT_NONE},
    [KEST_IR_MEET] = {"meet", KEST_IR_EFFECT_NONE},
    [KEST_IR_NOTHING] = {"nothing", KEST_IR_EFFECT_NONE},
};

_Static_assert(sizeof(IR_OPS) / sizeof(IR_OPS[0]) == KEST_IR_OP_COUNT,
               "every operation is named and says what it does");

const char *kest_ir_word(KestIrKind kind) {
    return kind < KEST_IR_OP_COUNT ? IR_OPS[kind].word : "?";
}

static uint16_t kest_ir_effects(KestIrKind kind) {
    return kind < KEST_IR_OP_COUNT ? IR_OPS[kind].effects : 0;
}

// What each of a body's lists starts at, measured over the library and the
// examples the way a chunk's were in D753: the middle body is forty-one
// operations over six blocks with nine values and four places.
#define FLOOR_OPS 32
#define FLOOR_ARGS 32
#define FLOOR_VALUES 16
#define FLOOR_PLACES 8
#define FLOOR_NAMES 8
#define FLOOR_CONSTANTS 4

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size, uint32_t floor) {
    uint32_t grown = *capacity == 0 ? floor : *capacity * 2;
    void *moved = kest_arena_alloc(arena, size * grown, 16);
    if (moved == NULL) {
        return NULL;
    }
    if (count > 0) {
        memcpy(moved, items, size * count);
    }
    *capacity = grown;
    return moved;
}

void kest_ir_program_init(KestIrProgram *program, KestArena *arena,
                          KestIrWritten written, void *backend) {
    memset(program, 0, sizeof *program);
    program->arena = arena;
    program->written = written;
    program->backend = backend;
}

KestIrBody *kest_ir_body_begin(KestIrProgram *program) {
    program->before = kest_arena_mark(program->arena);
    memset(&program->body, 0, sizeof program->body);
    return &program->body;
}

// The pass, which is written where the walks it needs are and is named here
// because this is where it is used. See D1024.
static bool optimize(KestIrProgram *program, KestIrBody *body,
                     KestIrFound *found);

bool kest_ir_body_end(KestIrProgram *program) {
    // Verified, optimized, verified again, and only then handed over. What
    // goes into the optimizer is a body the verifier accepted and what comes
    // out has to be one too, which is the whole of what the shape is for: a
    // pass that breaks a body is found here rather than in the machine.
    //
    // The second verification is the backend's own, at the top of
    // `kest_lower_body`, and is left there rather than repeated here: what a
    // body that is not one needs is a diagnostic naming where it was
    // declared, and the backend is what has the diagnostics. A backend is
    // entitled to refuse what it is handed, and this is the one that does.
    // See D1024.
    uint64_t at = kest_ir_ticked(program->now, program->now_context);
    if (!program->out_of_memory && kest_ir_verify(&program->body) == NULL) {
        program->verifying +=
            kest_ir_ticked(program->now, program->now_context) - at;
        at = kest_ir_ticked(program->now, program->now_context);
        KestIrFound found;
        memset(&found, 0, sizeof found);
        if (!optimize(program, &program->body, &found)) {
            program->out_of_memory = true;
        } else {
            program->found.bodies += found.bodies;
            program->found.ops += found.ops;
            program->found.slot_copies += found.slot_copies;
            program->found.reloads += found.reloads;
            program->found.reloaded_slots += found.reloaded_slots;
            program->found.dead_writes += found.dead_writes;
            program->found.materialized += found.materialized;
            program->found.hot_copies += found.hot_copies;
            program->found.hot_reloads += found.hot_reloads;
            program->found.hot_dead += found.hot_dead;
            program->found.hot_materialized += found.hot_materialized;
            program->found.copies_taken += found.copies_taken;
            if (program->say_found != NULL) {
                program->say_found(&program->body, &found);
            }
        }
        program->optimizing +=
            kest_ir_ticked(program->now, program->now_context) - at;
    }
    at = kest_ir_ticked(program->now, program->now_context);
    bool went = program->out_of_memory
                    ? false
                    : program->written(program->backend, &program->body);
    program->lowering +=
        kest_ir_ticked(program->now, program->now_context) - at;
    kest_arena_rewind(program->arena, program->before);
    memset(&program->body, 0, sizeof program->body);
    return went;
}

uint32_t kest_ir_place_add(KestIrProgram *program, KestIrBody *body,
                           const KestIrPlace *place) {
    if (body->place_count == body->place_capacity) {
        uint32_t capacity = body->place_capacity;
        void *places = grow(program->arena, body->places, body->place_count,
                            &capacity, sizeof(KestIrPlace), FLOOR_PLACES);
        if (places == NULL) {
            program->out_of_memory = true;
            return KEST_IR_NO_PLACE;
        }
        body->places = places;
        body->place_capacity = capacity;
    }
    body->places[body->place_count] = *place;
    return body->place_count++;
}

KestIrRef kest_ir_value_add(KestIrProgram *program, KestIrBody *body,
                            const KestType *type, uint16_t slots) {
    if (body->value_count == body->value_capacity) {
        uint32_t capacity = body->value_capacity;
        void *values = grow(program->arena, body->values, body->value_count,
                            &capacity, sizeof(KestIrValue), FLOOR_VALUES);
        if (values == NULL) {
            program->out_of_memory = true;
            return KEST_IR_NONE;
        }
        body->values = values;
        body->value_capacity = capacity;
    }
    KestIrValue *value = &body->values[body->value_count];
    value->type = type;
    value->slots = slots;
    value->made_by = body->op_count;
    value->read_by = KEST_IR_NONE;
    return body->value_count++;
}

uint32_t kest_ir_name_add(KestIrProgram *program, KestIrBody *body,
                          const KestIrName *name) {
    if (body->name_count == body->name_capacity) {
        uint32_t capacity = body->name_capacity;
        void *names = grow(program->arena, body->names, body->name_count,
                           &capacity, sizeof(KestIrName), FLOOR_NAMES);
        if (names == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->names = names;
        body->name_capacity = capacity;
    }
    body->names[body->name_count] = *name;
    return body->name_count++;
}

uint32_t kest_ir_constants_add(KestIrProgram *program, KestIrBody *body,
                               const KestValue *values, const uint8_t *classes,
                               uint16_t count) {
    while (body->constant_count + count > body->constant_capacity) {
        uint32_t capacity = body->constant_capacity;
        void *constants =
            grow(program->arena, body->constants, body->constant_count,
                 &capacity, sizeof(KestValue), FLOOR_CONSTANTS);
        uint32_t for_classes = body->constant_capacity;
        void *held =
            grow(program->arena, body->constant_classes, body->constant_count,
                 &for_classes, sizeof(uint8_t), FLOOR_CONSTANTS);
        if (constants == NULL || held == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->constants = constants;
        body->constant_classes = held;
        body->constant_capacity = capacity;
    }
    uint32_t first = body->constant_count;
    for (uint16_t i = 0; i < count; i++) {
        body->constants[first + i] = values[i];
        body->constant_classes[first + i] = classes[i];
    }
    body->constant_count += count;
    return first;
}

uint32_t kest_ir_op(KestIrProgram *program, KestIrBody *body, KestIrKind kind,
                    const KestType *type, const KestIrRef *args,
                    uint16_t arg_count, KestSpan span) {
    while (body->arg_count + arg_count > body->arg_capacity) {
        uint32_t capacity = body->arg_capacity;
        void *grown = grow(program->arena, body->args, body->arg_count,
                           &capacity, sizeof(KestIrRef), FLOOR_ARGS);
        if (grown == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->args = grown;
        body->arg_capacity = capacity;
    }
    if (body->op_count == body->op_capacity) {
        uint32_t capacity = body->op_capacity;
        void *ops = grow(program->arena, body->ops, body->op_count, &capacity,
                         sizeof(KestIrOp), FLOOR_OPS);
        if (ops == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->ops = ops;
        body->op_capacity = capacity;
    }
    uint32_t at = body->op_count++;
    KestIrOp *op = &body->ops[at];
    memset(op, 0, sizeof *op);
    op->kind = (uint16_t)kind;
    op->effects = kest_ir_effects(kind);
    op->dest = KEST_IR_NONE;
    op->place = KEST_IR_NO_PLACE;
    op->type = type;
    op->span = span;
    op->first_arg = body->arg_count;
    op->arg_count = arg_count;
    for (uint16_t i = 0; i < arg_count; i++) {
        body->args[body->arg_count++] = args[i];
        if (args[i] < body->value_count) {
            body->values[args[i]].read_by = at;
        }
    }
    return at;
}

void kest_ir_lands_here(KestIrBody *body, uint32_t branch) {
    if (branch < body->op_count) {
        body->ops[branch].target = body->op_count;
    }
}

// What a body has to be for a backend to read it without asking anything else.
// Said as a sentence rather than an index, because what a reader does with the
// answer is print it, and a number would send them back here.
// Whether a value of this type can hold anything the machine keeps. A number
// made inside a working-memory block is a number afterwards; a piece of text
// made there is a place that is not there any more.
static bool can_hold(const KestType *type) {
    const KestType *what = NULL;
    return kest_type_holds_own(type, &what);
}

// Whether what an operation leaves is made out of what it read rather than out
// of the heap. A cut of a piece of text is the piece it was cut from; an
// element read out of an array is the array's; a value out of its parts is its
// parts. Everything else that can hold what the machine keeps -- a call, a
// piece of text built, an array made -- is taken to have made it here, because
// what a called body did with the heap is not this body's to know.
static bool passes_through(uint16_t kind) {
    switch (kind) {
    case KEST_IR_CONST:
    case KEST_IR_CONST_AT:
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
    case KEST_IR_LOAD:
    case KEST_IR_ADDR:
    case KEST_IR_MAKE:
    case KEST_IR_PART:
    case KEST_IR_TURN:
    case KEST_IR_MEET:
    case KEST_IR_TEXT_SLICE:
    case KEST_IR_TEXT_REST:
    case KEST_IR_POP_LAST:
    case KEST_IR_TAKE:
    case KEST_IR_STORE_GET:
    case KEST_IR_STORE_REF:
    // `if let` in the form that leaves what the optional held: the branch
    // reads one value and leaves part of it, which is what `part` and `meet`
    // beside it do. It was not here, so every name an `if let` bound inside a
    // block was read as something the block made — and handing one to a call
    // that also takes something older is refused. A frame that walks a world
    // inside a `scratch { }` block and looks each thing up in a table is that
    // shape, and it could not be written. See D1073.
    case KEST_IR_ASK:
        return true;
    default:
        return false;
    }
}

// Whether a call handed a value of this type could make it bigger: an array or
// a store, or a shape holding one anywhere inside it. Text cannot grow -- a
// piece of text is its bytes and how many, and a call is handed a copy of both
// -- and neither can a number. See D1075.
static bool can_grow(const KestType *type, uint32_t depth) {
    if (type == NULL || depth > 8) {
        return type != NULL;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
    case KEST_T_STORE:
        return true;
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (can_grow(type->members[i].type, depth + 1)) {
                return true;
            }
        }
        return false;
    case KEST_T_OPTIONAL:
    case KEST_T_FIXED:
        return can_grow(type->element, depth + 1);
    default:
        return false;
    }
}

// Whether something can be written into a value of this type that holds what
// the machine keeps. A run of bytes cannot: copying a piece of text into one
// copies the bytes, and what the block made is gone with the block. A run of
// text can, and so can a store of a shape with text in it, and so can a
// reference to one. This is what tells a keep from a copy. See D966.
static bool can_keep(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_REF:
    case KEST_T_OPTIONAL:
        return can_hold(type->element) || can_keep(type->element);
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (can_keep(type->members[i].type)) {
                return true;
            }
        }
        return false;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (can_keep(type->cases[c].payload[p])) {
                    return true;
                }
            }
        }
        return false;
    case KEST_T_FIXED:
        return can_hold(type->element) || can_keep(type->element);
    default:
        return false;
    }
}

static bool value_kept(const bool *made, const KestIrBody *body,
                       const KestIrOp *op, uint16_t which) {
    if (which >= op->arg_count) {
        return false;
    }
    KestIrRef ref = body->args[op->first_arg + which];
    return ref < body->value_count && made[ref];
}

// Whether an operation on a container asks the heap for more room. `fit` and
// `set` write into what is already there and are what a `scratch { }` block
// does to a thing that outlives it; `push`, `room` and `add` ask for more.
// See D972.
static bool grows_the_heap(KestIrKind kind) {
    switch (kind) {
    case KEST_IR_APPEND:
    case KEST_IR_APPEND_TEXT:
    case KEST_IR_ROOM:
    case KEST_IR_STORE_ADD:
        return true;
    default:
        return false;
    }
}

const char *kest_ir_escapes(const KestIrBody *body, KestArena *arena,
                            KestSpan *where) {
    bool *made = KEST_ARENA_ARRAY(arena, bool,
                                  body->value_count == 0 ? 1
                                                         : body->value_count);
    bool *kept = KEST_ARENA_ARRAY(arena, bool,
                                  body->slot_count == 0 ? 1 : body->slot_count);
    uint16_t inside[16];
    uint32_t open = 0;
    if (made == NULL || kept == NULL) {
        return "there was no room to follow what a block keeps";
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        *where = op->span;
        if (op->kind == KEST_IR_REGION_OPEN) {
            if (open < sizeof(inside) / sizeof(inside[0])) {
                inside[open] = op->imm[1];
            }
            open++;
            continue;
        }
        if (op->kind == KEST_IR_REGION_CLOSE) {
            // A way out of the block rather than the end of it: the heap goes
            // back here and the block is still what a value made inside it
            // belongs to, so this one is not what stops the counting.
            if (op->imm[2] != 0) {
                continue;
            }
            if (open > 0) {
                open--;
                uint16_t from = open < sizeof(inside) / sizeof(inside[0])
                                    ? inside[open]
                                    : 0;
                for (uint16_t slot = from; slot < body->slot_count; slot++) {
                    kept[slot] = false;
                }
            }
            continue;
        }
        if (open == 0) {
            continue;
        }
        uint16_t nearest = inside[(open - 1) < 16 ? open - 1 : 15];
        const KestIrPlace *place =
            op->place == KEST_IR_NO_PLACE ? NULL : &body->places[op->place];

        // What a value is made of: something the block made, or something made
        // out of one. A comparison of two pieces of text is a truth and not a
        // piece of text, which is why the type is asked rather than the
        // operation.
        bool reads_one = false;
        for (uint16_t a = 0; a < op->arg_count; a++) {
            reads_one = reads_one || value_kept(made, body, op, a);
        }
        if (place != NULL && op->kind == KEST_IR_LOAD) {
            if (place->kind == KEST_IR_PLACE_SLOT) {
                for (uint16_t s = 0; s < place->slots; s++) {
                    uint32_t at = (uint32_t)place->slot + s;
                    reads_one = reads_one ||
                                (at < body->slot_count && kept[at]);
                }
            } else if (place->base < body->value_count) {
                reads_one = reads_one || made[place->base];
            }
        }
        if (op->dest != KEST_IR_NONE &&
            can_hold(body->values[op->dest].type) &&
            (reads_one || !passes_through(op->kind))) {
            made[op->dest] = true;
        }

        switch ((KestIrKind)op->kind) {
        case KEST_IR_PUT: {
            if (!value_kept(made, body, op, (uint16_t)(op->arg_count - 1))) {
                break;
            }
            if (place == NULL) {
                break;
            }
            if (place->kind == KEST_IR_PLACE_SLOT) {
                if (place->slot < nearest) {
                    return "this keeps what the block made in a name the "
                           "block does not own";
                }
                for (uint16_t s = 0; s < place->slots; s++) {
                    uint32_t at = (uint32_t)place->slot + s;
                    if (at < body->slot_count) {
                        kept[at] = true;
                    }
                }
                break;
            }
            if (place->base >= body->value_count || !made[place->base]) {
                return "this puts what the block made into something that "
                       "outlives it";
            }
            break;
        }
        // Everything a container is written through. The thing written into is
        // the first of what they read, and what goes in is the rest: a block's
        // own array may hold the block's own text, and nothing else may.
        case KEST_IR_APPEND:
        case KEST_IR_FIT:
        case KEST_IR_APPEND_TEXT:
        case KEST_IR_FIT_TEXT:
        case KEST_IR_ROOM:
        case KEST_IR_STORE_ADD:
        case KEST_IR_STORE_SET: {
            bool into = value_kept(made, body, op, 0);
            // And growing one that is older than the block. The heap goes
            // back where it was when the block ends, and a container that
            // outlives the block would go back with it -- not the bytes it
            // was given inside, which nobody could reach anyway, but the ones
            // it already had, because a bump arena hands out what is next and
            // what is next is above the mark. A world grown inside a block
            // was emptied by the end of it, and nothing said so. So this is
            // refused where it is written: `fit` and `set` write into room a
            // thing already has and are what a block does, and `push`, `room`
            // and `add` ask for more and are not. See D972.
            if (!into && grows_the_heap((KestIrKind)op->kind)) {
                return "this grows something that outlives the block, and "
                       "what a block takes it gives back";
            }
            for (uint16_t a = 1; a < op->arg_count; a++) {
                if (value_kept(made, body, op, a) && !into) {
                    return "this puts what the block made into something that "
                           "outlives it";
                }
            }
            break;
        }
        case KEST_IR_GIVE:
            if (value_kept(made, body, op, 0)) {
                return "this gives back what the block made, and the block "
                       "puts it away";
            }
            break;
        // A call that is handed what the block made and something older to put
        // it in. What a called body does with what it is given is its own, so
        // this is the one shape that cannot be followed and is refused
        // instead. A crossing into the host is not one of these: see D966.
        case KEST_IR_CALL:
        case KEST_IR_CALL_VALUE: {
            // A call inside a block, handed something older than the block
            // that can grow, and promising nothing about the heap. What it
            // grew would be grown in the block's memory and given back when
            // the block ends, which is the same thing D972 refuses when the
            // growth is written here -- and a call is the one shape that
            // cannot be followed, so it is refused rather than followed. A
            // callee that promises `no.alloc` cannot grow anything, which is
            // what leaves a lookup inside a block a thing a program may still
            // write. See D1075.
            if (open > 0 && op->imm[2] == 0) {
                for (uint16_t a = 0; a < op->arg_count; a++) {
                    KestIrRef ref = body->args[op->first_arg + a];
                    if (ref >= body->value_count || made[ref]) {
                        continue;
                    }
                    if (can_grow(body->values[ref].type, 0)) {
                        return "this hands something that outlives the block "
                               "to a call that may grow it, and what a block "
                               "grows it gives back";
                    }
                }
            }
            bool any = false;
            for (uint16_t a = 0; a < op->arg_count; a++) {
                any = any || value_kept(made, body, op, a);
            }
            if (!any) {
                break;
            }
            for (uint16_t a = 0; a < op->arg_count; a++) {
                KestIrRef ref = body->args[op->first_arg + a];
                if (ref >= body->value_count || made[ref]) {
                    continue;
                }
                if (can_keep(body->values[ref].type)) {
                    return "this hands what the block made to something that "
                           "outlives it and could keep it";
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return NULL;
}

uint64_t kest_ir_ticked(uint64_t (*now)(void *), void *context) {
    return now == NULL ? 0 : now(context);
}

bool kest_ir_asked_off(const char *name, int *decided) {
    if (*decided < 0) {
        *decided = getenv(name) == NULL ? 0 : 1;
    }
    return *decided != 0;
}

const char *kest_ir_verify(const KestIrBody *body) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind >= KEST_IR_OP_COUNT) {
            return "an operation this list has no name for";
        }
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target > body->op_count) {
            return "a branch landing on an operation this body has not got";
        }
        for (uint16_t a = 0; a < op->arg_count; a++) {
            KestIrRef ref = body->args[op->first_arg + a];
            if (ref >= body->value_count) {
                return "an operation reading a value this body has not made";
            }
            if (body->values[ref].made_by > i) {
                return "an operation reading a value made after it";
            }
        }
        if (op->place != KEST_IR_NO_PLACE && op->place >= body->place_count) {
            return "an operation naming a place this body has not got";
        }
    }
    for (uint32_t i = 0; i < body->value_count; i++) {
        if (body->values[i].read_by == KEST_IR_NONE) {
            return "a value nothing reads";
        }
    }
    return NULL;
}

// What a body's straight-line runs are: an operation a branch lands on starts
// one, and everything between two of those runs in a row. Kept as a bit a
// place rather than as a list of blocks, because what every pass here asks is
// "is there a landing between these two operations" and that is a scan of a
// few words. See D1024.
static bool lands_between(const KestIrBody *body, uint32_t from, uint32_t to) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind != KEST_IR_GO && op->kind != KEST_IR_ASK &&
            op->kind != KEST_IR_NEXT && op->kind != KEST_IR_SEEK_FROM &&
            op->kind != KEST_IR_SEEK_NEXT) {
            continue;
        }
        if (op->target > from && op->target <= to) {
            return true;
        }
    }
    return false;
}

// Whether two places are the same run of slots in the frame. Only `SLOT` can
// be answered for: a `RUN` is an index worked out while running and an `ELEM`
// is a handle and an index, and two of either may or may not be the same
// place depending on what ran in between.
static bool same_slots(const KestIrPlace *one, const KestIrPlace *two) {
    return one->kind == KEST_IR_PLACE_SLOT && two->kind == KEST_IR_PLACE_SLOT &&
           one->slot == two->slot && one->slots == two->slots;
}

// Whether the slots of one place can be touched by the other. Two runs in the
// frame overlap or they do not, and nothing else here writes the frame.
static bool slots_overlap(const KestIrPlace *one, const KestIrPlace *two) {
    if (one->kind != KEST_IR_PLACE_SLOT || two->kind != KEST_IR_PLACE_SLOT) {
        // A `RUN` writes somewhere in a fixed run of the frame and the index
        // is not known here, so it is taken to touch everything.
        return one->kind == KEST_IR_PLACE_RUN || two->kind == KEST_IR_PLACE_RUN;
    }
    return one->slot < two->slot + two->slots &&
           two->slot < one->slot + one->slots;
}

// Whether anything between two operations wrote the slots this place names,
// or did something a read of them cannot be moved across. A call is the
// second of those: what a body does to its caller's frame is nothing, but a
// host function is handed the machine and `kest_keeps` is a door.
static bool disturbed(const KestIrBody *body, uint32_t from, uint32_t to,
                      const KestIrPlace *place) {
    for (uint32_t i = from + 1; i < to; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind == KEST_IR_CALL || op->kind == KEST_IR_CALL_VALUE ||
            op->kind == KEST_IR_CALL_HOST) {
            return true;
        }
        if (op->kind != KEST_IR_PUT && op->kind != KEST_IR_ADDR) {
            continue;
        }
        if (op->place == KEST_IR_NO_PLACE) {
            return true;
        }
        if (slots_overlap(&body->places[op->place], place)) {
            return true;
        }
    }
    return false;
}

// Whether a branch goes backwards, which is the only thing that makes a body
// run an operation more than once. `for` is written as one operation and a
// `while` as a comparison and a jump, so both of them are answered here by
// where the jump lands rather than by which keyword was written.
static bool goes_back(const KestIrOp *op, uint32_t at) {
    switch (op->kind) {
    case KEST_IR_GO:
    case KEST_IR_ASK:
    case KEST_IR_NEXT:
    case KEST_IR_SEEK_FROM:
    case KEST_IR_SEEK_NEXT:
        return op->target <= at;
    default:
        return false;
    }
}

// Which operations of a body are inside a loop. Answered for the whole body
// at once into a run the body's own arena holds, because the question is
// asked of every shape found and asking it one operation at a time is the
// same walk over and over.
//
// It is what makes a count mean anything. A shape found once in a body that
// runs once is worth nothing however many there are of it; the same shape
// between a backward branch and what it lands on is worth as many times as
// the loop goes round, and a pass is written for the second and not the
// first.
static bool *looped(KestArena *arena, const KestIrBody *body) {
    uint32_t many = body->op_count == 0 ? 1 : body->op_count;
    bool *inside = KEST_ARENA_ARRAY(arena, bool, many);
    if (inside == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (!goes_back(&body->ops[i], i)) {
            continue;
        }
        for (uint32_t k = body->ops[i].target; k <= i; k++) {
            inside[k] = true;
        }
    }
    return inside;
}

// Whether the address of these slots is taken anywhere in the body. A place
// reached through an address is one nothing here can follow, so slots that
// have one are left alone altogether rather than reasoned about a use at a
// time.
static bool addressed(const KestIrBody *body, const KestIrPlace *place) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind != KEST_IR_ADDR || op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        if (slots_overlap(&body->places[op->place], place)) {
            return true;
        }
    }
    return false;
}

// Whether a write to these slots is one nothing reads: what follows it either
// leaves them alone or writes the whole of them again, with nothing branching
// in between. A branch is where this stops rather than guesses -- one going
// backwards puts what came before the write after it as well, and one going
// forwards may step over the write that would have made it dead.
static bool never_read_again(const KestIrBody *body, uint32_t at,
                             const KestIrPlace *place) {
    for (uint32_t i = at + 1; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        switch (op->kind) {
        case KEST_IR_GO:
        case KEST_IR_ASK:
        case KEST_IR_NEXT:
        case KEST_IR_SEEK_FROM:
        case KEST_IR_SEEK_NEXT:
            return false;
        default:
            break;
        }
        if (op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        const KestIrPlace *other = &body->places[op->place];
        if (!slots_overlap(other, place)) {
            continue;
        }
        // Written over whole: what was there is gone and nothing read it.
        if (op->kind == KEST_IR_PUT && other->kind == KEST_IR_PLACE_SLOT &&
            other->slot <= place->slot &&
            other->slot + other->slots >= place->slot + place->slots) {
            return true;
        }
        return false;
    }
    // The end of the body, where the frame goes. See D1024 for why this is
    // counted and not yet acted on: a slot holding a handle is a root while
    // the frame stands, and a write taken away is a root taken away.
    return true;
}

// Which slots of the frame an operation names itself, rather than through a
// place. Six of them do, and nothing about a place says so: a walk's step
// carries the slot it counts in and the slot it counts to, a seek carries the
// store and the index, a byte read out of text carries the text and the
// index, and a block of working memory carries the slot its mark is kept in.
// A copy taken away from under one of these is a loop that never ends, which
// is what the first way this was written cost on `table.slotOf`.
//
// Written out in full and with nothing falling through to a default, because
// it is a list that has to be complete: an operation added to this language
// and forgotten here is a miscompilation and not a warning. A text is two
// slots and the rest are one, which is what the machine reads at each.
static bool names_slots(const KestIrOp *op, uint16_t wide[2]) {
    wide[0] = 0;
    wide[1] = 0;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
    case KEST_IR_CONST_AT:
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
    case KEST_IR_LOAD:
    case KEST_IR_PUT:
    case KEST_IR_ADDR:
    case KEST_IR_MAKE:
    case KEST_IR_PART:
    case KEST_IR_TURN:
    case KEST_IR_DROP:
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
    case KEST_IR_SHR:
    case KEST_IR_NARROW:
    case KEST_IR_TO_FLOAT:
    case KEST_IR_TO_WHOLE:
    case KEST_IR_TO_F32:
    case KEST_IR_BITS:
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE:
    case KEST_IR_NOT:
    case KEST_IR_HASH:
    case KEST_IR_TEXT_LEN:
    case KEST_IR_TEXT_AT:
    case KEST_IR_TEXT_SLICE:
    case KEST_IR_TEXT_REST:
    case KEST_IR_TEXT_MATCHES:
    case KEST_IR_TEXT_FIND:
    case KEST_IR_TEXT_OF:
    case KEST_IR_TEXT_JOIN:
    case KEST_IR_TEXT_FROM:
    case KEST_IR_ARRAY:
    case KEST_IR_ARRAY_NEW:
    case KEST_IR_LEN:
    case KEST_IR_APPEND:
    case KEST_IR_FIT:
    case KEST_IR_APPEND_TEXT:
    case KEST_IR_FIT_TEXT:
    case KEST_IR_ROOM:
    case KEST_IR_POP_LAST:
    case KEST_IR_TAKE:
    case KEST_IR_CLEAR:
    case KEST_IR_STORE_NEW:
    case KEST_IR_STORE_ADD:
    case KEST_IR_STORE_GET:
    case KEST_IR_STORE_SET:
    case KEST_IR_STORE_REMOVE:
    case KEST_IR_STORE_COUNT:
    case KEST_IR_STORE_REF:
    case KEST_IR_CALL:
    case KEST_IR_CALL_VALUE:
    case KEST_IR_CALL_HOST:
    case KEST_IR_GO:
    case KEST_IR_ASK:
    case KEST_IR_GIVE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
    case KEST_IR_OP_COUNT:
        return false;
    case KEST_IR_NEXT:
    case KEST_IR_SEEK_FROM:
    case KEST_IR_SEEK_NEXT:
        wide[0] = 1;
        wide[1] = 1;
        return true;
    case KEST_IR_TEXT_IN:
        wide[0] = 2;
        wide[1] = 1;
        return true;
    case KEST_IR_REGION_OPEN:
    case KEST_IR_REGION_CLOSE:
        wide[0] = 1;
        return true;
    }
    return false;
}

// Whether any operation names these slots that way. Asked of the whole body,
// because one anywhere is enough to leave the run alone.
static bool named_by_hand(const KestIrBody *body, const KestIrPlace *place) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        uint16_t wide[2];
        if (!names_slots(&body->ops[i], wide)) {
            continue;
        }
        for (int which = 0; which < 2; which++) {
            uint16_t named = body->ops[i].imm[which];
            if (wide[which] != 0 && named < place->slot + place->slots &&
                place->slot < named + wide[which]) {
                return true;
            }
        }
    }
    return false;
}

// Copy propagation. A run of slots read straight into another run, where the
// run written is written that once and the run read is not written again
// afterwards: every read of the one is pointed back at the other and the two
// operations that moved it are taken away.
//
// It is the one of the four shapes counted above that takes bytes off what the
// machine moves rather than instructions off what it runs. A copy is a load
// and a store, and D1023 measured loading and storing at between half and four
// fifths of every byte this machine moves. See D1025 for why the other three
// are counted and not done.
//
// It asks about the whole body rather than about what follows, because what
// follows in the list is not what follows in the run: a body is a flat list
// and a branch lands where it likes. Written once and not written again is
// true on every path or on none, so no path has to be walked. The first way
// this was written walked forwards until it met a branch and stopped, and
// what it took away was `let at = from` in `text.trim`, whose reads are all
// inside the loop underneath it.
//
// What it will not touch is a run that can hold what the machine keeps. A slot
// holding a handle is a root while the frame stands, and a write taken away is
// a root taken away; the copies the counts found in a loop are struct and
// number copies, so the restriction costs nothing that was measured and buys
// not having to reason about the walk.
//
// A call is not in the way of any of this. A body called from here is handed a
// frame that starts above this one's slots, so what it does it does elsewhere;
// and a host cannot reach a slot at all.
static bool take_the_copy(KestIrProgram *program, KestIrBody *body,
                          uint32_t at) {
    KestIrPlace from = body->places[body->ops[at].place];
    KestIrPlace into = body->places[body->ops[at + 1].place];
    if (from.slots == 0 || from.slots != into.slots || from.type != into.type) {
        return false;
    }
    if (can_hold(from.type) || can_hold(into.type)) {
        return false;
    }
    if (slots_overlap(&from, &into) || addressed(body, &from) ||
        addressed(body, &into)) {
        return false;
    }
    if (named_by_hand(body, &from) || named_by_hand(body, &into)) {
        return false;
    }
    // Which operations read what was written, found before anything is
    // changed: a walk that rewrote as it went would be one that had half done
    // the job when it met the thing that made it stop.
    uint32_t many = 0;
    uint32_t *reads =
        KEST_ARENA_ARRAY(program->arena, uint32_t,
                         body->op_count == 0 ? 1 : body->op_count);
    if (reads == NULL) {
        program->out_of_memory = true;
        return false;
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (i == at || i == at + 1) {
            continue;
        }
        const KestIrOp *op = &body->ops[i];
        if (op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        const KestIrPlace *other = &body->places[op->place];
        // The run read may not be written again after the copy. Before it is
        // another matter: a write that comes first is what the copy copied.
        if (i > at && slots_overlap(other, &from) &&
            (op->kind == KEST_IR_PUT || op->kind == KEST_IR_ADDR)) {
            return false;
        }
        if (!slots_overlap(other, &into)) {
            continue;
        }
        // And the run written is written once, which is what makes it the
        // same value wherever it is read.
        if (op->kind != KEST_IR_LOAD) {
            return false;
        }
        // A read of the whole of it or of a field of it. Anything else -- a
        // run indexed while running, a place somewhere else that overlaps --
        // is a use this cannot follow.
        if (other->kind != KEST_IR_PLACE_SLOT || other->slot < into.slot ||
            other->slot + other->slots > into.slot + into.slots) {
            return false;
        }
        reads[many++] = i;
    }
    // Pointing each read back at where the value came from. The two runs are
    // the same type and so the same shape, so a field is at the same offset
    // in both and the whole of the arithmetic is one subtraction.
    for (uint32_t k = 0; k < many; k++) {
        KestIrPlace moved = body->places[body->ops[reads[k]].place];
        moved.slot = (uint16_t)(from.slot + (moved.slot - into.slot));
        uint32_t where = kest_ir_place_add(program, body, &moved);
        if (program->out_of_memory) {
            return false;
        }
        body->ops[reads[k]].place = where;
    }
    body->ops[at].kind = KEST_IR_NOTHING;
    body->ops[at].effects = KEST_IR_EFFECT_NONE;
    body->ops[at + 1].kind = KEST_IR_NOTHING;
    body->ops[at + 1].effects = KEST_IR_EFFECT_NONE;
    if (from.slots > body->took_slots) {
        body->took_slots = from.slots;
    }
    return true;
}

// Reads one body, counts what is there, and changes what it has been told to
// change. Answers false only when it ran out of room; a body it cannot
// improve is one it leaves alone.
//
// Between two verifications, which is what the shape of this is for: what
// goes in is a body the verifier accepted and what comes out has to be one
// too. See D1024.
// What there is to do in one body, which is a walk of it per operation for
// three of the four shapes. It happens when somebody asked and not otherwise:
// a compile nobody is reading the counts from should not pay for them, and
// what a release build does is the pass and nothing else. It was eight per
// cent of compiling `agents` before this sentence was true.
static bool count_what_there_is(KestIrProgram *program, const KestIrBody *body,
                                KestIrFound *found) {
    bool *inside = looped(program->arena, body);
    if (inside == NULL) {
        return false;
    }
    found->bodies++;
    found->ops += body->op_count;
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind == KEST_IR_MAKE && op->dest != KEST_IR_NONE &&
            body->values[op->dest].read_by < body->op_count &&
            body->ops[body->values[op->dest].read_by].kind == KEST_IR_PUT) {
            found->materialized++;
            found->hot_materialized += inside[i] ? 1 : 0;
        }
        if (op->kind == KEST_IR_PUT && op->place != KEST_IR_NO_PLACE &&
            body->places[op->place].kind == KEST_IR_PLACE_SLOT &&
            !addressed(body, &body->places[op->place]) &&
            never_read_again(body, i, &body->places[op->place])) {
            found->dead_writes++;
            found->hot_dead += inside[i] ? 1 : 0;
        }
        if (op->kind != KEST_IR_LOAD || op->place == KEST_IR_NO_PLACE) {
            continue;
        }
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind != KEST_IR_PLACE_SLOT) {
            continue;
        }
        // A read straight into a write of another run of slots, which is what
        // `b = a` is and what every argument written into a name is.
        if (op->dest != KEST_IR_NONE &&
            body->values[op->dest].read_by == i + 1 &&
            body->ops[i + 1].kind == KEST_IR_PUT &&
            body->ops[i + 1].place != KEST_IR_NO_PLACE &&
            body->places[body->ops[i + 1].place].kind == KEST_IR_PLACE_SLOT) {
            found->slot_copies++;
            found->hot_copies += inside[i] ? 1 : 0;
        }
        // And the same slots read again with nothing written to them and no
        // branch landing in between.
        for (uint32_t back = i; back-- > 0;) {
            const KestIrOp *was = &body->ops[back];
            if (was->kind != KEST_IR_LOAD || was->place == KEST_IR_NO_PLACE) {
                continue;
            }
            if (!same_slots(&body->places[was->place], place)) {
                continue;
            }
            if (!lands_between(body, back, i) &&
                !disturbed(body, back, i, place)) {
                found->reloads++;
                found->reloaded_slots += place->slots;
                found->hot_reloads += inside[i] ? 1 : 0;
            }
            break;
        }
    }
    return true;
}

// Reads one body, counts what is there when somebody asked, and changes what
// it has been told to change. Answers false only when it ran out of room; a
// body it cannot improve is one it leaves alone.
// A branch over a branch: `if` something `{ continue }` is a question whose
// answer jumps past a jump, and the way out took two of them where one asking
// the other way round goes straight there. The question is turned round where
// it is asked -- a whole number's `<` is its `>=`, and anything's `==` its
// `!=` -- so what the lowering makes one instruction of stays one, and asked
// the other way where it cannot be. Nothing may land on the jump that goes,
// because something arriving there would have had nowhere to go. See D1205.
static KestIrKind turned_round(const KestIrOp *compare) {
    bool ordered = compare->type != NULL && !kest_is_float(compare->type) &&
                   !kest_is_a_run(compare->type) &&
                   compare->type->tag != KEST_T_TEXT;
    switch ((KestIrKind)compare->kind) {
    case KEST_IR_EQ:
        return KEST_IR_NE;
    case KEST_IR_NE:
        return KEST_IR_EQ;
    case KEST_IR_LT:
        return ordered ? KEST_IR_GE : KEST_IR_OP_COUNT;
    case KEST_IR_GE:
        return ordered ? KEST_IR_LT : KEST_IR_OP_COUNT;
    case KEST_IR_LE:
        return ordered ? KEST_IR_GT : KEST_IR_OP_COUNT;
    case KEST_IR_GT:
        return ordered ? KEST_IR_LE : KEST_IR_OP_COUNT;
    default:
        return KEST_IR_OP_COUNT;
    }
}

static void jump_straight_out(KestIrBody *body) {
    for (uint32_t i = 0; i + 2 < body->op_count; i++) {
        KestIrOp *ask = &body->ops[i];
        KestIrOp *go = &body->ops[i + 1];
        if (ask->kind != KEST_IR_ASK || ask->imm[2] != 0 ||
            ask->target != i + 2 || ask->arg_count != 1 ||
            go->kind != KEST_IR_GO || go->target <= i + 2 ||
            go->target >= body->op_count) {
            continue;
        }
        bool landed = false;
        for (uint32_t k = 0; k < body->op_count && !landed; k++) {
            const KestIrOp *one = &body->ops[k];
            landed = (one->kind == KEST_IR_GO || one->kind == KEST_IR_ASK ||
                      one->kind == KEST_IR_NEXT ||
                      one->kind == KEST_IR_SEEK_FROM ||
                      one->kind == KEST_IR_SEEK_NEXT) &&
                     one->target == i + 1;
        }
        if (landed) {
            continue;
        }
        KestIrRef asked = body->args[ask->first_arg];
        KestIrOp *compare = NULL;
        for (uint32_t k = i; k > 0; k--) {
            if (body->ops[k - 1].dest == asked) {
                compare = &body->ops[k - 1];
                break;
            }
        }
        KestIrKind other = compare == NULL ? KEST_IR_OP_COUNT
                                           : turned_round(compare);
        if (other != KEST_IR_OP_COUNT && asked < body->value_count &&
            body->values[asked].read_by == i) {
            compare->kind = (uint16_t)other;
        } else {
            ask->imm[1] = ask->imm[1] != 0 ? 0 : 1;
        }
        ask->target = go->target;
        go->kind = KEST_IR_NOTHING;
    }
}

static bool optimize(KestIrProgram *program, KestIrBody *body,
                     KestIrFound *found) {
    if (found != NULL && program->say_found != NULL &&
        !count_what_there_is(program, body, found)) {
        return false;
    }
    // There is one way to turn the pass off and it is here rather than on the
    // command line, for the reason `KEST_PLAIN` is: compiling the same
    // program twice and requiring the same answer of both is how a
    // transformation is held to being one that keeps a program's meaning.
    // D1009 says every optimization is held that way.
    static int off = -1;
    if (kest_ir_asked_off("KEST_NOOPT", &off)) {
        return true;
    }
    // Counting first and changing after, over the body as it was written:
    // a walk that did both at once would be deciding about shapes it had
    // just made. What it says it found is what was there.
    for (uint32_t i = 0; i + 1 < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind != KEST_IR_LOAD || op->place == KEST_IR_NO_PLACE ||
            body->places[op->place].kind != KEST_IR_PLACE_SLOT ||
            op->dest == KEST_IR_NONE ||
            body->values[op->dest].read_by != i + 1 ||
            body->ops[i + 1].kind != KEST_IR_PUT ||
            body->ops[i + 1].place == KEST_IR_NO_PLACE ||
            body->places[body->ops[i + 1].place].kind != KEST_IR_PLACE_SLOT) {
            continue;
        }
        if (take_the_copy(program, body, i) && found != NULL) {
            found->copies_taken++;
        }
        if (program->out_of_memory) {
            return false;
        }
    }
    jump_straight_out(body);
    return true;
}
