#ifndef KEST_VALUE_H
#define KEST_VALUE_H

#include <stdatomic.h>

#include "kest.h"
#include "types.h"

typedef enum {
    KEST_OP_CONST,   // u16 index
    // A run of them, which is what a constant that is a struct or that many of
    // something is: one instruction and one copy rather than a push a slot.
    KEST_OP_CONST_RUN, // u16 first index, u16 count
    // One of a run of them, at an index worked out while running. The run is
    // in the chunk, so nothing is copied into slots to read one of it.
    KEST_OP_CONST_AT,  // u16 first index, u16 stride, u16 how many
    KEST_OP_LOAD,    // u16 slot
    KEST_OP_STORE,   // u16 slot
    // The multi-slot forms. A struct is a value laid out flat, so moving one
    // is moving a run of slots rather than following a pointer.
    KEST_OP_LOADN,   // u16 slot, u16 count
    KEST_OP_STOREN,  // u16 slot, u16 count
    // Two pushes in one instruction, which is what the two commonest pairs in
    // this machine are: a local and another local that is not beside it, and a
    // local and a constant. Counted over the frame step, those two pairs are a
    // quarter of everything it runs -- a push is the cheapest thing the
    // machine does and it is most of what it does, so the dispatch is the
    // cost. Neither is a new thing the machine can do: each is the two
    // instructions it replaces, written as one. See D961.
    KEST_OP_LOAD2,   // u16 slot, u16 slot
    KEST_OP_LOADK,   // u16 slot, u16 constant
    // Keeps one member of the struct on top of the stack and drops the rest.
    // Only needed where the struct is not rooted in a slot, because a field of
    // a local is reached by adding to the slot number instead.
    KEST_OP_FIELD,   // u16 offset, u16 size, u16 total
    // Takes count elements of stride slots each off the stack and leaves a
    // handle in their place.
    // The layout is the module's, and it is what an element is in memory:
    // an array of `f32` is four bytes an element and can be the array the
    // host already has.
    KEST_OP_ARRAY,      // u16 count, u16 layout
    // An array of a size nobody wrote down, and one more element on the end.
    // Growing moves the elements, so a borrowed block cannot be grown and the
    // machine says so rather than writing past what it was lent.
    KEST_OP_MAKE_ARRAY, // u16 layout
    KEST_OP_PUSH,
    // An append that never grows. `push` may double a block and so can never
    // be inside a `no.alloc` promise; this refuses where that one would grow,
    // which is what lets a body fill a buffer it was given room for without
    // reaching the heap. See D940.
    KEST_OP_FIT,
    // The same two given a whole piece of text rather than one byte. Text is
    // its bytes (D021), so a run of bytes and a piece of text are the same
    // bytes and the piece goes on the end whole. What that is worth is the
    // loop it takes the place of: building text a byte at a time was eighty
    // per cent of the instructions `bench/words.kest` runs. See D1068.
    KEST_OP_PUSH_TEXT,
    KEST_OP_FIT_TEXT,
    // Room for that many without changing what is in it or how many there
    // are. `array(n, v)` makes a new one with room and `clear` empties it,
    // which is what a program does when it knows how many are coming -- and
    // there was nothing that did it to one it already had. A table's keys and
    // its values are arrays it cannot replace, so being told how many pairs
    // are coming bought half of what it buys an array. See D912.
    KEST_OP_ROOM,       // u16 layout
    KEST_OP_INDEX,      // u16 layout
    // An element read out of a run a local holds, by an index a local holds:
    // `load2` and then `index` was two dispatches for every `xs[i]` in a loop.
    // See D1155.
    KEST_OP_INDEX_LL,   // u16 run's slot, u16 index's slot, u16 layout
    KEST_OP_POP_LAST,   // u16 layout, leaves an optional
    KEST_OP_TAKE,       // u16 layout, shifts what is after it down
    KEST_OP_CLEAR,
    // The address of an element, so a path that reaches through an array can
    // be written to. The address lives for one statement, during which
    // nothing can move what it points at.
    KEST_OP_ELEM_ADDR,  // u16 layout
    // One of an array, at a byte into it: the array and the index taken away
    // and the piece pushed. `elem.addr` and the `load.at` after it, which is
    // what reading a field of an element was written as and what a frame
    // reads most. See D1044.
    KEST_OP_ELEM_AT,  // u16 offset, u16 layout
    // That many of something, laid out where it stands. The index is worked
    // out while running, so these take a base and a stride rather than the
    // single slot `load` and `store` take.
    KEST_OP_LOAD_SLOTS,  // u16 base, u16 stride, u16 count
    KEST_OP_STORE_SLOTS, // u16 base, u16 stride, u16 count
    // The address of one of them inside memory the host laid out.
    KEST_OP_OFFSET_ADDR, // u16 stride, u16 count
    KEST_OP_LOAD_AT,    // u16 byte offset, u16 layout
    KEST_OP_LEN,
    // A piece of text is a pointer and nothing else, so its length is counted
    // rather than read. One byte of it is a `u8`; there is no character type
    // and nothing here pretends to decode one.
    KEST_OP_TEXT_LEN,
    KEST_OP_TEXT_AT,
    // The byte at a place in text a walk has already measured. It does not
    // look again: text does not change, the walk took its length when it
    // began, and the count it is reading with is the walk's own. `text.at`
    // is what a program's own index compiles to and that one measures.
    KEST_OP_TEXT_IN,     // u16 text slot, u16 index slot
    // A piece of a piece of text is a new one, because a piece of text is a
    // pointer to something that ends in a nought and a window into the middle
    // of one is not that. Finding is only reading and costs nothing.
    KEST_OP_TEXT_SLICE,
    // What is left of a piece of text from a place in it. A piece ends where
    // it ends, so the rest of one is a place inside it and nothing is copied.
    KEST_OP_TEXT_REST,
    // Whether a piece of text sits at a place in another. It compares where
    // it is told rather than looking for it, so what it costs is the place it
    // steps to and the piece it compares, and nothing is copied to do it.
    KEST_OP_TEXT_MATCHES,
    KEST_OP_TEXT_FIND,
    // Text is built rather than found, so each of these reaches the heap and
    // the contract charges for it.
    KEST_OP_TEXT_I,
    KEST_OP_TEXT_U,
    KEST_OP_TEXT_F,
    KEST_OP_TEXT_F32,
    KEST_OP_TEXT_B,
    // u16 layout. The names come from the type the layout was made for, and
    // what is written is the source that builds the value.
    KEST_OP_TEXT_FLAGS,
    // The text of a value laid out flat: an enum, a struct, that many of
    // something where it stands, or an optional holding one of those. Named
    // for what it walks rather than for the first thing that needed it, the
    // way the three beside it were in D874. See D876.
    KEST_OP_TEXT_VALUE,
    KEST_OP_CONCAT,     // u16 count
    // A number standing for a value, over exactly what `==` applies to.
    KEST_OP_HASH_I,
    KEST_OP_HASH_F,
    KEST_OP_HASH_T,
    // u16 layout. Over a value laid out flat and everything in it: an enum's
    // tag and whatever its case carries, or a struct's fields. Named for what
    // they walk rather than for the first thing that needed them — a value is
    // a value, and what says whether it can be compared at all is whether
    // everything in it can. See D874.
    KEST_OP_HASH_VALUE,
    KEST_OP_EQ_VALUE,
    KEST_OP_NE_VALUE,
    KEST_OP_TEXT_FROM,  // an array of bytes becomes one piece of text
    // The slot map. A reference is an index with the generation it was handed
    // out at packed above it, so a read can tell a live one from a stale one
    // without anything having been notified of the removal.
    KEST_OP_NEW_STORE,  // u16 stride, u16 layout of a place
    // A place in an array, kept as what it is made of rather than as an
    // address. The address is worked out where it is used, so nothing that
    // happens in between can move the block out from under it. See D931.
    KEST_OP_LOAD_ELEM,  // u16 offset, u16 layout; reads, leaves the place
    KEST_OP_STORE_ELEM, // u16 offset, u16 layout; writes, takes the place
    // An element read straight into the frame, and one written straight out of
    // it. `one = world[at]` is an index that unpacks a struct onto the stack
    // and a store that copies it off again, and `world[at] = one` is the same
    // the other way round -- four slots pushed and four popped for every four
    // that had to move at all. These move them once.
    //
    // A dependency rather than a push, which is what D1011 says to fuse: the
    // store reads exactly what the index just wrote, through memory, and
    // neither instruction can start until the other has finished. See D1012.
    KEST_OP_INDEX_TO,   // u16 layout, u16 slot; takes the place, writes slots
    KEST_OP_ELEM_FROM,  // u16 offset, u16 layout, u16 slot; takes the place
    // Arithmetic that writes where the answer is going. `sum += one.x` is an
    // addition that pushes and a store that pops what it pushed: the same
    // dependency through memory D1012 took out of an element move, and the
    // commonest pair the seven programs run after that one. Four of them
    // rather than one with the arithmetic as an operand, because the one that
    // read its kind out of an operand is the one that bought nothing (D1011).
    // See D1014.
    KEST_OP_ADD_I_NARROW_TO, // u16 scalar kind, u16 slot
    KEST_OP_SUB_I_NARROW_TO, // u16 scalar kind, u16 slot
    KEST_OP_ADD_F_TO,        // u16 slot
    KEST_OP_SUB_F_TO,        // u16 slot
    KEST_OP_ADD,        // u16 stride
    KEST_OP_GET,        // u16 stride, leaves an optional
    KEST_OP_SET,        // u16 stride
    KEST_OP_REMOVE,
    KEST_OP_COUNT,
    // Walking a store. The first live slot at or after one, and the reference
    // that names a slot, kept apart so the loop can hold its place between
    // turns without holding anything the program can see.
    // Finding the next live slot of a store and leaving when there is none.
    // A store's walk cannot count to a limit, because slots go dead; these are
    // to it what the counting instructions are to every other walk.
    KEST_OP_SEEK_FROM,   // u16 store slot, u16 index slot, u16 forward offset
    KEST_OP_SEEK_NEXT,   // u16 store slot, u16 index slot, u16 backward offset
    KEST_OP_STORE_REF,
    KEST_OP_TRUE,
    KEST_OP_FALSE,
    KEST_OP_POP,
    KEST_OP_POPN,    // u16 count
    // Turns the top run of slots over end to end. A case is built payload
    // first and tag last, because that is the order it is written in, and is
    // laid out tag first, because that is the order it is read in.
    KEST_OP_ROTATE,     // u16 count

    KEST_OP_ADD_I,
    KEST_OP_SUB_I,
    KEST_OP_MUL_I,
    KEST_OP_DIV_I,
    KEST_OP_MOD_I,
    KEST_OP_DIV_U,
    KEST_OP_MOD_U,
    KEST_OP_NEG_I,
    KEST_OP_AND_I,
    KEST_OP_OR_I,
    KEST_OP_XOR_I,
    KEST_OP_NOT_I,
    KEST_OP_SHL,
    KEST_OP_SHR_I,
    KEST_OP_SHR_U,
    // Cuts a result down to the width its type declares. A slot is sixty-four
    // bits and an `i8` is eight, and what the engine on the other side gets
    // is the eight.
    KEST_OP_NARROW,     // u16 scalar kind
    // The three that arrive with a cut behind them, in one instruction. Every
    // `+`, `-` and `*` on a whole number narrower than a slot is one of these
    // followed by a `narrow`, which is two dispatches for one piece of
    // arithmetic — 484 of the 572 cuts every example and library module makes
    // between them. The width stays an operand rather than becoming six
    // instructions each: what a dispatch costs is the branch, not the two
    // bytes read after it. See D868.
    KEST_OP_ADD_I_NARROW,   // u16 scalar kind
    KEST_OP_SUB_I_NARROW,   // u16 scalar kind
    KEST_OP_MUL_I_NARROW,   // u16 scalar kind
    // Between the two families. Nothing crosses on its own, so each of these
    // is somewhere a type was named.
    KEST_OP_I2F,
    KEST_OP_U2F,
    KEST_OP_F2I,        // u16 scalar kind, saturating
    KEST_OP_TO_F32,

    KEST_OP_ADD_F,
    KEST_OP_SUB_F,
    KEST_OP_MUL_F,
    KEST_OP_DIV_F,
    KEST_OP_MOD_F,
    KEST_OP_NEG_F,
    // A slot holds a double, but `f32` arithmetic must round to `f32` or the
    // answer is not the one the engine on the other side of the boundary
    // gets. Rounding the double result is exact for these five.
    KEST_OP_ADD_F32,
    KEST_OP_SUB_F32,
    KEST_OP_MUL_F32,
    KEST_OP_DIV_F32,
    KEST_OP_MOD_F32,
    KEST_OP_NEG_F32,

    KEST_OP_LT_I,
    KEST_OP_LE_I,
    KEST_OP_GT_I,
    KEST_OP_GE_I,
    KEST_OP_LT_U,
    KEST_OP_LE_U,
    KEST_OP_GT_U,
    KEST_OP_GE_U,
    KEST_OP_LT_F,
    KEST_OP_LE_F,
    KEST_OP_GT_F,
    KEST_OP_GE_F,

    // Equality is one comparison per storage class, not one per type.
    KEST_OP_EQ_I,
    KEST_OP_NE_I,
    KEST_OP_EQ_F,
    KEST_OP_NE_F,
    KEST_OP_EQ_T,
    KEST_OP_NE_T,
    // Text compares by its bytes, which is an order that is the same
    // everywhere rather than one that depends on where the program is run.
    KEST_OP_LT_T,
    KEST_OP_LE_T,
    KEST_OP_GT_T,
    KEST_OP_GE_T,

    KEST_OP_NOT,

    KEST_OP_JUMP,        // u16 forward offset
    KEST_OP_JUMP_FALSE,  // u16 forward offset, pops
    // A comparison of whole numbers and the jump that reads it, as one
    // instruction: `if a < b` and `while a < b` are what a frame is made of,
    // and both were two dispatches where the second only ever read what the
    // first had just written. The compiler makes these where it emits the
    // jump and nowhere else, so a comparison whose answer is used rather than
    // branched on is still its own instruction.
    // `a || b` asks whether the first one is true, and `!x` asks the same
    // question of one thing, so both were written as `not` and then a jump
    // that reads what `not` wrote. The jump asks it directly.
    KEST_OP_JUMP_TRUE,   // u16 forward offset, pops
    KEST_OP_JUMP_FALSE_LT_I, // u16 forward offset, pops two
    KEST_OP_JUMP_FALSE_LE_I,
    KEST_OP_JUMP_FALSE_GT_I,
    KEST_OP_JUMP_FALSE_GE_I,
    KEST_OP_JUMP_FALSE_EQ_I,
    KEST_OP_JUMP_FALSE_NE_I,
    KEST_OP_JUMP_TRUE_LT_I,
    KEST_OP_JUMP_TRUE_LE_I,
    KEST_OP_JUMP_TRUE_GT_I,
    KEST_OP_JUMP_TRUE_GE_I,
    KEST_OP_JUMP_TRUE_EQ_I,
    KEST_OP_JUMP_TRUE_NE_I,
    KEST_OP_JUMP_FALSE_LT_F,
    KEST_OP_JUMP_FALSE_LE_F,
    KEST_OP_JUMP_FALSE_GT_F,
    KEST_OP_JUMP_FALSE_GE_F,
    KEST_OP_JUMP_FALSE_EQ_F,
    KEST_OP_JUMP_FALSE_NE_F,
    KEST_OP_JUMP_TRUE_LT_F,
    KEST_OP_JUMP_TRUE_LE_F,
    KEST_OP_JUMP_TRUE_GT_F,
    KEST_OP_JUMP_TRUE_GE_F,
    KEST_OP_JUMP_TRUE_EQ_F,
    KEST_OP_JUMP_TRUE_NE_F,
    // A local weighed against a constant and the jump that reads it, as one
    // instruction: `load.k` and then one of the six above was two dispatches
    // for `if one.hp > 20` and every flag test, which are what a rule is made
    // of. Whole numbers only, and only where the jump is taken when the
    // answer is no. See D1154.
    // A local moved by a constant in place: `load.k`, the arithmetic and the
    // store it was written into, which is every `count += 1` and every timer
    // that runs down. The cut is the kind's, as in the two above. See D1155.
    // A constant written into a local: `const` and `store`, which is every
    // `let x = 0` and every reset a rule makes. See D1155.
    KEST_OP_STORE_K, // u16 slot, u16 constant
    KEST_OP_ADD_K_SELF, // u16 kind, u16 slot, u16 constant
    KEST_OP_SUB_K_SELF, // u16 kind, u16 slot, u16 constant
    KEST_OP_JUMP_FALSE_LT_K, // u16 slot, u16 constant, u16 forward offset
    KEST_OP_JUMP_FALSE_LE_K,
    KEST_OP_JUMP_FALSE_GT_K,
    KEST_OP_JUMP_FALSE_GE_K,
    KEST_OP_JUMP_FALSE_EQ_K,
    KEST_OP_JUMP_FALSE_NE_K,
    // And what is on the stack weighed against a constant: `const` and then
    // one of the six whole-number jumps, which is every `xs[i] > 0` and every
    // answer of a call compared with a written number. See D1155.
    KEST_OP_JUMP_FALSE_LT_C, // u16 constant, u16 forward offset, pops one
    KEST_OP_JUMP_FALSE_LE_C,
    KEST_OP_JUMP_FALSE_GT_C,
    KEST_OP_JUMP_FALSE_GE_C,
    KEST_OP_JUMP_FALSE_EQ_C,
    KEST_OP_JUMP_FALSE_NE_C,
    // A float local weighed against a constant and the jump, both ways round,
    // because a float comparison is written either way by `||` and `&&`:
    // `load.k` and one of the twelve float jumps above, which is every
    // `one.x < 0.0` a frame asks of a position. See D1165.
    KEST_OP_JUMP_FALSE_LT_FK, // u16 slot, u16 constant, u16 forward offset
    KEST_OP_JUMP_FALSE_LE_FK,
    KEST_OP_JUMP_FALSE_GT_FK,
    KEST_OP_JUMP_FALSE_GE_FK,
    KEST_OP_JUMP_FALSE_EQ_FK,
    KEST_OP_JUMP_FALSE_NE_FK,
    KEST_OP_JUMP_TRUE_LT_FK,
    KEST_OP_JUMP_TRUE_LE_FK,
    KEST_OP_JUMP_TRUE_GT_FK,
    KEST_OP_JUMP_TRUE_GE_FK,
    KEST_OP_JUMP_TRUE_EQ_FK,
    KEST_OP_JUMP_TRUE_NE_FK,
    // A float sum or difference of two locals written into a local: `load2`
    // and the arithmetic that writes where it is going, which is every
    // `one.x += one.dx` a frame moves something by. See D1166.
    KEST_OP_ADD_F_LL, // u16 slot, u16 slot, u16 slot
    KEST_OP_SUB_F_LL,
    // An element read into the frame and an element written out of it, with
    // the run and the index read where they are: `load2` and `index.to`, and
    // `load2` and `elem.from`, which is every `let one = world[at]` and every
    // `world[at] = one` a frame walks a world with. See D1167.
    KEST_OP_INDEX_TO_LL, // u16 layout, u16 slot, u16 run, u16 index
    KEST_OP_ELEM_FROM_LL, // u16 offset, u16 layout, u16 slot, u16 run, u16 index
    // Whole-number arithmetic with a constant for its right side: on what is
    // on the stack (`.c`, `const` and the arithmetic) or on a local (`.k`,
    // `load.k` and the arithmetic), which is every `% 7`, `* 3` and `+ 1` a
    // rule works a number out with. The narrowing ones carry the kind the
    // answer is cut to. See D1168.
    KEST_OP_MOD_I_C, // u16 constant
    KEST_OP_MOD_I_K, // u16 slot, u16 constant
    KEST_OP_DIV_I_C,
    KEST_OP_DIV_I_K,
    KEST_OP_ADD_I_NARROW_C, // u16 kind, u16 constant
    KEST_OP_ADD_I_NARROW_K, // u16 kind, u16 slot, u16 constant
    KEST_OP_SUB_I_NARROW_C,
    KEST_OP_SUB_I_NARROW_K,
    KEST_OP_MUL_I_NARROW_C,
    KEST_OP_MUL_I_NARROW_K,
    // An `f32` as its thirty-two bits and back: a slot holds one widened to
    // an `f64`, so the two are a conversion each way. See D1171.
    // A float as the bits it is, `u16` 32 or 64 for which width, and every
    // value that is not a number as the one such value, because the sign and
    // the payload of one are what the processor made and differ between
    // machines. See D1238.
    KEST_OP_FLOAT_BITS,
    KEST_OP_BITS_F32,
    // An element of a run read by a run and an index where they are, weighed
    // against a constant, and the jump: `index.ll`, `const` and one of the six
    // whole-number jumps, which is every `if one.cools[at] > 0` a rule asks of
    // a timer. See D1178.
    KEST_OP_JUMP_FALSE_LT_E, // u16 run, u16 index, u16 layout, u16 constant,
                             // u16 forward offset
    KEST_OP_JUMP_FALSE_LE_E,
    KEST_OP_JUMP_FALSE_GT_E,
    KEST_OP_JUMP_FALSE_GE_E,
    KEST_OP_JUMP_FALSE_EQ_E,
    KEST_OP_JUMP_FALSE_NE_E,
    KEST_OP_LOOP,        // u16 backward offset
    // The whole of a counted walk's turn: add one to the count, compare it
    // with the limit beside it, and go back while it is less. The test is at
    // the bottom and the one before the first turn is written above the loop,
    // so a turn costs one instruction rather than five.
    KEST_OP_NEXT_LESS_I, // u16 slot, u16 limit slot, u16 backward offset
    KEST_OP_NEXT_LESS_U, // u16 slot, u16 limit slot, u16 backward offset

    // Working memory: where the heap is, kept in a slot, and the heap put back
    // to it. What the program made in between is gone. A body that is refused
    // does not reach the second of these, so the machine puts back what a run
    // left open rather than trusting the code to. See D966.
    KEST_OP_SCRATCH,     // u16 slot
    KEST_OP_UNSCRATCH,   // u16 slot
    KEST_OP_CALL,        // u16 function, u16 argument slots
    // Through a value rather than a name. Which function it is sits on top of
    // the arguments; what it promises is in its type, so a cost contract is
    // still proved without knowing which one it will be.
    KEST_OP_CALL_VALUE,  // u16 argument slots
    // Into the host. The index is into the module's list of what it declared,
    // which is resolved by name before the program runs.
    KEST_OP_CALL_HOST,   // u16 extern, u16 argument slots, u16 result slots
    KEST_OP_RETURN,  // u16 count
    // Where a debugger put one. Nothing compiles to this: `kest debug` writes
    // it over the first byte of an instruction, keeps the byte it wrote over,
    // and puts it back when the machine stops there -- which is how a
    // breakpoint costs the machine nothing at all when nobody is debugging.
    // The machine stops with its frames where they are and a host carries on
    // with `kest_resume`. See D991.
    KEST_OP_STOP,
} KestOp;

// What a constant's bits mean. The virtual machine never reads this; it is
// what lets the disassembler print a constant rather than its bits.
typedef enum {
    KEST_CONST_INT,
    KEST_CONST_FLOAT,
    KEST_CONST_TEXT,
    // Which function of the program a function value is: a number the machine
    // calls through, which the verifier holds to being called with what that
    // function takes. See D1242.
    KEST_CONST_FN,
} KestConstClass;

// One name a body gave a slot. `kind` is what a layout piece is, so a
// debugger reads the slot as what it holds rather than as bits -- and
// `at_address` is whether the slot holds where the value is rather than the
// value, which a `for` that binds its element by address does. Without it a
// host reads a pointer as a number and has no way to know. See D866 and
// D1081.
typedef struct {
    const char *name;
    uint16_t slot;
    uint16_t slots;
    uint8_t kind;
    bool at_address;
} KestNamed;

// A body the host's compiler compiled, standing in for the instructions of
// the chunk it belongs to. Its arguments are the slots at `frame`, laid out
// the way a call leaves them; it writes its answer over them and says in
// `gave` how many slots came back, which is what `return` does. False means it
// stopped and said why, the same as an instruction that fails.
//
// It is not `KestNative`: that is a function the *host* provides and the
// program calls, and this is a body of the program the host's compiler wrote.
// Both are C functions handed a frame and they are the two ends of different
// boundaries. See D1094.
typedef bool (*KestNativeBody)(KestRuntime *runtime, KestValue *frame,
                               uint16_t *gave);

typedef struct {
    const char *name;
    // The same name as it was written, which is the name without what tells
    // one copy of a generic from another: `shapes.kept` for
    // `shapes.kept#[T],fn(T) -> bool`. Worked out when the chunk is made,
    // because a message that says which function it is about says it every
    // time and a host walking the list reads it for every function — and
    // working it out is a copy of the name, which is a thing to hand out once
    // rather than once an asking. See D610.
    const char *wrote;
    // The file this was compiled from, so a failure while running reports in
    // the same place a failure to compile would have.
    const KestSource *source;
    // And where in it the declaration this was compiled from is written. Two
    // chunks written the same are either one generic compiled twice or two
    // declarations of one name, and nothing said which: this does, because
    // copies of one declaration are written in one place. See D612.
    KestSpan declared;
    uint8_t *code;
    uint32_t code_count;
    uint32_t code_capacity;
    // The source offset each instruction came from, so a runtime failure can
    // be reported where a compile failure would have been. One per
    // instruction and in the order they were written, which is why reading one
    // means walking the code: the middle of a jump's operand came from
    // nowhere, and an instruction is nearly three bytes, so one a byte was
    // four fifths of what a module held of a function. See D751.
    uint32_t *origins;
    uint32_t origin_count;
    uint32_t origin_capacity;
    // Where the next instruction starts, which is how a byte handed over on
    // its own is told from an operand: the one at this offset is an opcode and
    // the ones after it are what it carries.
    uint32_t next_instruction;
    KestValue *constants;
    uint8_t *constant_classes;
    uint32_t constant_count;
    uint32_t constant_capacity;
    // How many of this function's values were worked out where they stand
    // rather than built by instructions every time it runs: a case written in
    // a body, a hash of a piece of text, a run of numbers. It is the
    // difference between a frame that costs nothing for one and a frame that
    // pays for it, and nothing said it per function until now. See D678.
    uint32_t folded;
    // And how many slots those values take, which is what says whether eight
    // of them are eight numbers or eight structs. A value worked out where it
    // stands is written into the chunk a slot at a time, so this is the size
    // of what the function was given. See D679.
    uint32_t folded_slots;
    // The names this body gave its slots, for a debugger and for nothing
    // else. Without them a stopped machine can say slot 4 holds 12 and cannot
    // say that slot 4 is `hungry` -- which is the difference between a
    // debugger and a memory viewer. It is the only thing in a chunk that is
    // about the source rather than about running, and it is what a chunk
    // costs for that: one entry a name, and a body with no names has none.
    //
    // No liveness in it: a name is written down once with the slot it was
    // given, and a body that reuses a slot after a scope ends has two names
    // for it and both are shown. A debugger says so. See D991.
    KestNamed *named;
    uint16_t named_count;
    uint16_t named_capacity;
    // In slots, not in names: a struct parameter is a run of them.
    uint16_t param_slots;
    // What each of them is, in the order they are written: an index into the
    // module's layouts, which says both how wide the argument is and what is
    // in it. A host asks where an argument starts rather than counting the
    // scalars of the ones before it, and asks what it is rather than trusting
    // that its own idea of the type is the program's.
    uint16_t *takes;
    uint16_t takes_count;
    // And what comes back, the same way: an index into the module's layouts,
    // read only when the function gives something.
    uint16_t gives;
    // What it gives back, so a host can be told how wide a frame has to be
    // without the types being around to ask.
    uint16_t result_slots;
    uint16_t slot_count;
    // How deep the operand stack gets. The compiler knows it exactly, so the
    // machine checks for room once per call instead of once per push.
    uint16_t stack_needed;
    // And by how much the number above may now be more than enough. The
    // compiler works out how deep the stack goes from what the body means;
    // the lowering then takes the widest element move off the stack
    // altogether (D1012), which the compiler's reckoning does not know about.
    // Reducing the number above by this would be wrong -- the deepest moment
    // may be somewhere else entirely -- so what is recorded is the slack, and
    // `check-costs.sh` holds a body to asking for no more than its deepest
    // run used plus this. Nought for a body nothing was fused in.
    uint16_t fused_slots;
    // The functions carried into this body at their calls rather than called
    // (D1156). The machine never enters them from here, and the other backend
    // does: a machine is sized for both engines, so a walk of what a body
    // needs counts each of these as the call it would have been.
    uint16_t *carried;
    uint16_t carried_count;
    uint16_t carried_room;
    // Whether anything in this program ever names this function as a value.
    // A call through a value enters one of these and nothing else, so it is
    // what a walk that meets one has to look at — and a program that names
    // none of them can only be handed one by a host. See D814.
    bool as_value;
    // The deepest the machine ever got in this body, which only the build
    // that checks itself counts. It is what says the number above is not
    // merely enough but no more than enough. See D812.
    uint32_t went;
    bool returns_value;
    // What the declaration promised. The promise is checked against the tree
    // before anything is emitted; this is what lets it be checked again
    // against what was emitted. See D058. Both of them, because both are
    // proved twice and the second proof reads what is written here. See D853.
    bool no_alloc;
    // Whether nothing this body does can make an array shorter or take one
    // away: no taking, emptying or resizing, no call through a value or into
    // the host, and calls only to bodies that keep them too, compiled before
    // it. What lets a walk that calls it be proved inside its array. Set by
    // the compiler when the body is finished. See D1188.
    bool keeps_runs;
    bool no_host;
    bool deterministic;
    // The same body, compiled by the host's compiler out of the C this
    // project's other backend wrote, or NULL for a body the machine runs. A
    // call enters it instead of the instructions, and everything around the
    // call is unchanged: the frame is where the caller left it, the answer
    // goes where a `return` would put it, and a fault inside it is reported
    // the way one inside the instructions is. See D1094.
    KestNativeBody native;
} KestChunk;

// A function the program declared and the host must provide.
typedef struct {
    const char *name;
    KestSpan span;
    const KestSource *source;
    // What the program expects it to take and to give back, the same way a
    // chunk says it: an index into the module's layouts for each argument in
    // the order they are written, and one for the answer. A host binds a C
    // function and nothing else checks that the two agree about what crosses.
    uint16_t *takes;
    uint16_t takes_count;
    uint16_t gives;
    bool gives_value;
    // What the program was told about the heap. An extern declared `no.alloc`
    // is a promise made on the host's behalf by whoever wrote the declaration,
    // and it is the one promise in this language that the machine has to hold
    // somebody else to.
    bool promises;
    // And whether it promises to be inside the simulation profile, which is
    // what lets a `deterministic` body call it. See D942.
    bool deterministic;
} KestExtern;

// A value holding a tag, written out as what moving it does: one step a
// scalar, at the slot and the byte it is at, and one step a tag, which reads
// the tag and then the steps of the case it names. Built once with the layout,
// because a type walked every time an element moved was a third of what
// `bench/rules.kest` spent. See D1159.
#define KEST_MOVE_CASES 0xFF
// A step of more than one of a kind is that kind with this bit set, so a step
// of one is read the way it always was and only a run pays for a loop.
#define KEST_MOVE_RUN 0x80
_Static_assert(KEST_L_REF < KEST_MOVE_RUN, "a kind leaves room for a run");
typedef struct {
    // A `KEST_L_*` kind, that kind with `KEST_MOVE_RUN` set, or
    // `KEST_MOVE_CASES`.
    uint8_t kind;
    uint16_t slot;
    uint32_t byte;
    // How many of the kind lie side by side from here, in the slots and in the
    // bytes, which is moved as one run: a struct of four `f32` is one step
    // rather than four, and a run of handles or of text is one copy. See D1177.
    uint16_t many;
    // For a tag: how many slots and bytes the enum is, which case steps come
    // first in the ranges, and how many cases there are.
    uint16_t slots;
    uint32_t size;
    uint32_t cases;
    uint32_t case_count;
    const KestType *type;
} KestMoveStep;

typedef struct {
    uint32_t first;
    uint32_t count;
} KestMoveRun;

typedef struct {
    const KestMoveStep *steps;
    const KestMoveRun *ranges;
    // How many of the steps are the value's own, rather than a case's.
    uint32_t count;
} KestMoving;

typedef struct {
    KestArena *arena;
    // What the file that was named calls itself. A host writes `spawn` and
    // the program registered `world.spawn`, and this is what tells them apart
    // without the host having to know there was a difference.
    const char *alias;
    KestChunk **functions;
    uint32_t count;
    uint32_t capacity;
    // Where each function is by its name: an open-addressed table of places
    // in `functions`, each one more than the place so that nought is empty,
    // kept at least twice as big as there are functions. A name is looked for
    // at every call the compiler writes and at every function it registers,
    // and a walk of the list for each made compiling a program of a thousand
    // modules grow as the square of it. See D1261.
    uint32_t *places;
    uint32_t place_capacity;
    KestExtern *externs;
    uint32_t extern_count;
    uint32_t extern_capacity;
    // How many machines are standing on this program. Freeing the build takes
    // the program out from under every one of them, so the build is refused
    // while any of them is still there. It is here because what the machines
    // have in common is the build, and it is the one field of a build a
    // machine writes -- which is why it is an atomic: two machines started on
    // two threads count themselves up at once, and a count that is not one is
    // a build freed under a machine that is still running. See D324 and D952.
    //
    // What said a stamp counter belonged here too was D316, and D936 moved it
    // into the machine: two machines of one build are two worlds and a
    // reference carries which. Nothing counts stamps here now.
    atomic_uint machines;
    // Whether a chunk could not be given another byte or another constant.
    // What that leaves behind is a body with the end missing, which reads as
    // an instruction of the wrong width to anything that walks it -- so the
    // proof below would say the two halves of this compiler disagree about
    // what a program is, about a machine that ran out. The one thing that
    // happened is said by whoever notices. See D750.
    bool out_of_room;
    // Whether small bodies are carried to their calls rather than called. On
    // for every build but one that is going to be profiled: a profile says how
    // many times each body was entered, which is a question about the program
    // as written rather than about how it was written out. See D1156.
    bool carrying_off;
    KestLayout *layouts;
    const KestType **layout_types;
    uint32_t layout_count;
    uint32_t layout_capacity;
} KestModule;

// The least a machine can be given: the deepest run of frames any call can
// make, and the slots those frames take together. False when there is no
// answer, and `why` says which of the two it was and in which function.
//
// `only` is which function to answer for, or -1 for every one of them, which
// is what a host that has not said which it calls has to be given.
//
// `from_host_slots` and `from_host_frames` are where the machine is at the
// deepest place it calls into the host, which is where a host function that
// calls back in starts from. Both are nought when nothing reaches a host
// function, and either may be NULL for a caller that is not asking.
// What one function needs, and why it has none where it has none. A machine
// that will only ever be called at one function needs what that function
// reaches rather than what the worst of them does, and the walk that answers
// for the whole program works out both on the way: `slots` and `frames` are
// what a machine to call this one takes. `reach` is nought where there is an
// answer, and where it is not, `from` is the function it came from — a caller
// of a function with no answer has none either, and what a reader wants is the
// one with the `call.value` or the loop in it. See D602 and D603.
typedef struct {
    uint32_t slots;
    uint32_t frames;
    uint32_t from;
    uint8_t reach;
} KestNoLeast;

// One walk of the whole program, kept. A module does not change after it is
// compiled, so the answer does not either: what it needs, where it calls into
// the host and which function that is. A host asks for it and every machine
// asks for it again, and working it out costs more scratch than a machine is
// made of. See D607.
typedef struct {
    bool taken;
    bool measured;
    uint32_t slots;
    uint32_t frames;
    uint32_t host_slots;
    uint32_t host_frames;
    // The widest one body of this program ever is, which is the one number a
    // program with no least still has: a frame is at most this, whatever the
    // run of calls above it turned out to be. A program that reaches itself
    // has no worst chain to add up and a ceiling on frames all the same, so
    // what it needs is at most this many slots a frame. See D815.
    uint32_t widest;
    // And the same question asked of the shape of the calls rather than of
    // every body: the widest body that goes round, and the whole of the ones
    // that do not. A chain of frames is those two, and the second cannot
    // repeat. Worked out only for a program with no least. See D816.
    uint32_t in_a_turn;
    uint32_t off_the_turns;
    KestReason why;
} KestWalk;

// The two numbers a program with no least is bounded by. `widest_in_a_turn` is
// the widest body of the ones that lie on a run of calls that comes back round,
// and `all_the_rest` is the sum of the widths of every body that does not — a
// body off a cycle can stand in a chain of frames at most once, because twice
// would be a cycle through it, so the whole of them together is a bound on
// what the chain's acyclic frames cost. Nought for both when there is no room
// to work them out. See D816.
// `only` is one function and what it reaches, or -1 for every function the
// program defines. `widest` is the widest body among them, whichever they are.
//
// `to_host` counts only the bodies that reach a host function, which are the
// only ones that can stand in a chain of frames ending at one: a body that
// never reaches the host is not below a host call and not above one either.
// See D818.
void kest_module_cycles(const KestModule *module, KestArena *arena,
                        int32_t only, bool to_host, uint32_t *widest,
                        uint32_t *widest_in_a_turn, uint32_t *all_the_rest);

bool kest_module_needs(const KestModule *module, KestArena *arena, int32_t only,
                       uint32_t *stack_slots, uint32_t *call_depth,
                       uint32_t *from_host_slots, uint32_t *from_host_frames,
                       KestNoLeast *reasons, KestReason *why);

void kest_module_init(KestModule *module, KestArena *arena);
KestChunk *kest_module_add(KestModule *module, const char *name);
// The index of a function by name, or -1. Calls are resolved through this, so
// a chunk holds an index rather than a pointer and stays copyable.
int32_t kest_module_find(const KestModule *module, const char *name);
// How many functions a generic name stands for, filling `found` with the
// first `room` of them. A generic is compiled once per set of types and each
// copy is named `sort#i32`, which is one place in the program and is not a
// name anybody wrote; this is that place, so that looking a name up and
// saying why the lookup could not answer agree about what a copy is.
// What a module still holds when a build is done, by what asked for it. See
// D784.
void kest_module_holds(const KestModule *module, uint32_t *code,
                       uint32_t *origins, uint32_t *constants,
                       uint32_t *layouts, uint32_t *chunks);

uint32_t kest_module_copied(const KestModule *module, uint32_t *bodies,
                            uint32_t *bytes);

uint32_t kest_module_copies(const KestModule *module, const char *name,
                            int32_t *found, uint32_t room);
// The name a program writes, out of the one a function was compiled under.
// What a function takes is part of what makes it that function rather than
// another one, so a copy is named `sort#i32`; nobody wrote that, and anything
// said to a person stops at the hash.


// Which function a host means by a name: the name as written, and then the
// same name under the module of the file that was named. -1 for one the
// program does not have.
int32_t kest_module_entry(const KestModule *module, const char *name);

// Where the instruction holding this byte was written, which is a walk over the
// body: one origin is kept per instruction rather than one per byte. Nought for
// a chunk with nothing in it. See D751.
uint32_t kest_chunk_origin(const KestChunk *chunk, uint32_t offset);

// How many bytes the instruction at a byte is, which is how a walk of the code
// finds where the next one starts. Nought for a byte that is no instruction.
uint32_t kest_op_wide(uint8_t op);

// What an instruction is called. The list of them is `value.c`'s and this is
// the one way anything else asks it, which is what keeps a machine that says
// what it ran from holding a second copy of the names. See D870.
const char *kest_op_name(uint8_t op);

// What the instruction at `at` takes off the operand stack and puts back on
// it, in slots, read the way the machine's handler for it moves the top of
// the stack. NULL when that can be said, and why not when it cannot: a call
// handing a function something other than what it takes, or a value of a
// layout with no type. The verifier walks every path with it, and the build
// that checks itself holds it to what the machine moved. See D1239.
const char *kest_op_stack(const KestModule *module, const KestChunk *chunk,
                          uint32_t at, uint32_t *takes, uint32_t *gives);

// What each number an instruction carries is, which is what the verifier holds
// it to before anything runs: one of the body's slots or constants, one of the
// module's functions, doors or layouts, a jump that lands on an instruction,
// or a number the instruction uses as it is -- a width, an offset, a count --
// whose limits are the stack's and the layout's. A run is a count read with
// the operand before it: that many slots or constants from there. Written
// beside each name, one list, because the machine reading a number and the
// verifier knowing what it is are one fact. See D1237.
typedef enum {
    KEST_OPERAND_NUMBER,
    KEST_OPERAND_SLOT,
    KEST_OPERAND_SLOT_RUN,
    KEST_OPERAND_CONSTANT,
    KEST_OPERAND_CONSTANT_RUN,
    KEST_OPERAND_FUNCTION,
    KEST_OPERAND_EXTERN,
    KEST_OPERAND_LAYOUT,
    KEST_OPERAND_FORWARD,
    KEST_OPERAND_BACKWARD,
} KestOperand;

// What the `k`th number an instruction carries is: nought counts from the
// first. A number past the last it carries is a number.
KestOperand kest_op_operand(uint8_t op, uint32_t k);

// Whether an instruction reaches the heap. The list is the machine's, read off
// the cases that call the allocator, and it is what makes a `no.alloc`
// promise a property of what runs rather than of what was read.
bool kest_op_allocates(uint8_t op);

// The number an instruction carries at a byte of a chunk, read the way the
// machine reads it.
uint16_t kest_chunk_u16(const KestChunk *chunk, uint32_t offset);

// Takes the last instruction back, which the compiler does when a comparison
// turns out to be what a jump reads. `to` is where that instruction started.
// It takes back the byte, where the next instruction is expected and where the
// instruction came from — three things written together and until D804 taken
// back one at a time, which left every origin after a fused jump naming the
// instruction after the one it is for. See D804.
void kest_chunk_take_back(KestChunk *chunk, uint32_t to);

// Writes down that this body called a slot something. Quietly does nothing
// when there is no room: a name a debugger cannot show is not worth refusing a
// compile over. See D991.
bool kest_chunk_names(KestModule *module, KestChunk *chunk, const char *name,
                      uint16_t slot, uint16_t slots, uint8_t kind,
                      bool at_address);

// What this body called the slot, or NULL. The last name written for a slot is
// the one answered, because a body that reuses a slot after a scope ends gave
// it a second name and the second is the one in scope where the code is now.
// `at_address` may be NULL for a reader that does not care.
const char *kest_chunk_named(const KestChunk *chunk, uint16_t slot,
                             uint16_t *slots, uint8_t *kind,
                             bool *at_address);

bool kest_chunk_emit(KestModule *module, KestChunk *chunk, uint8_t byte,
                     uint32_t origin);
bool kest_chunk_emit_u16(KestModule *module, KestChunk *chunk, uint16_t value,
                         uint32_t origin);
// Records a name the host must provide and returns where it sits in the list.
// Declaring the same one twice records it once.
int32_t kest_module_extern(KestModule *module, const char *name, KestSpan span,
                           const KestSource *source, bool promises,
                           bool deterministic);
// What the program expects the extern at `at` to take and give. The layouts
// are the caller's to work out, because working one out is the compiler's job
// and this file is where they are kept.
void kest_module_extern_shape(KestModule *module, uint32_t at, uint16_t *takes,
                              uint16_t count, uint16_t gives,
                              bool gives_value);

// The layout of a type, built once and shared. Returns where it sits in the
// module's table.
int32_t kest_module_layout(KestModule *module, const KestType *type);
// How many types of a written name the program lays out, filling `layout` when
// there is exactly one. Nought is a name the program does not hold in an
// array, which is the same as one it cannot lend, and more than one is a name
// that needs the module written in front of it. A lend and a host asking what
// it will be lending ask this, so the two cannot come apart about either.
uint32_t kest_module_layout_of(const KestModule *module, const char *name,
                               const KestLayout **found, uint32_t room);
// The nearest name that can be lent to, or NULL when nothing is near enough.
// Only what the program holds in an array is offered, because a name it has
// and cannot lend is a suggestion that fails the same way.
const char *kest_module_nearest(const KestModule *module, const char *name);
// The name a host would have to write to get one type back, which is the one
// given when that means a single type and the whole of the other when it does
// not. NULL when nothing of that name can be asked for at all.
const char *kest_module_askable(const KestModule *module, const char *name);

// A value written the way a program writes one, read back out of a word: what
// a shell hands the command line and what a host hands over rather than laying
// out slots itself. Anything that is not a number, a truth or a piece of text
// cannot be written as a word, and saying so beats guessing.
//
// False when the word is not one of that type, and `why` is what to say about
// it — a reason without the word or the type in front of it, so that a caller
// says where it came from in its own words.
bool kest_value_read(KestArena *arena, const char *text, const KestType *type,
                     KestValue *into, const char **why);

// What a piece of a layout is called, which is the name a program writes for
// that type. Anything that is not one of them is said as such rather than read
// past the end of the list.
const char *kest_scalar_name(uint8_t kind);

// And what a reason there is no least is called, which the JSON, the words a
// listing prints and the machine refusing for want of stack are the same list
// of: a reason added to `KestReach` is caught here rather than printed as
// whatever the last one fell through to.
const char *kest_reach_name(KestReach reach);

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow);

// A whole number in decimal, signed or not, into a buffer of at least
// `KEST_WHOLE_ROOM` bytes, ended with a nought; answers how many characters it
// wrote before it. Written out rather than asked of `snprintf`, which reads a
// format every time and was a fifth of what making text out of numbers cost.
// See D1173.
#define KEST_WHOLE_ROOM 24
int kest_write_whole(char *buffer, uint64_t bits, bool is_signed);

// A run of values the chunk holds, kept together and in order because what
// reads them back is one copy, and shared with a run already there that is the
// same. One value is a run of one: there used to be a door for that as well,
// and two doors for one question are two answers the day either moves.
// Gives where the run starts.
uint32_t kest_chunk_constant_run(KestModule *module, KestChunk *chunk,
                                 const KestValue *values,
                                 const uint8_t *classes, uint32_t count);

// Prints every function as instructions, for seeing what the compiler emitted.
// The instructions, for a person. `entries` is a NULL-terminated list of the
// names whose own cost is worth printing beside the program's, which is the
// caller's to say: a library does not know which functions a host will call.
void kest_module_disassemble(const KestModule *module,
                             const char *const *entries, FILE *out);
// The same thing for whatever is reading it rather than for a person: what is
// laid out, what the host must provide, and every function with its
// instructions as an offset, a name and the numbers after it. What the text
// form decorates — the value behind a constant, where a jump lands — is left
// as the numbers, because a reader that wanted prose would have asked for it.
// What one question about room answers, written as the fields of an object
// without the braces around them: the whole program when `only` is -1 and one
// function when it is not. Every part of this project that says this in JSON
// says it through here, so the shape a tool reads for a program and the shape
// it reads for a function are the same shape, nulls and all.
void kest_module_needs_json(const KestModule *module, int32_t only, FILE *out);

// The same, as one object. `entries` is the names to answer about beside the
// program, and every one of them the program has is in the answer whether or
// not it differs — a tool looks one up rather than reading a list.
// A number that moves when what the machine will run moves, and does not move
// when only where it was written does. See D659.
uint64_t kest_module_mark(const KestModule *module);

void kest_module_disassemble_json(const KestModule *module,
                                  const char *const *entries, FILE *out);

#endif
