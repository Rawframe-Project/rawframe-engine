#ifndef KEST_FMT_H
#define KEST_FMT_H

#include "parser.h"

// Writes the file back in the one form the language has. Printing from the
// tree rather than from the tokens is what lets `ref<Npc>` and `a < b` be
// told apart, which no amount of looking at the characters can do.
//
// Comments are kept, at the indent of what they precede. What is inside a
// string is left exactly as written, except for the expressions in its holes,
// which are code and are written back in the one form like any other code:
// `"{ a  +  1 }"` comes back as `"{a + 1}"`. That is why a comment may not be
// written inside a hole — there is nothing to write it back into — and the
// lexer refuses one.
// Returns the file as it should be written, in arena memory. Returning it
// rather than writing it is what lets a caller compare it with what is there
// and leave a file alone that is already right.
const char *kest_format(const KestUnit *unit, const KestSource *source,
                        KestArena *arena, size_t *length);

#endif
