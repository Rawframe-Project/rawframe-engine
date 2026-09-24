#ifndef KEST_CHECK_H
#define KEST_CHECK_H

#include "types.h"

// Checks every function body against the declarations `types` resolved:
// names, operators, calls, field access, assignment and returns. Errors are
// reported and recovered from, so one run reports the whole file.
bool kest_check_bodies(KestProgram *program, KestUnits *units);

// Puts one copy of a generic function's types back on the tree it shares with
// every other copy, which is what the compiler reads. Says nothing: whatever
// there was to say was said when the copy was made.
bool kest_retype_instance(KestProgram *program, KestInstance *instance);

#endif
