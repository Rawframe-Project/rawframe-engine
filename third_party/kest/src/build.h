#ifndef KEST_BUILD_H
#define KEST_BUILD_H

#include "check.h"
#include "compile.h"
#include "contract.h"
#include "loader.h"
#include "lower.h"
#include "vm.h"

// What each stage of compiling took, in whatever unit the clock a build was
// given counts in. Nought everywhere for a build that was given no clock,
// which is every build nobody asked about.
//
// What compiling cost in memory is a different question with different doors
// -- `kest_build_cost` and `kest_build_held` -- and the two never move
// together. Everything here is time.
typedef struct {
    // Every file the program names, found, read and parsed. Timed by whoever
    // opened the build, because the reading is what that call is and there is
    // nowhere to keep a clock before it.
    uint64_t reading;
    // Declarations, types, and what every name means.
    uint64_t naming;
    // Every function body checked against them.
    uint64_t bodies;
    // The cost promises proved.
    uint64_t promises;
    // The checked tree walked and written down as the resolved form, which is
    // what compiling is once the three stages below it are taken off.
    uint64_t writing;
    // Of all four of those, the share that went on copies of generic
    // functions: retyped, bound, and written out one per set of types they
    // were called with. It is a share rather than a stage of its own, because
    // a copy is a body and goes through what a body goes through. See D778.
    uint64_t copies;
    // The resolved form held to what a backend may read.
    uint64_t verifying;
    // The pass over it.
    uint64_t optimizing;
    // The resolved form written as bytecode.
    uint64_t lowering;
    // What was emitted, proved against what was declared.
    uint64_t finishing;
} KestSpent;

// A compiled program and everything it was compiled from. One arena holds all
// of it, so freeing the build frees the lot.
struct KestBuild {
    KestArena *arena;
    KestDiags diags;
    KestUnits units;
    KestProgram *program;
    KestModule module;
    // How much of what the build has said has been written out, so a host
    // asking twice is not told the same thing twice. The command line renders
    // the whole set itself and does not touch this.
    uint32_t reported;
    // And whether the one thing a run with no memory can say has been said.
    // It is not in the list — making a list entry is what there was no room
    // for — so what keeps it from being said twice is a bit of its own.
    bool starve_said;
    bool compiled;
    // Whether the checker keeps where every name was written and what it
    // named. Set between opening a build and checking it, by whoever is going
    // to ask -- which is the language server and nothing else. See D977.
    bool index_names;
    // The one walk of the whole program, worked out when somebody first asks
    // and handed to everybody who asks after, machines included. See D607.
    KestWalk walked;
    // Whether the bodies are written as C as well as as bytecode, and what
    // came out. Said between opening a build and compiling it: the two
    // backends read one body each time one is made, and a body is let go
    // before the next is built. See D1093.
    bool wants_c;
    const char *c_wrote;
    // Whether the C written carries every file the program was read from, so
    // that what is built from it needs none of them. See D1172.
    bool carrying;
    // The clock, or NULL for a build nobody is weighing, and what each stage
    // took by it.
    uint64_t (*now)(void *);
    void *now_context;
    KestSpent spent;
};

// The stages, so the command line can stop between them and a host does not
// have to know there are any. `library` may be NULL for `lib/` beside the
// program.
//
// `room` is the most this build may ask the machine for, in bytes, and nought
// is as much as there is. A build with a ceiling is refused at the allocation
// that would cross it and says what it had taken and what it wanted, the way a
// program stopped by a heap ceiling does; a build without one asks until the
// machine has nothing left, which is what took a machine down eight times in a
// day. See D843.
KestBuild *kest_build_open(const char *library, char **paths, int count,
                           size_t room);

// The same with a ceiling on the work compiling may do, which nought leaves
// off. See `kest_build_within` and D1248.
KestBuild *kest_build_open_within(const char *library, char **paths, int count,
                                  size_t room, uint64_t work);

// The name something lives under in the file that was named. A host does not
// need this — `kest_entry` leaves the module off for it — but the command line
// asks the program's own symbol table, which is registered qualified.
//
// Asked after the program is emitted, because it reads the module's own alias,
// which is the field `kest_entry` reads when it looks for the qualified form
// of a name a host wrote plainly. One field, so the two directions of one rule
// cannot come apart.
const char *kest_build_name(KestBuild *build, const char *name);
// Asks the next check to keep the index an editor reads. Nothing else wants
// it and it is a third again of what a finished build holds, so it is off.
void kest_build_index_names(KestBuild *build, bool keep);
// Asks the next emit to call every body where it is called rather than carry
// small ones to their calls, which is what a profile of calls wants. See
// D1156.
void kest_build_calls_as_written(KestBuild *build);

// Asks the C this build writes to carry the program's files rather than read
// them, which is what a release binary is. Asked before the build emits.
void kest_build_carries_sources(KestBuild *build);
// The clock this build times its own stages with, and how long opening it
// took, which the caller timed because there was nowhere to keep a clock while
// it happened. The clock is the caller's for the reason `kest_clock` is the
// host's: this library is ISO C, which has no monotonic clock, and a duration
// belongs to the machine that ran rather than to the program.
//
// Given between opening a build and checking it. A build never given one
// weighs nothing and is not slowed by asking: every reading is through one
// door that answers nought for no clock. See D1026.
void kest_build_clock(KestBuild *build, uint64_t (*now)(void *), void *context,
                      uint64_t reading);
// What each stage took, by that clock. Nought everywhere when there was none.
const KestSpent *kest_build_spent(const KestBuild *build);
// Asks the next compile to write the bodies as C beside the bytecode, which is
// what `kest emit --c` is. Nothing else changes: the same walk hands each body
// to both backends, so what the C says and what the machine runs came from one
// reading of the program.
void kest_build_writes_c(KestBuild *build, bool on);
// That C, or NULL for a build that was not asked or did not get that far. It
// is one translation unit, and every body this backend had no C for is named
// in it with the reason.
const char *kest_build_c(const KestBuild *build);
bool kest_build_check(KestBuild *build);
bool kest_build_emit(KestBuild *build);

#endif
