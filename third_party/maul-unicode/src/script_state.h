// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The state of a script iterator, which script_runs.c keeps in the public
// iterator's opaque storage. Here so that a fuzz target can change the
// fields that hold no pointer and see every call refuse what is out of
// range.

#ifndef MAUL_UNICODE_SRC_SCRIPT_STATE_H
#define MAUL_UNICODE_SRC_SCRIPT_STATE_H

#include "cursor.h"

#define BRACKET_DEPTH 32
#define NO_SCRIPT     0xFFu

typedef struct Scripts
{
    uint64_t bits[4];
} Scripts;

typedef struct ScriptState
{
    muniCursor cursor;
    Scripts candidates;               // the scripts the run can still have
    uint32_t brackets[BRACKET_DEPTH]; // closing bracket << 8 | Script index or NO_SCRIPT
    uint8_t bracketCount;
    uint8_t firstScript; // the Script of the run's first code point that has one
    bool determined;     // a code point with a real script is in the run
    bool started;        // a code point has been read
    bool waiting;        // the last call returned muni_needMoreText
    bool finished;       // the last run has been reported
} ScriptState;

#endif // MAUL_UNICODE_SRC_SCRIPT_STATE_H
