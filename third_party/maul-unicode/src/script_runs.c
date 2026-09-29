// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Script runs. A run keeps the set of scripts every code point in it can
// belong to, as a 256-bit mask over Script indexes: a code point with a
// real Script narrows it to its Script or its Script_Extensions, and a
// run ends where that would leave no script. Common and Inherited code
// points leave the set alone. Opening brackets remember the run's script
// on a stack of 32 (the oldest entry drops when it is full, as in ICU);
// a closing bracket that matches one counts as a code point of that
// script, so "(" and ")" around foreign text land in the same script.

#include "cursor.h"
#include "tables.h"

#include "maul-unicode/script.h"

#include <string.h>

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

static_assert(sizeof(ScriptState) <= sizeof(muniScriptIterator),
              "the public iterator must hold the state");

static bool IsNeutral(uint8_t script)
{
    uint32_t tag = muniScriptTags[script];
    return tag == MUNI_SCRIPT_COMMON || tag == MUNI_SCRIPT_INHERITED;
}

static void Add(Scripts* scripts, uint8_t script)
{
    scripts->bits[script >> 6] |= (uint64_t)1 << (script & 63);
}

static bool Has(const Scripts* scripts, uint8_t script)
{
    return (scripts->bits[script >> 6] >> (script & 63) & 1) != 0;
}

// The scripts a code point belongs to; false for Common and Inherited
// code points without extensions, which join any run.
static bool ScriptsOf(uint32_t codePoint, uint8_t* scriptOut, Scripts* scriptsOut)
{
    uint8_t script = muniLookupScript(codePoint);
    uint8_t set = muniLookupScriptExtensions(codePoint);
    memset(scriptsOut, 0, sizeof(*scriptsOut));
    *scriptOut = script;
    if (set == 0)
    {
        Add(scriptsOut, script);
        return !IsNeutral(script);
    }
    for (size_t k = muniScriptSetStarts[set - 1]; k < muniScriptSetStarts[set]; k++)
    {
        Add(scriptsOut, muniScriptSetMembers[k]);
    }
    return true;
}

// The run's script: its first code point's Script when the set still
// holds it, else the lowest Script index in the set.
static uint8_t Best(const ScriptState* state)
{
    if (state->firstScript != NO_SCRIPT && Has(&state->candidates, state->firstScript))
    {
        return state->firstScript;
    }
    for (uint8_t word = 0; word < 4; word++)
    {
        uint64_t bits = state->candidates.bits[word];
        if (bits != 0)
        {
            uint8_t bit = 0;
            while ((bits & 1) == 0)
            {
                bits >>= 1;
                bit += 1;
            }
            return (uint8_t)(word * 64 + bit);
        }
    }
    return NO_SCRIPT;
}

// Folds the canonical equivalents U+2329 and U+232A onto U+3008 and U+3009.
static uint32_t Fold(uint32_t codePoint)
{
    return codePoint == 0x2329 ? 0x3008 : codePoint == 0x232A ? 0x3009 : codePoint;
}

static void Push(ScriptState* state, uint32_t closer, uint8_t script)
{
    if (state->bracketCount == BRACKET_DEPTH)
    {
        memmove(state->brackets, state->brackets + 1, (BRACKET_DEPTH - 1) * sizeof(uint32_t));
        state->bracketCount -= 1;
    }
    state->brackets[state->bracketCount++] = closer << 8 | script;
}

// The script of the opening bracket closer closes, popping the stack down
// to it, or NO_SCRIPT when none matches.
static uint8_t Match(ScriptState* state, uint32_t closer)
{
    for (uint8_t k = state->bracketCount; k > 0; k--)
    {
        if (state->brackets[k - 1] >> 8 == closer)
        {
            state->bracketCount = (uint8_t)(k - 1);
            return (uint8_t)(state->brackets[k - 1] & 0xFF);
        }
    }
    return NO_SCRIPT;
}

// Gives the brackets opened before the run had a script the run's script.
static void SettlePending(ScriptState* state)
{
    uint8_t script = Best(state);
    for (uint8_t k = state->bracketCount; k > 0 && (state->brackets[k - 1] & 0xFF) == NO_SCRIPT;
         k--)
    {
        state->brackets[k - 1] = (state->brackets[k - 1] & ~0xFFu) | script;
    }
}

// Brackets among neutral code points: an opening one is pushed, and a
// closing one that matches takes its script. Returns whether the code
// point now has a script.
static bool Bracket(ScriptState* state, uint32_t codePoint, uint8_t* scriptOut, Scripts* scriptsOut)
{
    uint8_t type = muniLookupBidiBracket(codePoint);
    if (type == muni_bracketOpen)
    {
        Push(state, Fold(muniGetMirroringGlyph(codePoint)),
             state->determined ? Best(state) : NO_SCRIPT);
    }
    else if (type == muni_bracketClose)
    {
        uint8_t script = Match(state, Fold(codePoint));
        if (script != NO_SCRIPT)
        {
            *scriptOut = script;
            memset(scriptsOut, 0, sizeof(*scriptsOut));
            Add(scriptsOut, script);
            return true;
        }
    }
    return false;
}

static void StartRun(ScriptState* state, uint8_t script, const Scripts* scripts)
{
    state->candidates = *scripts;
    state->firstScript = IsNeutral(script) ? NO_SCRIPT : script;
    state->determined = true;
    SettlePending(state);
}

// Reads code points until a run ends; true with the run when one does.
static bool Step(ScriptState* state, uint32_t codePoint, muniScriptRun* runOut)
{
    uint8_t script;
    Scripts scripts;
    bool real = ScriptsOf(codePoint, &script, &scripts);
    if (!real)
    {
        real = Bracket(state, codePoint, &script, &scripts);
    }
    if (!real)
    {
        return false;
    }
    if (!state->determined)
    {
        StartRun(state, script, &scripts);
        return false;
    }
    Scripts shared;
    bool any = false;
    for (int word = 0; word < 4; word++)
    {
        shared.bits[word] = state->candidates.bits[word] & scripts.bits[word];
        any = any || shared.bits[word] != 0;
    }
    if (any)
    {
        state->candidates = shared;
        if (state->firstScript == NO_SCRIPT && !IsNeutral(script) && Has(&shared, script))
        {
            state->firstScript = script;
        }
        return false;
    }
    *runOut = (muniScriptRun){state->cursor.offset, muniScriptTags[Best(state)]};
    StartRun(state, script, &scripts);
    return true;
}

static muniResult Next(ScriptState* state, muniScriptRun* runOut)
{
    for (;;)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniCursorPeek(&state->cursor, &codePoint, &size);
        if (status == muni_needMoreText)
        {
            return status;
        }
        if (status == muni_done)
        {
            if (!state->started || state->finished)
            {
                return muni_done;
            }
            state->finished = true;
            muniScript script =
                state->determined ? muniScriptTags[Best(state)] : MUNI_SCRIPT_COMMON;
            *runOut = (muniScriptRun){state->cursor.offset, script};
            return muni_success;
        }
        bool ended = Step(state, codePoint, runOut);
        state->started = true;
        muniCursorAdvance(&state->cursor, size);
        if (ended)
        {
            return muni_success;
        }
    }
}

static ScriptState Load(const muniScriptIterator* iterator)
{
    ScriptState state;
    memcpy(&state, iterator, sizeof(state));
    return state;
}

static void Store(muniScriptIterator* iterator, const ScriptState* state)
{
    memcpy(iterator, state, sizeof(*state));
}

static void Start(ScriptState* state, const char* text, size_t length, bool moreFollows)
{
    memset(state, 0, sizeof(*state));
    state->firstScript = NO_SCRIPT;
    muniCursorInit(&state->cursor, text, length, moreFollows);
}

muniResult muniInitScriptIterator(muniScriptIterator* iterator, const char* text, size_t length,
                                  bool moreFollows)
{
    if (iterator == nullptr || (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    ScriptState state;
    Start(&state, text, length, moreFollows);
    memset(iterator, 0, sizeof(*iterator));
    Store(iterator, &state);
    return muni_success;
}

muniResult muniFeedScriptIterator(muniScriptIterator* iterator, const char* text, size_t length,
                                  bool moreFollows)
{
    if (iterator == nullptr || (text == nullptr && length != 0))
    {
        return muni_errorInvalid;
    }
    ScriptState state = Load(iterator);
    if (!state.waiting || !muniCursorFeed(&state.cursor, text, length, moreFollows))
    {
        return muni_errorInvalid;
    }
    state.waiting = false;
    Store(iterator, &state);
    return muni_success;
}

muniResult muniNextScriptRun(muniScriptIterator* iterator, muniScriptRun* runOut)
{
    if (iterator == nullptr || runOut == nullptr)
    {
        return muni_errorInvalid;
    }
    ScriptState state = Load(iterator);
    muniResult status = Next(&state, runOut);
    state.waiting = status == muni_needMoreText;
    Store(iterator, &state);
    return status;
}

muniResult muniFindScriptRuns(const char* text, size_t length, muniScriptRun* runs, size_t capacity,
                              size_t* countOut)
{
    if ((text == nullptr && length != 0) || (runs == nullptr && capacity != 0) ||
        countOut == nullptr)
    {
        return muni_errorInvalid;
    }
    ScriptState state;
    Start(&state, text, length, false);
    size_t count = 0;
    muniScriptRun run;
    while (Next(&state, &run) == muni_success)
    {
        if (count < capacity)
        {
            runs[count] = run;
        }
        count += 1;
    }
    *countOut = count;
    return count > capacity ? muni_errorCapacity : muni_success;
}
