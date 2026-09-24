#ifndef KEST_PARSER_H
#define KEST_PARSER_H

#include "ast.h"

// Parses a whole file. Errors are reported into diags and recovered from, so
// one run reports everything wrong with the file rather than the first thing.
// Returns false only when the host is out of memory; a program with syntax
// errors still yields a unit holding what could be parsed.
bool kest_parse(KestArena *arena, const KestSource *source, KestDiags *diags,
                KestUnit *unit);

// How tightly a binary operator holds its operands, higher holding tighter,
// and nought for what is not one. The formatter asks this rather than keeping
// a list of its own, because a formatter that thinks `|` holds tighter than
// `+` drops the brackets that keep `(a | b) + c` what it says. See D1236.
int kest_binary_precedence(KestTokenKind kind);

#endif
