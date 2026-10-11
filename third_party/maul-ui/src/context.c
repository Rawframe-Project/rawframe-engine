// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and the one block of memory it reserves at creation.

#include "context.h"

#include "allocator.h"
#include "invariant.h"

#include "maul-ui/style.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Slots are 1-based uint32_t values with room for a parent link count.
#define MAX_SLOTS 0x7FFFFFFFu
static_assert(MAX_SLOTS < MUI_TRANSFORM_SCALE, "a transform's owner has its slot and a flag");

// Every part after the context starts on a cache line, so a record whose
// size is a multiple of one never has a field split across two, wherever
// the parts before it end.
#define CACHE_LINE ((size_t)64)

static_assert(alignof(max_align_t) <= CACHE_LINE, "a cache line aligns every part");

// Every limit is a count, and each fits a slot; the ones with a
// placeholder at 0 have one fewer, and a context has a node.
static_assert(sizeof(muiLimits) % sizeof(uint32_t) == 0 && alignof(muiLimits) == alignof(uint32_t),
              "the limits are counts");

static bool AreLimitsValid(const muiLimits* limits)
{
    uint32_t counts[sizeof(muiLimits) / sizeof(uint32_t)];
    memcpy(counts, limits, sizeof counts);
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++)
    {
        if (counts[i] > MAX_SLOTS)
        {
            return false;
        }
    }
    return limits->nodes != 0 && limits->drawClips < MAX_SLOTS &&
           limits->drawGradients < MAX_SLOTS && limits->drawTransforms < MAX_SLOTS &&
           limits->pointers <= MUI_MAX_POINTERS;
}

// Where each part of the context's block starts.
typedef struct Parts
{
    size_t nodes;
    size_t layout;
    size_t paddings;
    size_t visual;
    size_t text;
    size_t textRecords;
    size_t interaction;
    size_t scrolls;
    size_t nodeStyles;
    size_t classSlots;
    size_t classes;
    size_t typeSlots;
    size_t types;
    size_t setSlots;
    size_t sets;
    size_t notifications;
    size_t specSlots;
    size_t specs;
    size_t recordSlots;
    size_t records;
    size_t tokenSlots;
    size_t tokens;
    size_t nameSlots;
    size_t names;
    size_t themeSlots;
    size_t themeTables;
    size_t overrideSlots;
    size_t overrides;
    size_t paintStates;
    size_t drawCommands;
    size_t drawClips;
    size_t drawGradients;
    size_t drawGlyphs;
    size_t drawTransforms;
    size_t drawTransformLinks;
    size_t ranges;
    size_t popups;
    size_t exits;
    size_t lists;
    size_t listSizes;
    size_t listSums;
    size_t listItems;
    size_t layers;
    size_t pointers;
    size_t pointerRecords;
    size_t neighbors;
    size_t routes;
    size_t accessEntries;
    size_t accessEntryOf;
    size_t accessRoots;
} Parts;

// A table per theme, an entry per token slot; a count past size_t marks
// the layout as overflowing.
static size_t ThemeTableEntries(muiLayout* layout, const muiLimits* limits)
{
    uint64_t entries = (uint64_t)limits->themes * limits->tokens;
    if (entries > SIZE_MAX)
    {
        layout->overflow = true;
        return 0;
    }
    return (size_t)entries;
}

// A part sized by one of the limits: where its offset goes in Parts and
// where its count is in muiLimits, in bytes, and its item's size.
typedef struct PartSpec
{
    uint16_t part;
    uint16_t limit;
    uint16_t size;
} PartSpec;

static_assert(sizeof(Parts) <= UINT16_MAX && sizeof(muiLimits) <= UINT16_MAX &&
                  sizeof(muiContext) <= UINT16_MAX,
              "the tables' offsets fit 16 bits");

#define PART(field, count, type)                                                                   \
    {(uint16_t)offsetof(Parts, field), (uint16_t)offsetof(muiLimits, count), (uint16_t)sizeof(type)}

static const PartSpec s_parts[] = {
    PART(nodes, nodes, muiTreeNode),
    PART(layout, nodes, muiLayoutNode),
    PART(paddings, nodes, muiEdges),
    PART(visual, nodes, muiVisualStyle),
    PART(text, nodes, muiTextStyle),
    PART(textRecords, nodes, muiTextRecord),
    PART(interaction, nodes, muiInteractionStyle),
    PART(scrolls, nodes, muiScrollState),
    PART(nodeStyles, nodes, muiNodeStyle),
    PART(classSlots, styles, muiPoolSlot),
    PART(classes, styles, muiStyleClass),
    PART(typeSlots, nodeTypes, muiPoolSlot),
    PART(types, nodeTypes, muiClassList),
    PART(setSlots, propertySets, muiPoolSlot),
    PART(sets, propertySets, muiPropertySet),
    PART(notifications, notifications, muiNotification),
    PART(specSlots, transitions, muiPoolSlot),
    PART(specs, transitions, muiTransitionSpec),
    PART(recordSlots, animations, muiPoolSlot),
    PART(records, animations, muiAnimation),
    PART(tokenSlots, tokens, muiPoolSlot),
    PART(tokens, tokens, muiToken),
    PART(nameSlots, tokenNames, muiPoolSlot),
    PART(names, tokenNames, muiTokenName),
    PART(themeSlots, themes, muiPoolSlot),
    PART(overrideSlots, themeOverrides, muiPoolSlot),
    PART(overrides, themeOverrides, muiThemeOverride),
    PART(paintStates, nodes, muiPaintState),
    PART(ranges, ranges, muiRangeEntry),
    PART(popups, popups, muiPopupEntry),
    PART(exits, exits, muiExitEntry),
    PART(lists, virtualLists, muiVirtualEntry),
    PART(listSizes, virtualItems, float),
    PART(listSums, virtualItems, double),
    PART(listItems, nodes, uint32_t),
    PART(layers, layers, muiLayerEntry),
    PART(pointers, pointers, muiPointer),
    PART(pointerRecords, pointerRecords, muiPointerRecord),
    PART(neighbors, neighbors, muiNeighbor),
    PART(routes, nodes, muiNodeId),
    PART(accessEntries, accessNodes, muiAccessEntry),
    PART(accessEntryOf, nodes, uint32_t),
    PART(accessRoots, accessRoots, muiAccessRoot),
};

static Parts LayOut(muiLayout* layout, const muiLimits* limits)
{
    // The context opens the block, so the block is freed through it.
    size_t contextOffset = muiLayoutAdd(layout, 1, sizeof(muiContext), alignof(muiContext));
    MUI_ASSERT(contextOffset == 0);
    (void)contextOffset;
    Parts parts = {0};
    parts.themeTables =
        muiLayoutAdd(layout, ThemeTableEntries(layout, limits), sizeof(uint32_t), CACHE_LINE);
    // Two lists: the last, and the one built from it. Clips and gradients
    // have the placeholder for none at 0.
    parts.drawCommands =
        muiLayoutAdd(layout, (size_t)limits->drawCommands * 2, sizeof(muiDrawCommand), CACHE_LINE);
    parts.drawClips =
        muiLayoutAdd(layout, ((size_t)limits->drawClips + 1) * 2, sizeof(muiDrawClip), CACHE_LINE);
    parts.drawGradients = muiLayoutAdd(layout, ((size_t)limits->drawGradients + 1) * 2,
                                       sizeof(muiDrawGradient), CACHE_LINE);
    parts.drawGlyphs =
        muiLayoutAdd(layout, (size_t)limits->drawGlyphs * 2, sizeof(muiGlyph), CACHE_LINE);
    parts.drawTransforms = muiLayoutAdd(layout, ((size_t)limits->drawTransforms + 1) * 2,
                                        sizeof(muiDrawTransform), CACHE_LINE);
    // Owners and parents, side by side.
    parts.drawTransformLinks = muiLayoutAdd(layout, ((size_t)limits->drawTransforms + 1) * 4,
                                            sizeof(uint32_t), CACHE_LINE);
    for (size_t i = 0; i < sizeof s_parts / sizeof s_parts[0]; i++)
    {
        uint32_t count = 0;
        memcpy(&count, (const unsigned char*)limits + s_parts[i].limit, sizeof count);
        size_t offset = muiLayoutAdd(layout, count, s_parts[i].size, CACHE_LINE);
        memcpy((unsigned char*)&parts + s_parts[i].part, &offset, sizeof offset);
    }
    return parts;
}

// A store's table placed by its part alone: where its pointer is in the
// context, in bytes, and its part's offset in Parts.
typedef struct TableSpec
{
    uint16_t field;
    uint16_t part;
} TableSpec;

#define TABLE(field, part) {(uint16_t)offsetof(muiContext, field), (uint16_t)offsetof(Parts, part)}

static const TableSpec s_tables[] = {
    TABLE(layout, layout),
    TABLE(paddings, paddings),
    TABLE(visual, visual),
    TABLE(text, text),
    TABLE(textRecords, textRecords),
    TABLE(interaction, interaction),
    TABLE(scrolls, scrolls),
    TABLE(style.nodes, nodeStyles),
    TABLE(style.classes, classes),
    TABLE(style.types, types),
    TABLE(style.sets, sets),
    TABLE(style.names, names),
    TABLE(animations.specs, specs),
    TABLE(animations.records, records),
    TABLE(tokens.tokens, tokens),
    TABLE(themes.tables, themeTables),
    TABLE(themes.overrides, overrides),
    TABLE(draw.states, paintStates),
};

// A pool placed by its part: where it is in the context, its slots'
// part and its capacity's limit, in bytes.
typedef struct PoolSpec
{
    uint16_t pool;
    uint16_t part;
    uint16_t limit;
} PoolSpec;

#define POOL(pool, part, count)                                                                    \
    {(uint16_t)offsetof(muiContext, pool), (uint16_t)offsetof(Parts, part),                        \
     (uint16_t)offsetof(muiLimits, count)}

static const PoolSpec s_pools[] = {
    POOL(style.classPool, classSlots, styles),
    POOL(style.typePool, typeSlots, nodeTypes),
    POOL(style.setPool, setSlots, propertySets),
    POOL(style.namePool, nameSlots, tokenNames),
    POOL(animations.specPool, specSlots, transitions),
    POOL(animations.pool, recordSlots, animations),
    POOL(tokens.pool, tokenSlots, tokens),
    POOL(themes.pool, themeSlots, themes),
    POOL(themes.overridePool, overrideSlots, themeOverrides),
};

// The part's start in the block.
static unsigned char* PartAt(unsigned char* base, const Parts* parts, uint16_t part)
{
    size_t offset = 0;
    memcpy(&offset, (const unsigned char*)parts + part, sizeof offset);
    return base + offset;
}

// Points the virtual lists' and accessibility's tables into the block.
static void PlaceListsAndAccess(muiContext* context, unsigned char* base, const Parts* parts,
                                const muiLimits* limits)
{
    context->lists = (muiVirtualStore){
        .entries = (muiVirtualEntry*)(base + parts->lists),
        .capacity = limits->virtualLists,
        .sizes = (float*)(base + parts->listSizes),
        .sums = (double*)(base + parts->listSums),
        .itemCapacity = limits->virtualItems,
        .items = (uint32_t*)(base + parts->listItems),
    };
    context->access = (muiAccessStore){
        .entries = (muiAccessEntry*)(base + parts->accessEntries),
        .capacity = limits->accessNodes,
        .entryOf = (uint32_t*)(base + parts->accessEntryOf),
        .roots = (muiAccessRoot*)(base + parts->accessRoots),
        .rootCapacity = limits->accessRoots,
    };
}

// Points the context's stores at their parts of the block.
static void Place(muiContext* context, unsigned char* base, const Parts* parts,
                  const muiLimits* limits)
{
    muiTreeInit(&context->tree, (muiTreeNode*)(base + parts->nodes), limits->nodes);
    for (size_t i = 0; i < sizeof s_tables / sizeof s_tables[0]; i++)
    {
        void* table = PartAt(base, parts, s_tables[i].part);
        memcpy((unsigned char*)context + s_tables[i].field, (const void*)&table, sizeof table);
    }
    for (size_t i = 0; i < sizeof s_pools / sizeof s_pools[0]; i++)
    {
        uint32_t capacity = 0;
        memcpy(&capacity, (const unsigned char*)limits + s_pools[i].limit, sizeof capacity);
        muiPoolInit((muiPool*)((unsigned char*)context + s_pools[i].pool),
                    (muiPoolSlot*)PartAt(base, parts, s_pools[i].part), capacity);
    }
    context->environment = muiDefaultEnvironment();
    muiNotifyInit(&context->notifications, (muiNotification*)(base + parts->notifications),
                  limits->notifications);
    context->themes.tokenCapacity = limits->tokens;
    muiDrawStore* draw = &context->draw;
    draw->commandCapacity = limits->drawCommands;
    draw->clipCapacity = limits->drawClips + 1;
    draw->gradientCapacity = limits->drawGradients + 1;
    draw->glyphCapacity = limits->drawGlyphs;
    draw->transformCapacity = limits->drawTransforms + 1;
    for (uint32_t i = 0; i < 2; i++)
    {
        draw->tables[i] = (muiDrawTables){
            .commands = (muiDrawCommand*)(base + parts->drawCommands) + i * draw->commandCapacity,
            .clips = (muiDrawClip*)(base + parts->drawClips) + i * draw->clipCapacity,
            .gradients =
                (muiDrawGradient*)(base + parts->drawGradients) + i * draw->gradientCapacity,
            .glyphs = (muiGlyph*)(base + parts->drawGlyphs) + i * draw->glyphCapacity,
            .transforms =
                (muiDrawTransform*)(base + parts->drawTransforms) + i * draw->transformCapacity,
            .transformOwners =
                (uint32_t*)(base + parts->drawTransformLinks) + 2 * i * draw->transformCapacity,
            .transformParents = (uint32_t*)(base + parts->drawTransformLinks) +
                                (2 * i + 1) * draw->transformCapacity,
            .clipCount = 1,
            .gradientCount = 1,
            .transformCount = 1,
        };
        draw->tables[i].transforms[0] = (muiDrawTransform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }
    muiScrollInit(&context->scrolling);
    muiRangeInit(&context->ranges, (muiRangeEntry*)(base + parts->ranges), limits->ranges);
    muiPopupInit(&context->popups, (muiPopupEntry*)(base + parts->popups), limits->popups);
    muiExitInit(&context->exits, (muiExitEntry*)(base + parts->exits), limits->exits);
    PlaceListsAndAccess(context, base, parts, limits);
    muiLayerInit(&context->layers, (muiLayerEntry*)(base + parts->layers), limits->layers);
    muiEventInit(&context->events, (muiNodeId*)(base + parts->routes));
    muiFocusInit(&context->focus, (muiNeighbor*)(base + parts->neighbors), limits->neighbors);
    muiPointerInit(&context->pointers, (muiPointer*)(base + parts->pointers), limits->pointers,
                   (muiPointerRecord*)(base + parts->pointerRecords), limits->pointerRecords);
}

muiResult muiCreateContext(const muiContextDef* def, muiContext** contextOut)
{
    if (contextOut != nullptr)
    {
        *contextOut = nullptr;
    }
    if (def == nullptr || contextOut == nullptr || def->cookie != MUI_CONTEXT_DEF_COOKIE)
    {
        return mui_errorInvalid;
    }
    // Another version's def may be of another layout: nothing past the
    // version is read.
    if (def->version != MUI_DEF_VERSION)
    {
        return mui_errorVersion;
    }
    if (!muiIsAllocatorValid(&def->allocator) || !AreLimitsValid(&def->limits))
    {
        return mui_errorInvalid;
    }
    muiLayout layout = {0};
    Parts parts = LayOut(&layout, &def->limits);
    if (layout.overflow)
    {
        return mui_errorCapacity;
    }
    // The allocator gives max_align_t; this much more reaches the cache
    // line the parts are placed from.
    size_t size = 0;
    if (ckd_add(&size, layout.size, CACHE_LINE - alignof(max_align_t)))
    {
        return mui_errorCapacity;
    }
    unsigned char* block = muiAllocate(&def->allocator, size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, size);
    unsigned char* base = block + (CACHE_LINE - (uintptr_t)block % CACHE_LINE) % CACHE_LINE;
    MUI_ASSERT(base - block <= (ptrdiff_t)(CACHE_LINE - alignof(max_align_t)));
    muiContext* context = (muiContext*)block;
    context->allocator = def->allocator;
    context->blockSize = size;
    Place(context, base, &parts, &def->limits);
    *contextOut = context;
    return mui_success;
}

void muiDestroyContext(muiContext* context)
{
    if (context == nullptr)
    {
        return;
    }
    const muiAllocator allocator = context->allocator;
    muiAccessRelease(&context->access, &allocator);
    muiRelease(&allocator, context, context->blockSize, alignof(max_align_t));
}

uint64_t muiGetContextMisuse(const muiContext* context)
{
    return context != nullptr ? context->misuse : 0;
}

muiWorkCounts muiGetWorkCounts(const muiContext* context)
{
    return context != nullptr ? context->work : (muiWorkCounts){0};
}

muiResult muiRefuse(muiContext* context)
{
    context->misuse++;
    return mui_errorInvalid;
}

bool muiIsInHostCall(const muiContext* context)
{
    return context->inHostCall;
}

uint32_t muiResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0 || muiIsInHostCall(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiNextNotification(muiContext* context, muiNotification* notificationOut)
{
    if (context == nullptr || notificationOut == nullptr)
    {
        return mui_errorInvalid;
    }
    return muiNotifyTake(&context->notifications, notificationOut) ? mui_success : mui_empty;
}
