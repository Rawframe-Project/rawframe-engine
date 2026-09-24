#ifndef KEST_EMITC_H
#define KEST_EMITC_H

#include "ir.h"

// The resolved form written as C, which the host's compiler makes a program
// out of. It is the second backend on the one seam: `lower` walks a body and
// writes instructions for the machine to dispatch, and this walks the same
// body and writes what the instructions would have done, so that the host's
// compiler can keep an operand in a register and a loop in registers.
//
// What it is for is the engine a game ships rather than the one it is written
// against. The machine compiles a program in milliseconds and runs it at the
// cost of a dispatch an operation; this costs whatever the host's compiler
// takes and runs at the cost of nothing between the operations. Neither is the
// other's replacement: the first is what an iteration wants and the second is
// what a frame budget wants. See D1092.
//
// It writes what it can and says what it cannot. A body holding something this
// backend has no C for is not a program refused: it is a body the machine
// runs, and the two halves are one program. Which those were is written into
// the file it produces, because a reader asking why a body is slow is a reader
// asking exactly that.
typedef struct KestEmitC KestEmitC;

// Somewhere to write bodies into. The arena is the caller's and outlives the
// bodies, which are freed one at a time: everything kept here is copied into
// it rather than pointed at. The module is what is being compiled, read for
// how a value is laid out and for what a body promised.
KestEmitC *kest_emitc_new(KestArena *arena, const KestModule *module);

// One body, in the shape a backend is handed one. It answers false only when
// there was no memory: a body it cannot write is not a failure of the walk.
bool kest_emitc_body(void *writing, const KestIrBody *body);

// The whole translation unit, once every body has been through. `entry` is the
// symbol the program's `main` is compiled under and `from` is the file it was
// all written from, which the host this produces reads again: the C is half of
// a program and the machine holds the other half, so the program itself is
// what says which half is which. NULL when there was no memory.
// `carried` is every file the program was read from when the program is to
// carry them, with `library` the root the library's are under: then the host
// this writes builds from those rather than reading a file, and every word on
// its command line is the program's. NULL for a host that reads `from`. See
// D1172.
const char *kest_emitc_done(KestEmitC *writing, const char *entry,
                            const char *from, const KestFile *carried,
                            uint32_t carried_count, const char *library);

#endif
