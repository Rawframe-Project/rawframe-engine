// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a text block reads as to accessibility (records mui-0006 and
// mui-0008, I123): its text as shown, its editing selection, its lines
// and its grapheme clusters where painting lays them out, and its words;
// and the requests that set the selection and replace text, in the
// offsets it reads as.

#include "allocator.h"
#include "text_block.h"
#include "text_blocks.h"
#include "text_boxes.h"
#include "text_editing.h"
#include "text_mask.h"
#include "text_service.h"

#include "maul-ui/access.h"
#include "maul-ui/layout.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_editor.h"
#include "maul-unicode/segment.h"

#include <string.h>

// A line's clusters, after those kept, in the node's own space: the
// content box's place added; false when memory runs out.
static bool ReadClusters(const muiLaidText* laid, uint32_t index, muiRect content,
                         muiTextBlock* block, uint32_t* countInOut)
{
    muiTextBoxes boxes;
    muiTextService* service = laid->paragraph.service;
    uint32_t kept = *countInOut;
    if (!muiGetLineBoxes(laid, index, &boxes) ||
        !muiReserveKeeping(&service->allocator, &block->accessClusters,
                           (kept + boxes.count) * sizeof(muiAccessCluster),
                           kept * sizeof(muiAccessCluster)))
    {
        return false;
    }
    muiAccessCluster* clusters = block->accessClusters.data;
    for (uint32_t i = 0; i < boxes.count; i++)
    {
        const muiTextBox* box = &boxes.data[i];
        clusters[kept + i] =
            (muiAccessCluster){box->start, box->end, content.x + box->x0, content.x + box->x1};
    }
    *countInOut = kept + boxes.count;
    return true;
}

// The most clusters read of a text, so that a long one costs what is
// near the screen.
#define MOST_CLUSTERS 16384u

// Marks which lines' clusters to leave out: all but those within a
// shown height of shown, those overlapping shown first, then the rest
// top down, while their bytes, which bound their clusters, stay within
// MOST_CLUSTERS.
static void OmitFar(const muiLaidText* laid, muiRect content, muiRect shown,
                    muiAccessLineBox* lineBoxes)
{
    float nearTop = shown.y - shown.height;
    float nearBottom = shown.y + 2.0f * shown.height;
    uint32_t budget = MOST_CLUSTERS;
    for (uint32_t i = 0; i < laid->lineCount; i++)
    {
        lineBoxes[i].omitted = true;
    }
    for (int pass = 0; pass < 2; pass++)
    {
        float from = pass == 0 ? shown.y : nearTop;
        float to = pass == 0 ? shown.y + shown.height : nearBottom;
        for (uint32_t i = 0; i < laid->lineCount; i++)
        {
            const muiTextLine* line = &laid->lines[i];
            float top = content.y + line->top;
            uint32_t bytes = line->end - line->start;
            if (lineBoxes[i].omitted && top + line->height >= from && top <= to && bytes <= budget)
            {
                lineBoxes[i].omitted = false;
                budget -= bytes;
            }
        }
    }
}

// Where the lines of a node's text start and where the clusters of those
// near what is shown are, as painting lays them out in its content box,
// kept in the block; false when they cannot be laid out.
static bool ReadLines(const muiTextHost* host, muiNodeId nodeId, muiRect shown, muiTextBlock* block,
                      muiAccessTextMarks* marks)
{
    muiRect content = muiNode_GetContentRect(host->context, nodeId);
    muiLaidText laid;
    muiAllocator* allocator = &host->service->allocator;
    if (muiLayText(host, nodeId, content.width, &laid) != mui_success || laid.lineCount == 0 ||
        !muiReserve(allocator, &block->accessLines, laid.lineCount * sizeof(uint32_t)) ||
        !muiReserve(allocator, &block->accessLineBoxes, laid.lineCount * sizeof(muiAccessLineBox)))
    {
        return false;
    }
    uint32_t* starts = block->accessLines.data;
    muiAccessLineBox* lineBoxes = block->accessLineBoxes.data;
    OmitFar(&laid, content, shown, lineBoxes);
    uint32_t clusterCount = 0;
    for (uint32_t i = 0; i < laid.lineCount; i++)
    {
        const muiTextLine* line = &laid.lines[i];
        starts[i] = line->start;
        lineBoxes[i].top = content.y + line->top;
        lineBoxes[i].bottom = content.y + line->top + line->height;
        lineBoxes[i].firstCluster = clusterCount;
        if (!lineBoxes[i].omitted && !ReadClusters(&laid, i, content, block, &clusterCount))
        {
            return false;
        }
    }
    marks->lineStarts = starts;
    marks->lineCount = laid.lineCount;
    marks->lineBoxes = lineBoxes;
    marks->clusters = clusterCount != 0 ? block->accessClusters.data : nullptr;
    marks->clusterCount = clusterCount;
    return true;
}

// The word segments with a letter or a number of bytes from up to to of
// a text, put in a buffer after count words, the count moved past them;
// false when memory runs out. A paragraph's start and end are word
// breaks, so whole paragraphs are segmented alone.
static bool SegmentWords(const muiAllocator* allocator, const char* text, uint32_t from,
                         uint32_t to, muiBuffer* buffer, uint32_t* countInOut)
{
    muniSegmentIterator iterator;
    if (to == from ||
        muniInitWordIterator(&iterator, text + from, to - from, false) != muni_success)
    {
        return true;
    }
    uint32_t count = *countInOut;
    size_t start = 0;
    size_t end = 0;
    while (muniNextSegmentBreak(&iterator, &end) == muni_success)
    {
        if (end > start && muiIsWordSegment(text + from, start, end))
        {
            if (!muiReserveKeeping(allocator, buffer, (count + 1) * sizeof(muiAccessWord),
                                   count * sizeof(muiAccessWord)))
            {
                return false;
            }
            ((muiAccessWord*)buffer->data)[count++] =
                (muiAccessWord){from + (uint32_t)start, from + (uint32_t)end};
        }
        start = end;
    }
    *countInOut = count;
    return true;
}

// The first of count words that ends past an offset (start false) or
// starts at it or past it (start true).
static uint32_t WordFrom(const muiAccessWord* words, uint32_t count, uint32_t offset, bool start)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        bool before = start ? words[middle].start < offset : words[middle].end <= offset;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low;
}

// The words of the paragraphs edits changed in place of those they had,
// segmented apart; those after moved by the change in length.
static bool SpliceWords(const muiAllocator* allocator, const char* text, uint32_t length,
                        muiWordCache* cache)
{
    uint32_t from = cache->stale.start;
    uint32_t to = cache->stale.end;
    int64_t delta = (int64_t)length - (int64_t)cache->length;
    uint32_t first = WordFrom(cache->words.data, cache->count, from, false);
    uint32_t after =
        WordFrom(cache->words.data, cache->count, (uint32_t)((int64_t)to - delta), true);
    muiBuffer region = {0};
    uint32_t added = 0;
    uint32_t tail = cache->count - after;
    size_t size = sizeof(muiAccessWord);
    bool fits = SegmentWords(allocator, text, from, to, &region, &added) &&
                muiReserveKeeping(allocator, &cache->words, (first + added + tail) * size,
                                  cache->count * size);
    if (fits)
    {
        muiAccessWord* words = cache->words.data;
        memmove(words + first + added, words + after, tail * size);
        if (added != 0)
        {
            memcpy(words + first, region.data, added * size);
        }
        for (uint32_t i = first + added; i < first + added + tail; i++)
        {
            words[i].start = (uint32_t)((int64_t)words[i].start + delta);
            words[i].end = (uint32_t)((int64_t)words[i].end + delta);
        }
        cache->count = first + added + tail;
    }
    muiFreeBuffer(allocator, &region);
    return fits;
}

// A text's words, kept in the block between reads: segmented whole the
// first time and after a change they did not follow, else only in the
// paragraphs edits changed since. False when memory runs out, the words
// then segmented whole the next time.
static bool ReadWords(muiTextService* service, muiTextBlock* block, muiAccessTextMarks* marks)
{
    muiWordCache* cache = &block->accessWords;
    const char* text = block->text.data;
    bool kept = cache->made && cache->revision == block->revision;
    cache->made = false;
    if (!kept)
    {
        cache->count = 0;
        if (!SegmentWords(&service->allocator, text, 0, block->length, &cache->words,
                          &cache->count))
        {
            return false;
        }
    }
    else if (cache->stale.on && !SpliceWords(&service->allocator, text, block->length, cache))
    {
        return false;
    }
    *cache = (muiWordCache){.words = cache->words,
                            .count = cache->count,
                            .length = block->length,
                            .revision = block->revision,
                            .made = true};
    marks->words = cache->count != 0 ? cache->words.data : nullptr;
    marks->wordCount = cache->count;
    return true;
}

bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, const muiRect* shown,
                     muiAccessContent* contentOut)
{
    const muiTextHost* host = user;
    const muiTextBlockId blockId = {(uint32_t)hostKey, (uint32_t)(hostKey >> 32)};
    muiTextBlock* block = host != nullptr && host->service != nullptr
                              ? muiResolveTextBlock(host->service, blockId)
                              : nullptr;
    // A password reads as its mask.
    const muiTextBlock* read = block != nullptr ? muiShownBlock(host->service, block) : nullptr;
    if (read == nullptr || contentOut == nullptr)
    {
        return false;
    }
    // An empty block may have no buffer yet.
    const char* text = read->text.data != nullptr ? read->text.data : "";
    *contentOut = (muiAccessContent){.text = text, .length = read->length};
    muiAccessTextMarks* marks = &contentOut->marks;
    if (block->editing.on)
    {
        const muiTextSelection* selection = &block->editing.selection;
        marks->anchor = muiMaskOffset(block, selection->anchor);
        marks->focus = muiMaskOffset(block, selection->caret.offset);
        marks->selected = true;
    }
    // Lines and words left out when they cannot be read: the text is
    // still read. A password's bullets are no words.
    if (shown != nullptr && !ReadLines(host, nodeId, *shown, block, marks))
    {
        *marks = (muiAccessTextMarks){
            .anchor = marks->anchor, .focus = marks->focus, .selected = marks->selected};
    }
    if (shown != nullptr && !muiIsMasked(block) && !ReadWords(host->service, block, marks))
    {
        marks->words = nullptr;
        marks->wordCount = 0;
    }
    return true;
}

// Whether an offset of the text as shown lies at a character's start.
static bool IsShownStart(const muiTextBlock* shown, uint32_t offset)
{
    const unsigned char* text = shown->text.data;
    return offset == shown->length || (offset < shown->length && (text[offset] & 0xC0u) != 0x80u);
}

muiResult muiTextPerformAccessAction(const muiTextHost* host, const muiAccessRequest* request,
                                     muiTextEditOutcome* outcomeOut)
{
    if (host == nullptr || host->service == nullptr || host->context == nullptr ||
        request == nullptr || outcomeOut == nullptr)
    {
        return muiRefuseEdit(host != nullptr ? host->service : nullptr);
    }
    *outcomeOut = (muiTextEditOutcome){false, false, false, false};
    if (request->action != mui_actionSetSelection && request->action != mui_actionReplaceText)
    {
        return mui_empty;
    }
    muiNodeId nodeId = muiNodeIdOfAccess(request->target);
    uint64_t key = muiNode_GetHostKey(host->context, nodeId);
    const muiTextBlockId blockId = {(uint32_t)key, (uint32_t)(key >> 32)};
    muiTextBlock* block = key != 0 ? muiResolveTextBlock(host->service, blockId) : nullptr;
    if (block == nullptr)
    {
        return mui_errorStale;
    }
    const muiTextBlock* shown = muiShownBlock(host->service, block);
    if (shown == nullptr)
    {
        return mui_errorCapacity;
    }
    if (!block->editing.on || !IsShownStart(shown, request->anchor) ||
        !IsShownStart(shown, request->focus) ||
        (request->action == mui_actionReplaceText && request->text == nullptr &&
         request->length != 0))
    {
        return muiRefuseEdit(host->service);
    }
    const muiTextSelection before = block->editing.selection;
    const muiTextSelection selection = {
        muiUnmaskOffset(block, request->anchor),
        {muiUnmaskOffset(block, request->focus), mui_affinityDownstream}};
    muiTextEditOutcome outcome = {true, false, false, false};
    muiResult result = muiTextBlock_Select(host->service, blockId, selection);
    // Text replaced goes in as a paste does: under the block's rules, an
    // edit undone alone.
    if (result == mui_success && request->action == mui_actionReplaceText)
    {
        result = muiTextBlock_Paste(host->service, blockId, request->text, request->length,
                                    &outcome.changed);
    }
    const muiTextSelection* after = &block->editing.selection;
    outcome.selected = after->anchor != before.anchor ||
                       after->caret.offset != before.caret.offset ||
                       after->caret.affinity != before.caret.affinity;
    *outcomeOut = outcome;
    return result;
}
