#ifndef KEST_HOSTILE_H
#define KEST_HOSTILE_H

#include <stdint.h>

#include "mem.h"
#include "types.h"

// A function `hostile` that calls every door the file at `path` declares,
// `rounds` times each, with values a program nobody trusts could hand it:
// numbers at the ends of their widths, floats that are not numbers, text of no
// length and of a great deal, every case of an enum. Written as Kest, to be put
// after the file and run by the host that binds the doors, because what a door
// does with what it is handed is the host's to get right and this is how it
// finds out. `called` is how many calls were written and `passed` how many doors
// take something a program cannot make and were said about instead. NULL when
// there was no room. See D1254.
const char *kest_hostile_calls(KestProgram *program, KestArena *arena,
                               const char *path, uint64_t seed,
                               uint32_t rounds, uint32_t *called,
                               uint32_t *passed);

#endif
