#ifndef KEST_CONTRACT_H
#define KEST_CONTRACT_H

#include "types.h"

// Proves every `no.alloc` promise, or refuses the program and names the body
// that breaks it. A callee defined in this file is judged by what it does; a
// foreign one is judged by what it declares, because nothing can be inferred
// about a body that is not here.
bool kest_check_contracts(KestProgram *program, const KestUnits *units);

#endif
