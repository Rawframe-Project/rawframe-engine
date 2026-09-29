// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rules P1 to P3 and X1 to X9 of UAX #9: paragraphs, the paragraph level,
// and the explicit levels from embeddings, overrides and isolates, with
// the directional status stack of rule X1 on the C stack (127 entries of
// three bytes).

#include "bidi_explicit.h"

enum
{
    OverrideNone = 0,
    OverrideLeft = 1,
    OverrideRight = 2,
};

// An entry of the directional status stack.
typedef struct Status
{
    uint8_t level;
    uint8_t override;
    bool isolate;
} Status;

typedef struct Explicit
{
    Status stack[MUNI_BIDI_MAX_DEPTH + 2];
    size_t depth; // entries on the stack
    size_t overflowIsolates;
    size_t overflowEmbeddings;
    size_t validIsolates;
} Explicit;

size_t muniBidiParagraphLength(const uint8_t* text, size_t length)
{
    size_t index = 0;
    while (index < length)
    {
        size_t size;
        uint32_t codePoint = muniBidiCodePoint(text, length, index, &size);
        index += size;
        if (muniLookupBidiClass(codePoint) == muni_bcB)
        {
            bool crlf = codePoint == '\r' && index < length && text[index] == '\n';
            return crlf ? index + 1 : index;
        }
    }
    return length;
}

uint8_t muniBidiFirstStrongLevel(const muniBidiParagraph* paragraph, size_t index, bool stopAtPdi,
                                 uint8_t fallback)
{
    size_t depth = 0;
    while (index < paragraph->length)
    {
        size_t size;
        uint8_t type = muniLookupBidiClass(
            muniBidiCodePoint(paragraph->text, paragraph->length, index, &size));
        index += size;
        if (muniBidiIsIsolateInitiator(type))
        {
            depth += 1;
        }
        else if (type == muni_bcPdi)
        {
            if (depth == 0 && stopAtPdi)
            {
                break;
            }
            depth -= depth > 0 ? 1 : 0;
        }
        else if (type == muni_bcB)
        {
            break;
        }
        else if (depth == 0 && type == muni_bcL)
        {
            return 0;
        }
        else if (depth == 0 && (type == muni_bcR || type == muni_bcAl))
        {
            return 1;
        }
    }
    return fallback;
}

static Status* Top(Explicit* state)
{
    return &state->stack[state->depth - 1];
}

// The next level above the top entry's: odd for right to left.
static uint8_t RaisedLevel(const Explicit* state, bool rightToLeft)
{
    uint8_t level = state->stack[state->depth - 1].level;
    return rightToLeft ? (uint8_t)((level + 1) | 1) : (uint8_t)((level + 2) & ~1);
}

// X2 to X5: an embedding or override initiator.
static void PushEmbedding(Explicit* state, bool rightToLeft, uint8_t override)
{
    uint8_t level = RaisedLevel(state, rightToLeft);
    if (level <= MUNI_BIDI_MAX_DEPTH && state->overflowIsolates == 0 &&
        state->overflowEmbeddings == 0)
    {
        state->stack[state->depth] = (Status){level, override, false};
        state->depth += 1;
    }
    else if (state->overflowIsolates == 0)
    {
        state->overflowEmbeddings += 1;
    }
}

// X5a to X5c after the initiator took the top entry's level.
static void PushIsolate(Explicit* state, bool rightToLeft)
{
    uint8_t level = RaisedLevel(state, rightToLeft);
    if (level <= MUNI_BIDI_MAX_DEPTH && state->overflowIsolates == 0 &&
        state->overflowEmbeddings == 0)
    {
        state->validIsolates += 1;
        state->stack[state->depth] = (Status){level, OverrideNone, true};
        state->depth += 1;
    }
    else
    {
        state->overflowIsolates += 1;
    }
}

// X6a before the PDI takes the top entry's level.
static void PopIsolate(Explicit* state)
{
    if (state->overflowIsolates > 0)
    {
        state->overflowIsolates -= 1;
    }
    else if (state->validIsolates > 0)
    {
        state->overflowEmbeddings = 0;
        while (!Top(state)->isolate)
        {
            state->depth -= 1;
        }
        state->depth -= 1;
        state->validIsolates -= 1;
    }
}

// X7.
static void PopEmbedding(Explicit* state)
{
    if (state->overflowIsolates > 0)
    {
        return;
    }
    if (state->overflowEmbeddings > 0)
    {
        state->overflowEmbeddings -= 1;
    }
    else if (!Top(state)->isolate && state->depth >= 2)
    {
        state->depth -= 1;
    }
}

// The class a code point gets under the top entry's override (X5a to X6a).
static uint8_t Overridden(const Explicit* state, uint8_t type)
{
    uint8_t override = state->stack[state->depth - 1].override;
    return override == OverrideLeft ? muni_bcL : override == OverrideRight ? muni_bcR : type;
}

// X2 to X8 for one code point at index; next is the index after it.
static void Apply(Explicit* state, const muniBidiParagraph* paragraph, size_t index, size_t next,
                  uint8_t type)
{
    uint8_t* slot = &paragraph->workspace[index];
    switch (type)
    {
    case muni_bcRle:
    case muni_bcLre:
    case muni_bcRlo:
    case muni_bcLro:
        PushEmbedding(state, type == muni_bcRle || type == muni_bcRlo,
                      type == muni_bcRlo   ? OverrideRight
                      : type == muni_bcLro ? OverrideLeft
                                           : OverrideNone);
        *slot = muni_bidiRemoved;
        return;
    case muni_bcPdf:
        PopEmbedding(state);
        *slot = muni_bidiRemoved;
        return;
    case muni_bcBn:
        *slot = muni_bidiRemoved;
        return;
    case muni_bcB:
        paragraph->levels[index] = paragraph->level; // X8
        *slot = muni_bcB;
        return;
    case muni_bcRli:
    case muni_bcLri:
    case muni_bcFsi:
    {
        paragraph->levels[index] = Top(state)->level;
        *slot = Overridden(state, type);
        bool rightToLeft =
            type == muni_bcRli ||
            (type == muni_bcFsi && muniBidiFirstStrongLevel(paragraph, next, true, 0) == 1);
        PushIsolate(state, rightToLeft);
        return;
    }
    case muni_bcPdi:
        PopIsolate(state);
        break;
    default:
        break;
    }
    // X6 and the end of X6a.
    paragraph->levels[index] = Top(state)->level;
    uint8_t current = Overridden(state, type);
    *slot = (uint8_t)(current | (current == muni_bcNsm ? muni_bidiWasNsm : 0));
}

// The classes after which a left-to-right paragraph can hold other levels
// than 0.
static bool NeedsImplicit(uint8_t type)
{
    return type == muni_bcR || type == muni_bcAl || type == muni_bcAn ||
           (type >= muni_bcLre && type <= muni_bcPdi);
}

bool muniBidiResolveExplicit(const muniBidiParagraph* paragraph)
{
    bool needed = paragraph->level != 0;
    Explicit state;
    state.stack[0] = (Status){paragraph->level, OverrideNone, false};
    state.depth = 1;
    state.overflowIsolates = 0;
    state.overflowEmbeddings = 0;
    state.validIsolates = 0;
    size_t index = 0;
    while (index < paragraph->length)
    {
        size_t size;
        uint32_t codePoint = muniBidiCodePoint(paragraph->text, paragraph->length, index, &size);
        for (size_t k = 1; k < size; k++)
        {
            paragraph->workspace[index + k] = muni_bidiContinuation;
            paragraph->levels[index + k] = 0;
        }
        uint8_t type = muniLookupBidiClass(codePoint);
        needed = needed || NeedsImplicit(type);
        Apply(&state, paragraph, index, index + size, type);
        index += size;
    }
    return needed;
}
