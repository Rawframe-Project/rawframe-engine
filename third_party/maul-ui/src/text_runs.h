// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Run styles (record mui-0006): where a block's spans set a font, size,
// weight, slant or baseline shift, its text is shaped and placed in runs
// of those apart from the node's style. A block's run styles are the node's and each distinct
// one its spans make, each byte with its own; a paragraph's chains are
// every face of every run style, the node's first, each style trying its
// own in its own order.

#ifndef MAUL_UI_SRC_TEXT_RUNS_H
#define MAUL_UI_SRC_TEXT_RUNS_H

#include "font_chain.h"
#include "text_block.h"
#include "text_service.h"

#include "maul-ui/text_style.h"

#include <stdbool.h>
#include <stdint.h>

enum
{
    // The most run styles of a block: the node's and 31 its spans make;
    // spans making more are shaped in the style under them.
    MUI_MAX_RUN_STYLES = 32
};

// What a run is shaped in: a font key, a weight, a slant and a size, and
// the size over the node's (1 for the node's own style); and how far
// above the baseline it sits.
typedef struct muiRunStyle
{
    uint64_t font;
    float weight;
    float size;
    float scale;
    float shift;
    muiFontSlant slant;
} muiRunStyle;

// A paragraph's chains: the faces of its run styles in one chain, and
// each style's order of trying them.
typedef struct muiRunChains
{
    muiFontChain chain;
    uint8_t order[MUI_MAX_RUN_STYLES][MUI_MAX_CHAIN];
    uint8_t orderCount[MUI_MAX_RUN_STYLES];
    uint32_t styleCount;
} muiRunChains;

// Makes a block's run styles, and each byte's, for a node's computed
// style, unless they are those last made; none when no span sets what
// shapes. False when memory runs out.
bool muiPrepareRuns(muiTextService* service, muiTextBlock* block,
                    const muiComputedTextStyle* style);

// The chains of a block's run styles, or of the node's style alone;
// false when the node's font names no face.
bool muiBuildRunChains(const muiTextService* service, const muiTextBlock* block,
                       const muiComputedTextStyle* style, muiRunChains* out);

// A byte's run style; 0, the node's, in a block without run styles.
static inline uint32_t muiRunOf(const muiTextBlock* block, uint32_t offset)
{
    return block->runStyleCount != 0 ? ((const uint8_t*)block->runs.data)[offset] : 0;
}

// How far a run style sits above the baseline.
static inline float muiRunShift(const muiTextBlock* block, uint32_t style)
{
    return block->runStyleCount != 0 ? ((const muiRunStyle*)block->runStyles.data)[style].shift
                                     : 0.0f;
}

// A run style's size over the node's.
static inline float muiRunScale(const muiTextBlock* block, uint32_t style)
{
    return block->runStyleCount != 0 ? ((const muiRunStyle*)block->runStyles.data)[style].scale
                                     : 1.0f;
}

#endif // MAUL_UI_SRC_TEXT_RUNS_H
