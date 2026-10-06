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

#define CONTEXT_DEF_COOKIE 0x6D756378u // "mucx"

// Slots are 1-based uint32_t values with room for a parent link count.
#define MAX_SLOTS 0x7FFFFFFFu

// Every part after the context starts on a cache line, so a record whose
// size is a multiple of one never has a field split across two, wherever
// the parts before it end.
#define CACHE_LINE ((size_t)64)

static_assert(alignof(max_align_t) <= CACHE_LINE, "a cache line aligns every part");

muiContextDef muiDefaultContextDef(void)
{
    return (muiContextDef){
        .cookie = CONTEXT_DEF_COOKIE,
        .limits = {.nodes = 4096,
                   .styles = 256,
                   .nodeTypes = 64,
                   .propertySets = 1024,
                   .notifications = 64,
                   .transitions = 64,
                   .animations = 256,
                   .tokens = 256,
                   .tokenNames = 1024,
                   .themes = 16,
                   .themeOverrides = 512,
                   .drawCommands = 8192,
                   .drawClips = 256,
                   .drawGradients = 256,
                   .drawGlyphs = 16384,
                   .layers = 64},
    };
}

static bool AreLimitsValid(const muiLimits* limits)
{
    return limits->nodes != 0 && limits->nodes <= MAX_SLOTS && limits->styles <= MAX_SLOTS &&
           limits->nodeTypes <= MAX_SLOTS && limits->propertySets <= MAX_SLOTS &&
           limits->notifications <= MAX_SLOTS && limits->transitions <= MAX_SLOTS &&
           limits->animations <= MAX_SLOTS && limits->tokens <= MAX_SLOTS &&
           limits->tokenNames <= MAX_SLOTS && limits->themes <= MAX_SLOTS &&
           limits->themeOverrides <= MAX_SLOTS && limits->drawCommands <= MAX_SLOTS &&
           limits->drawClips < MAX_SLOTS && limits->drawGradients < MAX_SLOTS &&
           limits->drawGlyphs <= MAX_SLOTS && limits->layers <= MAX_SLOTS;
}

// Where each part of the context's block starts.
typedef struct Parts
{
    size_t nodes;
    size_t layout;
    size_t visual;
    size_t text;
    size_t textRecords;
    size_t interaction;
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
    size_t layers;
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

static Parts LayOut(muiLayout* layout, const muiLimits* limits)
{
    // The context opens the block, so the block is freed through it.
    size_t contextOffset = muiLayoutAdd(layout, 1, sizeof(muiContext), alignof(muiContext));
    MUI_ASSERT(contextOffset == 0);
    (void)contextOffset;
    return (Parts){
        .nodes = muiLayoutAdd(layout, limits->nodes, sizeof(muiTreeNode), CACHE_LINE),
        .layout = muiLayoutAdd(layout, limits->nodes, sizeof(muiLayoutNode), CACHE_LINE),
        .visual = muiLayoutAdd(layout, limits->nodes, sizeof(muiVisualStyle), CACHE_LINE),
        .text = muiLayoutAdd(layout, limits->nodes, sizeof(muiTextStyle), CACHE_LINE),
        .textRecords = muiLayoutAdd(layout, limits->nodes, sizeof(muiTextRecord), CACHE_LINE),
        .interaction = muiLayoutAdd(layout, limits->nodes, sizeof(muiInteractionStyle), CACHE_LINE),
        .nodeStyles = muiLayoutAdd(layout, limits->nodes, sizeof(muiNodeStyle), CACHE_LINE),
        .classSlots = muiLayoutAdd(layout, limits->styles, sizeof(muiPoolSlot), CACHE_LINE),
        .classes = muiLayoutAdd(layout, limits->styles, sizeof(muiStyleClass), CACHE_LINE),
        .typeSlots = muiLayoutAdd(layout, limits->nodeTypes, sizeof(muiPoolSlot), CACHE_LINE),
        .types = muiLayoutAdd(layout, limits->nodeTypes, sizeof(muiClassList), CACHE_LINE),
        .setSlots = muiLayoutAdd(layout, limits->propertySets, sizeof(muiPoolSlot), CACHE_LINE),
        .sets = muiLayoutAdd(layout, limits->propertySets, sizeof(muiPropertySet), CACHE_LINE),
        .notifications =
            muiLayoutAdd(layout, limits->notifications, sizeof(muiNotification), CACHE_LINE),
        .specSlots = muiLayoutAdd(layout, limits->transitions, sizeof(muiPoolSlot), CACHE_LINE),
        .specs = muiLayoutAdd(layout, limits->transitions, sizeof(muiTransitionSpec), CACHE_LINE),
        .recordSlots = muiLayoutAdd(layout, limits->animations, sizeof(muiPoolSlot), CACHE_LINE),
        .records = muiLayoutAdd(layout, limits->animations, sizeof(muiAnimation), CACHE_LINE),
        .tokenSlots = muiLayoutAdd(layout, limits->tokens, sizeof(muiPoolSlot), CACHE_LINE),
        .tokens = muiLayoutAdd(layout, limits->tokens, sizeof(muiToken), CACHE_LINE),
        .nameSlots = muiLayoutAdd(layout, limits->tokenNames, sizeof(muiPoolSlot), CACHE_LINE),
        .names = muiLayoutAdd(layout, limits->tokenNames, sizeof(muiTokenName), CACHE_LINE),
        .themeSlots = muiLayoutAdd(layout, limits->themes, sizeof(muiPoolSlot), CACHE_LINE),
        .themeTables =
            muiLayoutAdd(layout, ThemeTableEntries(layout, limits), sizeof(uint32_t), CACHE_LINE),
        .overrideSlots =
            muiLayoutAdd(layout, limits->themeOverrides, sizeof(muiPoolSlot), CACHE_LINE),
        .overrides =
            muiLayoutAdd(layout, limits->themeOverrides, sizeof(muiThemeOverride), CACHE_LINE),
        .paintStates = muiLayoutAdd(layout, limits->nodes, sizeof(muiPaintState), CACHE_LINE),
        // Two lists: the last, and the one built from it. Clips and
        // gradients have the placeholder for none at 0.
        .drawCommands = muiLayoutAdd(layout, (size_t)limits->drawCommands * 2,
                                     sizeof(muiDrawCommand), CACHE_LINE),
        .drawClips = muiLayoutAdd(layout, ((size_t)limits->drawClips + 1) * 2, sizeof(muiDrawClip),
                                  CACHE_LINE),
        .drawGradients = muiLayoutAdd(layout, ((size_t)limits->drawGradients + 1) * 2,
                                      sizeof(muiDrawGradient), CACHE_LINE),
        .drawGlyphs =
            muiLayoutAdd(layout, (size_t)limits->drawGlyphs * 2, sizeof(muiGlyph), CACHE_LINE),
        .layers = muiLayoutAdd(layout, limits->layers, sizeof(muiLayerEntry), CACHE_LINE),
    };
}

// Points the context's stores at their parts of the block.
static void Place(muiContext* context, unsigned char* base, const Parts* parts,
                  const muiLimits* limits)
{
    muiTreeInit(&context->tree, (muiTreeNode*)(base + parts->nodes), limits->nodes);
    context->layout = (muiLayoutNode*)(base + parts->layout);
    context->visual = (muiVisualStyle*)(base + parts->visual);
    context->text = (muiTextStyle*)(base + parts->text);
    context->textRecords = (muiTextRecord*)(base + parts->textRecords);
    context->interaction = (muiInteractionStyle*)(base + parts->interaction);
    context->environment = muiDefaultEnvironment();
    muiStyleStore* style = &context->style;
    style->nodes = (muiNodeStyle*)(base + parts->nodeStyles);
    muiPoolInit(&style->classPool, (muiPoolSlot*)(base + parts->classSlots), limits->styles);
    style->classes = (muiStyleClass*)(base + parts->classes);
    muiPoolInit(&style->typePool, (muiPoolSlot*)(base + parts->typeSlots), limits->nodeTypes);
    style->types = (muiClassList*)(base + parts->types);
    muiPoolInit(&style->setPool, (muiPoolSlot*)(base + parts->setSlots), limits->propertySets);
    style->sets = (muiPropertySet*)(base + parts->sets);
    muiNotifyInit(&context->notifications, (muiNotification*)(base + parts->notifications),
                  limits->notifications);
    muiAnimationStore* animations = &context->animations;
    muiPoolInit(&animations->specPool, (muiPoolSlot*)(base + parts->specSlots),
                limits->transitions);
    animations->specs = (muiTransitionSpec*)(base + parts->specs);
    muiPoolInit(&animations->pool, (muiPoolSlot*)(base + parts->recordSlots), limits->animations);
    animations->records = (muiAnimation*)(base + parts->records);
    muiPoolInit(&context->tokens.pool, (muiPoolSlot*)(base + parts->tokenSlots), limits->tokens);
    context->tokens.tokens = (muiToken*)(base + parts->tokens);
    muiPoolInit(&style->namePool, (muiPoolSlot*)(base + parts->nameSlots), limits->tokenNames);
    style->names = (muiTokenName*)(base + parts->names);
    muiThemeStore* themes = &context->themes;
    muiPoolInit(&themes->pool, (muiPoolSlot*)(base + parts->themeSlots), limits->themes);
    themes->tables = (uint32_t*)(base + parts->themeTables);
    themes->tokenCapacity = limits->tokens;
    muiPoolInit(&themes->overridePool, (muiPoolSlot*)(base + parts->overrideSlots),
                limits->themeOverrides);
    themes->overrides = (muiThemeOverride*)(base + parts->overrides);
    muiDrawStore* draw = &context->draw;
    draw->states = (muiPaintState*)(base + parts->paintStates);
    draw->commandCapacity = limits->drawCommands;
    draw->clipCapacity = limits->drawClips + 1;
    draw->gradientCapacity = limits->drawGradients + 1;
    draw->glyphCapacity = limits->drawGlyphs;
    for (uint32_t i = 0; i < 2; i++)
    {
        draw->tables[i] = (muiDrawTables){
            .commands = (muiDrawCommand*)(base + parts->drawCommands) + i * draw->commandCapacity,
            .clips = (muiDrawClip*)(base + parts->drawClips) + i * draw->clipCapacity,
            .gradients =
                (muiDrawGradient*)(base + parts->drawGradients) + i * draw->gradientCapacity,
            .glyphs = (muiGlyph*)(base + parts->drawGlyphs) + i * draw->glyphCapacity,
            .clipCount = 1,
            .gradientCount = 1,
        };
    }
    draw->identity = (muiDrawTransform){1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    muiLayerInit(&context->layers, (muiLayerEntry*)(base + parts->layers), limits->layers);
}

muiResult muiCreateContext(const muiContextDef* def, muiContext** contextOut)
{
    if (contextOut != nullptr)
    {
        *contextOut = nullptr;
    }
    if (def == nullptr || contextOut == nullptr || def->cookie != CONTEXT_DEF_COOKIE ||
        !muiIsAllocatorValid(&def->allocator) || !AreLimitsValid(&def->limits))
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
    muiRelease(&allocator, context, context->blockSize, alignof(max_align_t));
}

uint64_t muiGetContextMisuse(const muiContext* context)
{
    return context != nullptr ? context->misuse : 0;
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
