// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A password's mask (record mui-0006): an editing block with the password
// rule is laid out, painted, hit and read by accessibility as a bullet
// (U+2022, as Chrome and AppKit draw one) per grapheme cluster, its mask
// made again whenever its text changes. Offsets of the text and of the
// mask match by cluster. muiAccessTextOf (maul-ui/text_block.h) lives
// here, reading a node's text as it is shown.

#ifndef MAUL_UI_SRC_TEXT_MASK_H
#define MAUL_UI_SRC_TEXT_MASK_H

#include "text_block.h"
#include "text_service.h"

#include <stdbool.h>
#include <stdint.h>

// Whether a block is laid out as its mask: an editing password.
bool muiIsMasked(const muiTextBlock* block);

// The block laid out for a block: its mask, made again when its text
// changed, for a password; the block itself otherwise. NULL when memory
// for the mask runs out.
muiTextBlock* muiShownBlock(muiTextService* service, muiTextBlock* block);

// The mask's offset of an offset of a password's text: its cluster's
// bullet, or after the last; the text's of a mask's offset. Offsets of a
// block not masked are their own.
uint32_t muiMaskOffset(const muiTextBlock* block, uint32_t offset);
uint32_t muiUnmaskOffset(const muiTextBlock* block, uint32_t offset);

#endif // MAUL_UI_SRC_TEXT_MASK_H
