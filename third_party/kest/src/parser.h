#ifndef KEST_PARSER_H
#define KEST_PARSER_H

#include "ast.h"

// Parses a whole file. Errors are reported into diags and recovered from, so
// one run reports everything wrong with the file rather than the first thing.
// Returns false only when the host is out of memory; a program with syntax
// errors still yields a unit holding what could be parsed.
bool kest_parse(KestArena *arena, const KestSource *source, KestDiags *diags,
                KestUnit *unit);

// The block written at `span`, parsed again into a tree of its own: the same
// source read a second time. A walk over a struct's fields is checked once a
// field, each time with its names standing for another type, and a tree is
// where the checker writes what each thing was found to be -- so each field
// is given a tree of its own, and the source is the copy there already is.
// False when there was no room, or when what is there is not a block. See
// D1264.
bool kest_parse_block_again(KestArena *arena, const KestSource *source,
                            KestDiags *diags, KestSpan span, KestBlock *into);

// How tightly a binary operator holds its operands, higher holding tighter,
// and nought for what is not one. The formatter asks this rather than keeping
// a list of its own, because a formatter that thinks `|` holds tighter than
// `+` drops the brackets that keep `(a | b) + c` what it says. See D1236.
int kest_binary_precedence(KestTokenKind kind);

#endif
