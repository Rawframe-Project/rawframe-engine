#ifndef KEST_IR_H
#define KEST_IR_H

#include "types.h"
#include "value.h"

// What a checked program means, written down once so that more than one
// backend can read the same answer.
//
// The compiler used to walk the checked tree and write stack instructions as
// it went, which made the tree the only statement of what a program does and
// the emitted code the only statement of how. Anything that wanted the first
// without the second -- a second backend, a walk that asks where a value came
// from, a lifetime that has to be followed through a call -- had to ask the
// tree again and resolve the names again. This is the seam: one body per
// concrete function, resolved, typed, and with every place written down as
// what reaches it rather than as an address that has already been worked out.
//
// Three things it is not. It is not SSA: a name is a place and a place is
// written to. It is not a second checker: everything here was already decided
// and nothing in it refuses a program. And it is not a tree: control flow is a
// branch naming what it lands on, because a backend that has to find the end
// of an `if` by walking is a backend that carries the parser's shape into the
// machine's.

// A value produced by an operation and read by exactly one later one. The
// builder makes them in the order a walk of the tree makes them, so a body's
// values are consumed innermost first: what that buys is a stack backend that
// needs no scratch slots and a slot backend whose window position is the depth
// at which the value was made. `kest_ir_verify` holds a body to it.
typedef uint32_t KestIrRef;
#define KEST_IR_NONE ((KestIrRef)0xFFFFFFFFu)

typedef struct {
    const KestType *type;
    // How many slots it takes. A struct is laid out flat, so one value can be
    // a run.
    uint16_t slots;
    uint32_t made_by;
    // Which operation reads it, or `KEST_IR_NONE` while nothing has.
    uint32_t read_by;
} KestIrValue;

// Where a value lives, as what reaches it. Nothing here is an address worked
// out early: an element of an array is the array and the index until the
// moment it is read or written, so growing the array in between cannot leave
// a place pointing at a block nothing will read again. See D931.
typedef enum {
    // A run of slots in the frame: a name, or a field of one, or one of a
    // fixed run at an index written down.
    KEST_IR_PLACE_SLOT,
    // One of a fixed run in the frame at an index worked out while running.
    KEST_IR_PLACE_RUN,
    // One of an array or of a store, as the handle and the index.
    KEST_IR_PLACE_ELEM,
    // Inside memory the host laid out, as an address and a byte offset.
    KEST_IR_PLACE_AT,
    // A value the chunk holds. A `let` the body never assigns to, worked out
    // where it was written: it takes no slot and nothing builds it. See D887.
    KEST_IR_PLACE_HELD,
} KestIrPlaceKind;

typedef struct {
    KestIrPlaceKind kind;
    // What the whole place holds, and how wide that is in slots.
    const KestType *type;
    uint16_t slots;
    // SLOT and RUN: where in the frame. RUN adds the index times the stride.
    uint16_t slot;
    uint16_t stride;
    uint16_t count;
    // ELEM and AT: what the module says an element is made of, and how far
    // into it a field path reached.
    uint16_t layout;
    uint16_t offset;
    // ELEM: the array or store, and which one. RUN: which one. AT: where.
    KestIrRef base;
    KestIrRef index;
    // HELD: the value, and how many slots of it there are.
    const KestValue *held;
    uint16_t held_slots;
    // ELEM: proved to be inside the array, so neither what the handle is nor
    // the bounds need asking where it is read or written. Set only for the
    // element a walk counts through, where nothing in the walk can make the
    // array shorter or name another. See D1187.
    bool in_bounds;
    // ELEM: inside the array whenever the array held in slot `guard_held` is
    // at least as long as slot `guard_limit` says, in a walk counting in slot
    // `guard_counter` from nought or more to that limit -- which is asked
    // once where the walk begins rather than at every element. See D1189.
    bool guarded;
    uint16_t guard_held;
    uint16_t guard_counter;
    uint16_t guard_limit;
    KestSpan span;
} KestIrPlace;

// What an operation does that a promise is about. The contract proof reads
// these rather than asking the tree a second question; a backend reads them to
// know what it may move. See D950 for what "work" means here.
typedef enum {
    KEST_IR_EFFECT_NONE = 0,
    // Reaches the heap.
    KEST_IR_EFFECT_ALLOCATES = 1u << 0,
    // Crosses into the host.
    KEST_IR_EFFECT_HOST = 1u << 1,
    // Answers differently on two machines that ran the same program.
    KEST_IR_EFFECT_UNSETTLED = 1u << 2,
    // Writes something the program can read again.
    KEST_IR_EFFECT_WRITES = 1u << 3,
    // May move what a handle points at, so nothing worked out from that
    // handle survives it.
    KEST_IR_EFFECT_MOVES = 1u << 4,
    // Costs more the more there is of it, so the fuel charge is weighted.
    KEST_IR_EFFECT_WEIGHED = 1u << 5,
} KestIrEffect;

typedef enum {
    // What a value is, before anything is done with it.
    KEST_IR_CONST,
    KEST_IR_CONST_AT,
    KEST_IR_TRUE,
    KEST_IR_FALSE,
    // Moving one. A place is read into a value and a value is written into a
    // place, and nothing else touches the frame.
    KEST_IR_LOAD,
    KEST_IR_PUT,
    KEST_IR_ADDR,
    // A value out of its parts, in the order they were made, and one part of
    // one. A struct is laid out flat, so both are arithmetic on a run.
    KEST_IR_MAKE,
    KEST_IR_PART,
    KEST_IR_TURN,
    KEST_IR_DROP,

    // Arithmetic, each one typed by what it answers. Whether it is signed,
    // how wide it is and whether it is a float are the type's to say, so
    // there is one addition here and not eleven.
    KEST_IR_ADD,
    KEST_IR_SUB,
    KEST_IR_MUL,
    KEST_IR_DIV,
    KEST_IR_MOD,
    KEST_IR_NEG,
    KEST_IR_AND,
    KEST_IR_OR,
    KEST_IR_XOR,
    KEST_IR_FLIP,
    KEST_IR_SHL,
    KEST_IR_SHR,
    // Cut to the width the type declares. Separate from the arithmetic
    // because a backend may put the two together and one that does not is
    // still right.
    KEST_IR_NARROW,
    // Between whole numbers and floats. Each one is somewhere a type was
    // named, so none of them happens on its own.
    KEST_IR_TO_FLOAT,
    KEST_IR_TO_WHOLE,
    KEST_IR_TO_F32,
    // A float as the bits it is made of, or the other way: `imm[0]` is
    // nought for the bits and one for the float. Typed by the float. See
    // D1171.
    KEST_IR_BITS,

    // Answers. Typed by what was compared rather than by what comes back,
    // which is always a truth.
    KEST_IR_LT,
    KEST_IR_LE,
    KEST_IR_GT,
    KEST_IR_GE,
    KEST_IR_EQ,
    KEST_IR_NE,
    KEST_IR_NOT,
    KEST_IR_HASH,

    // Text. Everything that reads one costs nothing and everything that makes
    // one reaches the heap, which is the line the `no.alloc` promise is drawn
    // on.
    KEST_IR_TEXT_LEN,
    KEST_IR_TEXT_AT,
    KEST_IR_TEXT_IN,
    KEST_IR_TEXT_SLICE,
    KEST_IR_TEXT_REST,
    KEST_IR_TEXT_MATCHES,
    KEST_IR_TEXT_FIND,
    KEST_IR_TEXT_OF,
    KEST_IR_TEXT_JOIN,
    KEST_IR_TEXT_FROM,

    // That many of something, and what is done to it.
    KEST_IR_ARRAY,
    KEST_IR_ARRAY_NEW,
    KEST_IR_LEN,
    KEST_IR_APPEND,
    KEST_IR_FIT,
    KEST_IR_APPEND_TEXT,
    KEST_IR_FIT_TEXT,
    KEST_IR_ROOM,
    KEST_IR_POP_LAST,
    KEST_IR_TAKE,
    KEST_IR_CLEAR,

    // The slot map.
    KEST_IR_STORE_NEW,
    KEST_IR_STORE_ADD,
    KEST_IR_STORE_GET,
    KEST_IR_STORE_SET,
    KEST_IR_STORE_REMOVE,
    KEST_IR_STORE_COUNT,
    KEST_IR_STORE_REF,
    // A walk's step. `for` is the only loop this language has over a
    // sequence, and its step is one thing: count on, decide, and go round
    // again or leave. It is written as one operation because that is what a
    // turn of a walk is, and both backends want the whole of it rather than
    // three quarters of it and a comparison to put back together.
    KEST_IR_NEXT,
    KEST_IR_SEEK_FROM,
    KEST_IR_SEEK_NEXT,

    // Leaving this body.
    KEST_IR_CALL,
    KEST_IR_CALL_VALUE,
    KEST_IR_CALL_HOST,

    // A block whose working memory goes back where it was when it ends. What
    // the program made inside it is gone at the close, so nothing made inside
    // it may be kept: `kest_ir_escapes` is what holds that, and it is here
    // rather than in the walk that wrote the body because it is a question
    // about the whole of one. See D966.
    KEST_IR_REGION_OPEN,
    KEST_IR_REGION_CLOSE,

    // Going somewhere else in the same body. A branch names the operation it
    // lands on; `target` is that, and is filled in when the walk reaches it.
    KEST_IR_GO,
    KEST_IR_ASK,
    KEST_IR_GIVE,
    // Where two ways of getting here meet. The arms of an `if` written as a
    // value and of a `match` each leave one behind them, and from here on it
    // is one value whichever arm ran. Nothing is emitted for it.
    KEST_IR_MEET,
    // An operation that was here and is not. The optimizer takes a movement
    // away by writing this over it rather than by closing the gap: a body is
    // a flat list and a branch names what it lands on, so shifting everything
    // down would renumber every target, every value and every argument for
    // the sake of two operations. Nothing is emitted for it, and what it was
    // is left beside it for whoever reads a body back. See D1025.
    KEST_IR_NOTHING,

    KEST_IR_OP_COUNT
} KestIrKind;

typedef struct {
    uint16_t kind;
    uint16_t effects;
    // What the operation leaves behind, or `KEST_IR_NONE` when it leaves
    // nothing.
    KestIrRef dest;
    // What it reads, in the order it reads them. A call takes as many as the
    // function does, so these are a run in the body's own list rather than
    // three fields that would have to grow.
    uint32_t first_arg;
    uint16_t arg_count;
    // The place it reads or writes, or `KEST_IR_NO_PLACE`.
    uint32_t place;
    // What the operation is about: the result for arithmetic, what was
    // compared for a comparison, what was converted from for a conversion.
    const KestType *type;
    // Numbers the operation carries: which constant, how wide, which layout,
    // which function. What each means is written beside the operation in the
    // list in `ir.c`.
    uint16_t imm[3];
    // Where a branch lands, as the operation it lands on. A body can hold
    // more operations than two bytes count, so this is not one of the three
    // above.
    uint32_t target;
    KestSpan span;
} KestIrOp;

#define KEST_IR_NO_PLACE 0xFFFFFFFFu

// Control flow is branches in the one list rather than a graph beside it: a
// branch names the operation it lands on, and the operations between two
// branches are what runs in a row. A backend that wants blocks works them out
// from the branch targets in one walk, which is cheaper than keeping a second
// shape in step with the first. The mission's suggested form was blocks; what
// it required of them is here, and what a tree walk writes is a list.

// A name the body declared, kept so that a walk over the body can say which
// name a place belongs to without going back to the file.
typedef struct {
    const char *name;
    const KestType *type;
    uint16_t slot;
    uint16_t slots;
    // The slot holds where the value is rather than the value.
    bool by_address;
    KestSpan span;
} KestIrName;

typedef struct {
    // The declaration this is one concrete copy of, and what it is called in
    // the module.
    const char *symbol;
    const KestSource *source;
    KestSpan declared;
    const KestType *signature;

    KestIrOp *ops;
    uint32_t op_count;
    uint32_t op_capacity;

    KestIrRef *args;
    uint32_t arg_count;
    uint32_t arg_capacity;

    KestIrValue *values;
    uint32_t value_count;
    uint32_t value_capacity;

    KestIrPlace *places;
    uint32_t place_count;
    uint32_t place_capacity;

    KestIrName *names;
    uint32_t name_count;
    uint32_t name_capacity;

    KestValue *constants;
    uint8_t *constant_classes;
    uint32_t constant_count;
    uint32_t constant_capacity;

    // How much of this body was worked out where it was written rather than
    // built while running, which is a thing a reader asks of a body and not of
    // a machine. See D887.
    uint32_t folded;
    uint32_t folded_slots;

    // How many slots the frame needs, and how many of them the arguments are.
    uint16_t slot_count;
    uint16_t param_slots;
    uint16_t result_slots;
    // The most values this body has on the go at once, in slots.
    uint16_t stack_needed;
    // What the declaration promised, kept beside what the body does so that a
    // reader of the body alone can tell one from the other.
    bool no_alloc;
    bool no_host;
    bool deterministic;
    bool returns_value;
    // The widest run of slots the optimizer took a copy of. The compiler works
    // out how deep the stack goes from what the body means, and a copy taken
    // away is a push and a pop the reckoning still counts; the lowering carries
    // this into the chunk beside what its own fusions saved, for the reason
    // D1012 put that there. Nought for a body nothing was taken out of.
    uint16_t took_slots;
} KestIrBody;

// What a backend is, from here: something handed one body at a time. It
// answers false when it cannot go on, which stops the walk.
typedef bool (*KestIrWritten)(void *backend, const KestIrBody *body);

// What the optimizer found in one body and what it did about it. Counting
// before changing is the rule this project works by: the first thing the
// optimizer did was say what there was to do, so that the pass which gets
// written is the one the numbers asked for rather than the one on a list.
//
// Static counts, because the IR carries no profile. What a count is worth
// depends on where it is, so these are printed per body and the hot ones are
// read by name. See D1024.
typedef struct {
    uint32_t bodies;
    uint32_t ops;
    // A place read straight into another place, both of them runs of slots in
    // the frame: `b = a` and every argument written into a name.
    uint32_t slot_copies;
    // A slot read again with nothing written to it in between and no branch
    // landing between the two reads. This is what a redundant load is.
    uint32_t reloads;
    uint32_t reloaded_slots;
    // A slot written and written again with nothing reading it in between.
    uint32_t dead_writes;
    // A value built out of pieces and put straight into a place, which is an
    // aggregate materialized on the way to where it was going.
    uint32_t materialized;
    // The same four again, counted only where they are inside a loop. A shape
    // found once in a body that runs once is worth nothing however many of
    // them there are; the same shape between a backward branch and what it
    // lands on is worth as many times as the loop goes round. Which of the
    // two a count is about is the whole of the decision, so both are kept.
    uint32_t hot_copies;
    uint32_t hot_reloads;
    uint32_t hot_dead;
    uint32_t hot_materialized;
    // And what was done about it, which is not the same number: a shape that
    // is there is not always one that can be taken away.
    uint32_t copies_taken;
} KestIrFound;

typedef struct {
    KestArena *arena;
    // Where the arena was before this body. One body is alive at a time: it is
    // written, handed to the backend, and let go. What that saves is the peak,
    // and the peak is what a program compiled under a ceiling meets — holding
    // every body of a program at once put five of the ladder's programs over
    // the reading ceiling that used to reach the one after it.
    KestMark before;
    KestIrWritten written;
    void *backend;
    KestIrBody body;
    bool out_of_memory;
    // What the optimizer found over the whole program, and somewhere to say
    // it per body for whoever is reading. The second is nothing in a normal
    // build: it is set by the command line when it is asked.
    KestIrFound found;
    void (*say_found)(const KestIrBody *body, const KestIrFound *found);
    // The clock the build was given, or NULL, and what the stages that happen
    // inside one body come to by it. They are added up here because this is
    // where they happen, and read by whoever owns the build. `copies` is
    // written by the compiler rather than here: it is the share of all four
    // that went on copies of generic functions. See D1026.
    uint64_t (*now)(void *);
    void *now_context;
    uint64_t verifying;
    uint64_t optimizing;
    uint64_t lowering;
    uint64_t copies;
} KestIrProgram;

// A reading of a clock that may not be there, and nought when it is not.
// Nought at both ends of a stage is what makes it cost nothing to weigh a
// compile nobody asked about.
//
// It is here for the reason `kest_ir_asked_off` is here: one door rather than
// one per module, and this is the module every stage below the build can see.
// The clock itself belongs to whoever called, the same rule `kest_clock` keeps
// and for the same reason -- this library is ISO C and ISO C has no monotonic
// clock. See D1026.
uint64_t kest_ir_ticked(uint64_t (*now)(void *), void *context);

// Whether a switch that turns a transformation off is set. `decided` is where
// the answer is kept and starts below nought: the environment is read once
// and not again, because a compiler that asked it per body would be one whose
// answer could change half way through a program.
//
// There is one of these rather than one per transformation because two would
// be one walk written twice. It is here because this is the module that says
// what a transformation is; `KEST_PLAIN` holds the lowering's fusions and
// `KEST_NOOPT` holds this module's own pass, and D1009 is why either exists.
bool kest_ir_asked_off(const char *name, int *decided);

// Making one, and the one body at a time that goes through it. Every part of a
// body is arena memory and none of it is freed on its own; what frees all of it
// is the end of the body.
void kest_ir_program_init(KestIrProgram *program, KestArena *arena,
                          KestIrWritten written, void *backend);
KestIrBody *kest_ir_body_begin(KestIrProgram *program);
bool kest_ir_body_end(KestIrProgram *program);

uint32_t kest_ir_place_add(KestIrProgram *program, KestIrBody *body,
                           const KestIrPlace *place);
KestIrRef kest_ir_value_add(KestIrProgram *program, KestIrBody *body,
                            const KestType *type, uint16_t slots);
uint32_t kest_ir_name_add(KestIrProgram *program, KestIrBody *body,
                          const KestIrName *name);
// A run of values the body holds, kept in the order they were added: what an
// operation names is where its run starts, so nothing here is shared between
// two of them. The chunk a backend writes does its own sharing. Answers where
// the run starts.
uint32_t kest_ir_constants_add(KestIrProgram *program, KestIrBody *body,
                               const KestValue *values, const uint8_t *classes,
                               uint16_t count);

// Writing one operation. `args` may be NULL when there are none. Answers the
// operation's index, which is what a branch to it is.
uint32_t kest_ir_op(KestIrProgram *program, KestIrBody *body, KestIrKind kind,
                    const KestType *type, const KestIrRef *args,
                    uint16_t arg_count, KestSpan span);

// Says where a branch written earlier lands, which is wherever the body has
// got to now.
void kest_ir_lands_here(KestIrBody *body, uint32_t branch);

// What each operation is called, where a reader sees it. What it does that a
// promise is about is on the operation itself, because that is where anything
// walking a body reads it.
const char *kest_ir_word(KestIrKind kind);

// What a body does that a region cannot allow: something made inside one and
// kept past it. Answers what is wrong and where, or NULL. The rule is that
// shorter-lived memory does not reach longer-lived state, followed through
// every value a body makes. See D966.
const char *kest_ir_escapes(const KestIrBody *body, KestArena *arena,
                            KestSpan *where);

// Holds a body to what a body is: every value made once and read once, every
// branch landing on an operation this body has, and every place
// naming what it reaches. Answers what is wrong, or NULL. This is the check
// that says the builder and the backends agree about the shape of the thing
// between them, and it is asked of every body in the build that checks itself.
const char *kest_ir_verify(const KestIrBody *body);



#endif
