#ifndef KEST_LOWER_H
#define KEST_LOWER_H

#include "ir.h"

// The stack machine as a reader of bodies. It is handed one at a time and
// decides nothing about what a program means: what it decides is which
// instruction, how wide a jump is, and which pairs of instructions are worth
// writing as one.
typedef struct KestLower KestLower;

// Made in the arena the bodies are in, which is where its own working memory
// goes. Answers NULL when there is none.
KestLower *kest_lower_new(KestProgram *program, KestModule *module,
                          KestArena *arena);
bool kest_lower_body(void *lower, const KestIrBody *body);

#endif
