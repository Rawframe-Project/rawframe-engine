// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch panning: a touch's or a pen's drag of a scroll container moves
// it, past a limit by the rubber band when the rule allows overscroll,
// and its release flings it or springs its overscroll back.

#include "context.h"
#include "layout_node.h"
#include "scroll.h"
#include "scroll_store.h"
#include "tree.h"

#include "maul-tween/spring.h"

#include <math.h>

// The pan of a pointer, which drags one node at a time; the end of the
// table for none.
static uint32_t PanOf(const muiScrollStore* store, uint32_t pointer)
{
    uint32_t i = 0;
    while (i < store->panCount && store->pans[i].pointer != pointer)
    {
        i++;
    }
    return i;
}

// Takes out the pans of nodes destroyed since, whose drags end unseen.
static void PurgePans(muiContext* context)
{
    muiScrollStore* store = &context->scrolling;
    for (uint32_t i = store->panCount; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->pans[i - 1].node) == 0)
        {
            store->pans[i - 1] = store->pans[--store->panCount];
        }
    }
}

// iOS's rubber band, Maul Tween's: how far past a limit the children move
// for a pan past it by past in a scrollport of port; and the pan past it
// that moved them by over, its inverse, 0 from the scrollport on. A
// scrollport of no size, which Maul Tween refuses, bands nothing.
#define MUI_RUBBER_BAND 0.55

static float Band(float past, float port)
{
    double offset = 0.0;
    return mtwnApplyRubberBand((double)past, (double)port, MUI_RUBBER_BAND, &offset) == mtwn_success
               ? (float)offset
               : 0.0f;
}

static float Unband(float over, float port)
{
    double drag = 0.0;
    return over < port && mtwnInvertRubberBand((double)over, (double)port, MUI_RUBBER_BAND,
                                               &drag) == mtwn_success
               ? (float)drag
               : 0.0f;
}

// No pan shows an overscroll of the scrollport or more, where the band
// tends. A pan catching one there takes, past that limit and for as long
// as it lasts, a band twice as wide as the overscroll it caught, and goes
// on from where it is. Twice is the library's choice, not a platform's.
#define MUI_CAUGHT_BAND 2.0f

// The band a pan catching overscroll over takes past its limit, signed as
// over is; 0 below the scrollport, for the scrollport's own.
static float CaughtBand(float over, float port)
{
    return fabsf(over) < port ? 0.0f : MUI_CAUGHT_BAND * over;
}

// The band past a limit: the one a pan caught there, else the scrollport's.
static float BandPast(float caught, float port)
{
    return caught != 0.0f ? fabsf(caught) : port;
}

// Where a pan along one axis puts the offset, within 0 and limit, and
// the overscroll past either, signed, when the rule allows it.
static float Along(const muiContext* context, float wanted, float limit, float port, float caught,
                   bool axis, float* overOut)
{
    float offset = fminf(fmaxf(wanted, 0.0f), limit);
    *overOut = 0.0f;
    if (context->scrolling.rule.overscroll && axis && wanted != offset)
    {
        *overOut = wanted < 0.0f ? -Band(-wanted, BandPast(fminf(caught, 0.0f), port))
                                 : Band(wanted - limit, BandPast(fmaxf(caught, 0.0f), port));
    }
    return offset;
}

// The scrollport of a node, its padding box.
static muiSize PortOf(const muiLayoutNode* layout)
{
    const muiEdges* border = &layout->style.border;
    return (muiSize){fmaxf(layout->rect.width - border->start - border->end, 0.0f),
                     fmaxf(layout->rect.height - border->top - border->bottom, 0.0f)};
}

// Moves a panned container's offsets opposite the pointer's offset from
// the drag's start, logical under right to left, within its limits; past
// them, its overscroll when the rule allows it.
static void Pan(muiContext* context, uint32_t slot, const muiScrollPan* pan,
                const muiPointerRecord* record)
{
    const muiLayoutNode* layout = &context->layout[slot - 1];
    muiScrollState* scroll = &context->scrolls[slot - 1];
    const muiSize size = {layout->rect.width, layout->rect.height};
    const muiSize port = PortOf(layout);
    muiScrollAxes axes = layout->style.scrollAxes;
    float across = scroll->right ? record->offsetX : -record->offsetX;
    float down = scroll->bottom ? record->offsetY : -record->offsetY;
    float overX = 0.0f;
    float overY = 0.0f;
    float x =
        Along(context, pan->startX + across, muiScrollLimit(&layout->style, size, scroll, true),
              port.width, pan->caughtX, (axes & mui_scrollHorizontal) != 0, &overX);
    float y =
        Along(context, pan->startY + down, muiScrollLimit(&layout->style, size, scroll, false),
              port.height, pan->caughtY, (axes & mui_scrollVertical) != 0, &overY);
    if (x != scroll->x || y != scroll->y || overX != scroll->overX || overY != scroll->overY)
    {
        scroll->x = x;
        scroll->y = y;
        scroll->overX = overX;
        scroll->overY = overY;
        muiNoteScrolled(context, slot);
    }
}

// The flings' window of moves, and their least and greatest speeds, in
// units a second.
#define MUI_FLING_WINDOW_NS 100000000ull
#define MUI_FLING_MIN       50.0
#define MUI_FLING_MAX       8000.0

// The slope of a least squares line through a pan's moves of the last
// 100 ms, per axis, in units a second; 0 with fewer than two.
static void VelocityOf(const muiScrollPan* pan, double* xOut, double* yOut)
{
    uint32_t count = pan->count < MUI_SCROLL_SAMPLES ? pan->count : MUI_SCROLL_SAMPLES;
    const muiScrollSample* last = &pan->samples[(pan->count - 1) % MUI_SCROLL_SAMPLES];
    double sumT = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    uint32_t used = 0;
    for (uint32_t k = 0; k < count; k++)
    {
        const muiScrollSample* sample = &pan->samples[(pan->count - 1 - k) % MUI_SCROLL_SAMPLES];
        if (last->timeNs - sample->timeNs > MUI_FLING_WINDOW_NS)
        {
            break;
        }
        // Seconds before the last.
        sumT -= (double)(last->timeNs - sample->timeNs) / 1e9;
        sumX += (double)sample->x;
        sumY += (double)sample->y;
        used++;
    }
    // One move, or moves at one time, give no spread: no velocity.
    *xOut = 0.0;
    *yOut = 0.0;
    double meanT = sumT / used;
    double meanX = sumX / used;
    double meanY = sumY / used;
    double spread = 0.0;
    double alongX = 0.0;
    double alongY = 0.0;
    for (uint32_t k = 0; k < used; k++)
    {
        const muiScrollSample* sample = &pan->samples[(pan->count - 1 - k) % MUI_SCROLL_SAMPLES];
        double t = -(double)(last->timeNs - sample->timeNs) / 1e9 - meanT;
        spread += t * t;
        alongX += t * ((double)sample->x - meanX);
        alongY += t * ((double)sample->y - meanY);
    }
    if (spread > 0.0)
    {
        *xOut = alongX / spread;
        *yOut = alongY / spread;
    }
}

// Starts a fling of a panned container at the pan's velocity, the
// offsets' opposite the pointer's; none below the least speed, under
// reduced motion, or with the ease table full.
static void StartFling(muiContext* context, uint32_t slot, const muiScrollPan* pan, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    double vx = 0.0;
    double vy = 0.0;
    VelocityOf(pan, &vx, &vy);
    double speed = sqrt(vx * vx + vy * vy);
    if (speed < MUI_FLING_MIN || context->environment.reducedMotion ||
        store->easeCount == MUI_SCROLL_EASES)
    {
        return;
    }
    double scale = speed > MUI_FLING_MAX ? MUI_FLING_MAX / speed : 1.0;
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    store->eases[store->easeCount++] = (muiScrollEase){
        .node = muiTreeIdOf(&context->tree, slot),
        .fromX = scroll->x,
        .fromY = scroll->y,
        .startNs = timeNs,
        .kind = muiScrollEaseFling,
        .velocityX = (float)((scroll->right ? vx : -vx) * scale),
        .velocityY = (float)((scroll->bottom ? vy : -vy) * scale),
    };
}

// Springs a released overscroll back to none; at once under reduced
// motion or with the ease table full.
static void StartBounce(muiContext* context, uint32_t slot, uint64_t timeNs)
{
    muiScrollStore* store = &context->scrolling;
    const muiScrollState* scroll = &context->scrolls[slot - 1];
    if (context->environment.reducedMotion || store->easeCount == MUI_SCROLL_EASES)
    {
        muiScrollCancel(context, slot);
        return;
    }
    store->eases[store->easeCount++] = (muiScrollEase){
        .node = muiTreeIdOf(&context->tree, slot),
        .fromX = scroll->overX,
        .fromY = scroll->overY,
        .startNs = timeNs,
        .kind = muiScrollEaseBounce,
    };
}

bool muiScrollPointer(muiContext* context, const muiPointerRecord* record)
{
    muiScrollStore* store = &context->scrolling;
    uint32_t node = muiTreeResolve(&context->tree, record->node);
    if (node == 0)
    {
        return false;
    }
    bool drag = record->kind == mui_pointerRecordDragStart ||
                record->kind == mui_pointerRecordDragMove ||
                record->kind == mui_pointerRecordDragEnd;
    if (!drag || record->pointerKind == mui_pointerMouse ||
        context->layout[node - 1].style.scrollAxes == mui_scrollNone)
    {
        return false;
    }
    uint32_t i = PanOf(store, record->pointer);
    if (record->kind == mui_pointerRecordDragStart)
    {
        PurgePans(context);
        i = PanOf(store, record->pointer);
        if (i == store->panCount)
        {
            if (store->panCount == MUI_SCROLL_PANS)
            {
                return false;
            }
            store->panCount++;
        }
        // A pan caught past a limit goes on from the pan that put it there.
        const muiScrollState* scroll = &context->scrolls[node - 1];
        const muiSize port = PortOf(&context->layout[node - 1]);
        float caughtX = CaughtBand(scroll->overX, port.width);
        float caughtY = CaughtBand(scroll->overY, port.height);
        float startX =
            scroll->x +
            copysignf(Unband(fabsf(scroll->overX), BandPast(caughtX, port.width)), scroll->overX);
        float startY =
            scroll->y +
            copysignf(Unband(fabsf(scroll->overY), BandPast(caughtY, port.height)), scroll->overY);
        muiScrollCancel(context, node);
        store->pans[i] = (muiScrollPan){.pointer = record->pointer,
                                        .node = record->node,
                                        .startX = startX,
                                        .startY = startY,
                                        .caughtX = caughtX,
                                        .caughtY = caughtY};
    }
    if (i == store->panCount)
    {
        return false;
    }
    muiScrollPan* pan = &store->pans[i];
    pan->samples[pan->count++ % MUI_SCROLL_SAMPLES] =
        (muiScrollSample){record->timeNs, record->offsetX, record->offsetY};
    Pan(context, node, pan, record);
    if (record->kind == mui_pointerRecordDragEnd)
    {
        const muiScrollState* scroll = &context->scrolls[node - 1];
        if (scroll->overX != 0.0f || scroll->overY != 0.0f)
        {
            StartBounce(context, node, record->timeNs);
        }
        else if (!record->cancelled)
        {
            StartFling(context, node, pan, record->timeNs);
        }
        store->pans[i] = store->pans[--store->panCount];
    }
    return true;
}
