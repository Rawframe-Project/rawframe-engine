#ifndef KEST_VERIFY_H
#define KEST_VERIFY_H

#include "value.h"

// Proves a module is what the machine may run without asking, before it runs:
// every body can be walked, every number an instruction carries names
// something the program has and every jump lands on an instruction (D1237),
// the operand stack is one depth on every path and inside the room each body
// was given (D1239), and every `no.alloc`, `no.host` and `deterministic`
// promise holds of the code that was emitted rather than of the tree it was
// checked on (D058). Reports what it finds and returns false when it found
// anything.
bool kest_module_prove(const KestModule *module, KestArena *arena,
                       KestDiags *diags);

#endif
