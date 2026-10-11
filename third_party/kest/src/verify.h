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

// What the walk that proves a body says one slot of it holds where the
// instruction at `at` starts, on every way the body can get there: what a
// debugger may read out of a stopped frame without reading what the frame
// before it left. Walked again for the asking, in an arena taken under
// `under` and given back. See D1291.
typedef enum {
    // Not a place the walk reached, a body that is not the one proved, or
    // no room to walk it.
    KEST_PROVED_UNKNOWN,
    // On some way here the body has not written it.
    KEST_PROVED_UNWRITTEN,
    // Written on every way, and not proved to be a piece of text.
    KEST_PROVED_WRITTEN,
    // With the slot after it, one piece of text on every way, or nought.
    KEST_PROVED_TEXT,
} KestProved;

KestProved kest_module_proved(const KestModule *module, uint32_t which,
                              uint32_t at, uint32_t slot, KestArena *under);

#endif
