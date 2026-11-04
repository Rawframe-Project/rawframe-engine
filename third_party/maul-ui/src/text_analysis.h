// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A text block's analysis: its line break opportunities (UAX #14) and
// script runs (UAX #24), found paragraph by paragraph into buffers apart
// from the block, so a failure keeps the block's own. An edit finds the
// paragraphs it reaches again and splices them between the block's
// tables before and after it, moved by the change.

#ifndef MAUL_UI_SRC_TEXT_ANALYSIS_H
#define MAUL_UI_SRC_TEXT_ANALYSIS_H

#include "text_block.h"

#include <stdbool.h>
#include <stdint.h>

// A block's text and what was found in it, made apart from the block.
typedef struct muiTextAnalysis
{
    muiBuffer text;
    uint32_t length;
    muiBuffer breaks;
    uint32_t breakCount;
    muiBuffer scripts;
    uint32_t scriptCount;
} muiTextAnalysis;

void muiFreeTextAnalysis(const muiAllocator* allocator, muiTextAnalysis* analysis);

// Analyzes a text of at most MUI_MAX_TEXT_BYTES, copied first; false when
// memory runs out, with nothing held.
bool muiAnalyzeText(const muiAllocator* allocator, const char* text, uint32_t length,
                    muiTextAnalysis* analysis);

// Analyzes a block's text with its bytes from start up to end replaced by
// a text of length bytes, which may be part of the old, as
// muiBlockTextStatus allowed: the paragraphs the edit reaches are found
// again, from past the last mandatory break before start up to the first
// paragraph's end past the new text, whose separator the edit leaves as
// it was, and the rest of the block's tables move by the change. Writes
// where the paragraphs found again lie in the new text; false when memory
// runs out, with nothing held.
bool muiAnalyzeReplace(const muiAllocator* allocator, const muiTextBlock* block, uint32_t start,
                       uint32_t end, const char* text, uint32_t length, muiTextAnalysis* analysis,
                       uint32_t* fromOut, uint32_t* toOut);

#endif // MAUL_UI_SRC_TEXT_ANALYSIS_H
