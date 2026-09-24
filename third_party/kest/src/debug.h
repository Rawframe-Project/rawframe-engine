#ifndef KEST_DEBUG_H
#define KEST_DEBUG_H

#include <stdio.h>

#include "build.h"

// A source-level debugger. Breakpoints are written into the code and taken out
// again, so a machine nobody is debugging pays nothing at all: there is no test
// in the dispatch loop, because one measured a third of the machine. See D991.
//
// Two things talk to a person through it: `kest debug`, which is words on two
// streams, and `kest dap`, which is an editor's debug protocol. What stops,
// steps and reads is here once, and each of those is how it is asked. See
// D1182.

#define KEST_MOST_BREAKPOINTS 64

// One byte written over, and what was there. A breakpoint costs the machine
// nothing because it *is* the machine: the byte the debugger wrote is the one
// instruction nothing compiles to, and putting the old one back is how the
// machine carries on. See D991.
typedef struct {
    int32_t entry;
    uint32_t at;
    uint8_t was;
    // Whether a person asked for it or this asked for it on their behalf. A
    // step is a breakpoint at every line and they all go when the step ends.
    bool asked_for;
} KestWritten;

// The build and the machine are the caller's, already started with whatever
// the caller binds. `out` is where what happens is said to a person, or NULL
// where nothing is said and the caller reads the answers instead.
typedef struct {
    KestBuild *build;
    KestRuntime *runtime;
    FILE *out;
    KestWritten written[KEST_MOST_BREAKPOINTS];
    uint32_t count;
    bool running;
    bool over;
    // What the body the run began with answered, once it has. A machine that
    // stopped and was carried on answers into this rather than into the call
    // that started it, which has long since returned.
    KestValue answer[8];
} KestDebugger;

// What a machine did when it was let go: stopped at a breakpoint or a step,
// ran to the end, or was refused on the way.
typedef enum {
    KEST_DEBUG_STOPPED,
    KEST_DEBUG_FINISHED,
    KEST_DEBUG_FAILED,
} KestDebugState;

// How far a step goes: the next line wherever it is, the next line of this
// body with a call stepped over, or the line after the call this body was
// called from.
typedef enum {
    KEST_STEP_INTO,
    KEST_STEP_OVER,
    KEST_STEP_OUT,
} KestStep;

void kest_debugger_open(KestDebugger *held, KestBuild *build,
                        KestRuntime *runtime, FILE *out);

// A breakpoint at every body's first instruction of `line`, in the file the
// program was named by when `file` is NULL and otherwise in the one whose path
// holds `file`. Answers how many went in.
uint32_t kest_debugger_break(KestDebugger *held, const char *file,
                             uint32_t line);

// Every breakpoint a person asked for in that file taken out, which is how an
// editor that sends a file's whole list at once is answered.
void kest_debugger_unbreak(KestDebugger *held, const char *file);

KestDebugState kest_debugger_run(KestDebugger *held, int32_t entry);
KestDebugState kest_debugger_continue(KestDebugger *held);
KestDebugState kest_debugger_step(KestDebugger *held, KestStep how);

// The frames standing, nought the outermost, and where each is: the body, the
// file and the line and column the instruction it is at came from.
uint32_t kest_debugger_frames(KestDebugger *held);
bool kest_debugger_frame(KestDebugger *held, uint32_t deep, const char **body,
                         const char **path, uint32_t *line, uint32_t *column);

// A frame's slots, and what the body called one and holds in it, written the
// way the language writes a value. NULL for a slot the body gave no name.
uint16_t kest_debugger_wide(KestDebugger *held, uint32_t deep);
const char *kest_debugger_local(KestDebugger *held, uint32_t deep,
                                uint16_t slot, char *value, size_t room,
                                uint16_t *slots);

// Every byte written over put back, so the program is the program again.
void kest_debugger_close(KestDebugger *held);

// `kest debug`: the debugger asked in words, one command a line. Answers what
// the process should exit with.
int kest_debug_serve(KestBuild *build, KestRuntime *runtime, const char *entry,
                     FILE *in, FILE *out);

#endif
