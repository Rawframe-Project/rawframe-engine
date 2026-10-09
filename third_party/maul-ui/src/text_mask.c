// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A password's mask (record mui-0006).

#include "text_mask.h"

#include "allocator.h"
#include "text_blocks.h"

#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-unicode/segment.h"

#include <stdalign.h>
#include <string.h>

// U+2022 BULLET in UTF-8.
static const char s_bullet[] = "\xE2\x80\xA2";

enum
{
    BULLET_BYTES = 3
};

bool muiIsMasked(const muiTextBlock* block)
{
    return block->editing.on && (block->editing.def.flags & mui_editPassword) != 0;
}

// The clusters of a text that end at or before an offset: the index of
// the cluster the offset is in, or starts.
static uint32_t ClustersBefore(const muiTextBlock* block, uint32_t offset)
{
    muniSegmentIterator iterator;
    if (block->length == 0 ||
        muniInitGraphemeIterator(&iterator, block->text.data, block->length, false) != muni_success)
    {
        return 0;
    }
    uint32_t count = 0;
    size_t at = 0;
    while (muniNextSegmentBreak(&iterator, &at) == muni_success && at <= offset)
    {
        count += at != 0 ? 1u : 0u;
    }
    return count;
}

// The offset where a text's cluster of an index starts; its end past
// the last.
static uint32_t ClusterStart(const muiTextBlock* block, uint32_t index)
{
    muniSegmentIterator iterator;
    if (index == 0 ||
        muniInitGraphemeIterator(&iterator, block->text.data, block->length, false) != muni_success)
    {
        return 0;
    }
    size_t at = 0;
    while (index != 0 && muniNextSegmentBreak(&iterator, &at) == muni_success)
    {
        index -= at != 0 ? 1u : 0u;
    }
    // Past the last cluster the iterator stands at the text's end.
    return (uint32_t)at;
}

uint32_t muiMaskOffset(const muiTextBlock* block, uint32_t offset)
{
    return muiIsMasked(block) ? ClustersBefore(block, offset) * BULLET_BYTES : offset;
}

uint32_t muiUnmaskOffset(const muiTextBlock* block, uint32_t offset)
{
    return muiIsMasked(block) ? ClusterStart(block, offset / BULLET_BYTES) : offset;
}

muiTextBlock* muiShownBlock(muiTextService* service, muiTextBlock* block)
{
    if (!muiIsMasked(block))
    {
        return block;
    }
    if (block->mask != nullptr && block->maskRevision == block->revision)
    {
        return block->mask;
    }
    if (block->mask == nullptr)
    {
        block->mask = muiAllocate(&service->allocator, sizeof *block->mask, alignof(muiTextBlock));
        if (block->mask == nullptr)
        {
            return nullptr;
        }
        *block->mask = (muiTextBlock){0};
    }
    uint32_t count = ClustersBefore(block, block->length);
    if (!muiReserve(&service->allocator, &service->editScratch, (size_t)count * BULLET_BYTES + 1u))
    {
        return nullptr;
    }
    char* bullets = service->editScratch.data;
    for (uint32_t i = 0; i < count; i++)
    {
        memcpy(bullets + (size_t)i * BULLET_BYTES, s_bullet, BULLET_BYTES);
    }
    if (!muiSetBlockText(service, block->mask, bullets, count * BULLET_BYTES))
    {
        return nullptr;
    }
    block->maskRevision = block->revision;
    return block->mask;
}

bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, const char** textOut,
                     size_t* lengthOut)
{
    (void)nodeId;
    const muiTextHost* host = user;
    const muiTextBlockId blockId = {(uint32_t)hostKey, (uint32_t)(hostKey >> 32)};
    muiTextBlock* block = host != nullptr && host->service != nullptr
                              ? muiResolveTextBlock(host->service, blockId)
                              : nullptr;
    // A password reads as its mask.
    const muiTextBlock* shown = block != nullptr ? muiShownBlock(host->service, block) : nullptr;
    if (shown == nullptr)
    {
        return false;
    }
    *textOut = shown->text.data;
    *lengthOut = shown->length;
    return true;
}
