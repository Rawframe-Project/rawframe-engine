// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the text editor needs of text blocks (record mui-0006): a block
// by its id, and replacing part of its text as muiTextBlock_Replace does.

#ifndef MAUL_UI_SRC_TEXT_BLOCKS_H
#define MAUL_UI_SRC_TEXT_BLOCKS_H

#include "text_block.h"
#include "text_service.h"

#include "maul-ui/base.h"
#include "maul-ui/text_block.h"

#include <stddef.h>
#include <stdint.h>

// A block by its id; NULL for one that is gone.
muiTextBlock* muiResolveTextBlock(const muiTextService* service, muiTextBlockId blockId);

// Sets a block's text, valid UTF-8 of the block's limit, as
// muiTextBlock_SetText does but keeping its spans and composition; false
// when memory runs out, which keeps the old text.
bool muiSetBlockText(muiTextService* service, muiTextBlock* block, const char* text,
                     uint32_t length);

// Whether replacing the bytes of a block's text from start up to end
// with length bytes is within the text and keeps it within its limit.
bool muiFitsBlockText(const muiTextBlock* block, uint32_t start, uint32_t end, size_t length);

// Whether an offset begins a character of a block's text, or is its end,
// as decoding reads it: a byte past what a sequence's lead takes begins
// one of its own, as the U+FFFD it is laid out as. The text's start
// always does.
bool muiIsCharacterStart(const muiTextBlock* block, uint32_t at);

// Replaces the bytes of a block's text from start up to end with a text
// of length bytes, valid UTF-8 of the block's limit, moving its spans and
// composition: `mui_success`, `mui_errorInvalid` for a range out of the
// text or a result past the limit, `mui_errorCapacity` when memory runs
// out, which keeps the old text.
muiResult muiReplaceBlockText(muiTextService* service, muiTextBlock* block, uint32_t start,
                              uint32_t end, const char* text, size_t length);

#endif // MAUL_UI_SRC_TEXT_BLOCKS_H
