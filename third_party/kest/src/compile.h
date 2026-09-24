#ifndef KEST_COMPILE_H
#define KEST_COMPILE_H

#include "ir.h"

// Writes a body for every function that has one: what the program means,
// resolved and typed, for a backend to read. Reports what it cannot write
// rather than writing something that does not mean the same thing.
bool kest_compile(KestProgram *program, const KestUnits *units,
                  KestModule *module, KestIrProgram *ir);

#endif
