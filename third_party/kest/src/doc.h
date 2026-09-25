#ifndef KEST_DOC_H
#define KEST_DOC_H

#include <stdbool.h>
#include <stdio.h>

#include "loader.h"

// What a file offers somebody who is going to call it rather than read it,
// written to `out`: what it calls itself and what it says about itself, then
// every declaration as it is written -- a function up to where its body
// starts, anything else whole -- with the comment written above it. Markdown,
// or the same as JSON. The words are the file's own and nothing is made up: a
// declaration nobody wrote a comment above is said with nothing under it.
// False when there was no room for the file's comments. See D1258.
bool kest_doc(const KestUnitInfo *file, KestArena *arena, bool json,
              FILE *out);

#endif
