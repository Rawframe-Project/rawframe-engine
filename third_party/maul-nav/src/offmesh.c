// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Off-mesh links (mnav-0004): staging them, and attaching them to polygons at
// each commit.

#include "offmesh.h"

#include "allocator.h"
#include "navmesh.h"
#include "nearest.h"
#include "sort.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>

// The key's fields, low to high: direction, link, polygon, slot.
#define LINK_BITS    20
#define POLYGON_BITS 16

uint64_t mnavAttachmentKey(mnavAttachment a)
{
    return (uint64_t)(uint32_t)a.slot << (1 + LINK_BITS + POLYGON_BITS) |
           (uint64_t)(uint32_t)a.polygon << (1 + LINK_BITS) | (uint64_t)(uint32_t)a.link << 1 |
           (uint64_t)a.reverse;
}

mnavAttachment mnavAttachmentOf(uint64_t key)
{
    return (mnavAttachment){(int32_t)(key >> (1 + LINK_BITS + POLYGON_BITS)),
                            (int32_t)(key >> (1 + LINK_BITS) & ((1u << POLYGON_BITS) - 1)),
                            (int32_t)(key >> 1 & ((1u << LINK_BITS) - 1)), (key & 1u) != 0};
}

static bool FinitePoint(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

// The slot an id names, or the reason it names none; links in their last
// generation are never reused, so generations do not wrap.
static mnavResult Find(const mnavNavmesh* navmesh, mnavLinkId id, int32_t* slotOut)
{
    if (id.slot == 0 || id.slot > (uint32_t)navmesh->linkSlots || id.generation == 0)
    {
        return mnav_errorInvalid;
    }
    const mnavOffLink* link = &navmesh->links[id.slot - 1];
    if (id.generation > link->generation)
    {
        return mnav_errorInvalid;
    }
    *slotOut = (int32_t)id.slot - 1;
    if (id.generation < link->generation || link->phase == MNAV_LINK_FREE)
    {
        return mnav_errorStale;
    }
    // An edge link's later crossings have slots no id names.
    return link->parent == *slotOut ? mnav_success : mnav_errorInvalid;
}

// Whether a point lies within the extent bake input may have round the
// navmesh's origin, so that the arithmetic on links stays finite.
static bool InExtent(const mnavNavmesh* navmesh, mnavPos3 p)
{
    const mnavBakeDef* def = &navmesh->def;
    double ground = (double)def->cellSize * (double)MNAV_MAX_EXTENT_CELLS;
    double vertical = (double)def->cellHeight * (double)MNAV_MAX_HEIGHT_CELLS;
    return fabs(p.x - def->origin.x) <= ground && fabs(p.z - def->origin.z) <= ground &&
           fabs(p.y - def->origin.y) <= vertical;
}

static mnavResult CheckDef(const mnavNavmesh* navmesh, const mnavLinkDef* def)
{
    if (!FinitePoint(def->start) || !FinitePoint(def->end))
    {
        return mnav_errorInvalid;
    }
    if (!InExtent(navmesh, def->start) || !InExtent(navmesh, def->end))
    {
        return mnav_errorRange;
    }
    bool radius = def->radius >= 0.0f && def->radius <= MNAV_MAX_LINK_RADIUS;
    bool cost = def->cost >= 0.0f && def->cost <= MNAV_MAX_LINK_COST;
    bool width = def->width >= 0.0f && def->width <= MNAV_MAX_LINK_WIDTH;
    if (!(radius && cost && width && def->kind < MNAV_LINK_KINDS))
    {
        return mnav_errorRange;
    }
    // An edge link needs a direction on the ground: ends so near that the
    // square of their distance rounds to 0 give none.
    double gx = def->end.x - def->start.x;
    double gz = def->end.z - def->start.z;
    bool flat = gx * gx + gz * gz == 0.0;
    return def->width > 0.0f && flat ? mnav_errorInvalid : mnav_success;
}

// How many points a link is crossed at: one for a point link; for an edge
// link enough that they lie no farther apart than the agent's radius, a
// cell when the agent has none, at least two and at most
// MNAV_MAX_LINK_CROSSINGS.
static int32_t Crossings(const mnavNavmesh* navmesh, const mnavLinkDef* def)
{
    if (def->width == 0.0f)
    {
        return 1;
    }
    double spacing = navmesh->def.agent.radius > 0.0f ? (double)navmesh->def.agent.radius
                                                      : (double)navmesh->def.cellSize;
    double gaps = ceil((double)def->width / spacing);
    return gaps >= (double)(MNAV_MAX_LINK_CROSSINGS - 1) ? MNAV_MAX_LINK_CROSSINGS
                                                         : 1 + (int32_t)gaps;
}

// Crossing k of n: the link moved across its ground direction, from one
// side of its width to the other, and made a point link.
static mnavLinkDef Crossing(const mnavLinkDef* def, int32_t k, int32_t n)
{
    mnavLinkDef c = *def;
    c.width = 0.0f;
    if (n == 1)
    {
        return c;
    }
    double gx = def->end.x - def->start.x;
    double gz = def->end.z - def->start.z;
    double ground = sqrt(gx * gx + gz * gz);
    double along = (double)def->width * ((double)k / (double)(n - 1) - 0.5);
    double ox = -gz / ground * along;
    double oz = gx / ground * along;
    c.start.x += ox;
    c.start.z += oz;
    c.end.x += ox;
    c.end.z += oz;
    return c;
}

// The next free slot from s on that may take another generation, or
// linkSlots.
static int32_t NextFree(const mnavNavmesh* navmesh, int32_t s)
{
    while (s < navmesh->linkSlots && (navmesh->links[s].phase != MNAV_LINK_FREE ||
                                      navmesh->links[s].generation == UINT32_MAX))
    {
        s += 1;
    }
    return s;
}

mnavResult mnavStageLink(mnavNavmesh* navmesh, const mnavLinkDef* def, mnavLinkId* linkOut)
{
    if (navmesh == nullptr || def == nullptr || linkOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckDef(navmesh, def);
    if (result != mnav_success)
    {
        return result;
    }
    int32_t n = Crossings(navmesh, def);
    if (navmesh->linksHeld + n > navmesh->def.limits.links)
    {
        return mnav_errorLimit;
    }
    // Room for every crossing first, so that nothing fails half staged.
    result = mnavReserve(&navmesh->memory, (void**)&navmesh->links, &navmesh->linkCapacity,
                         navmesh->linkSlots, navmesh->linkSlots + n, sizeof(mnavOffLink),
                         alignof(mnavOffLink));
    if (result != mnav_success)
    {
        return result;
    }
    int32_t first = -1;
    int32_t s = -1;
    for (int32_t k = 0; k < n; ++k)
    {
        s = NextFree(navmesh, s + 1);
        if (s == navmesh->linkSlots)
        {
            navmesh->links[navmesh->linkSlots++] = (mnavOffLink){0};
        }
        first = k == 0 ? s : first;
        mnavOffLink* link = &navmesh->links[s];
        *link = (mnavOffLink){
            Crossing(def, k, n), link->generation + 1, MNAV_LINK_ADDING, true, {0}, first, k, n};
    }
    navmesh->linksHeld += n;
    navmesh->linksPending += n;
    *linkOut = (mnavLinkId){(uint32_t)first + 1, navmesh->links[first].generation};
    return mnav_success;
}

mnavResult mnavStageLinkRemoval(mnavNavmesh* navmesh, mnavLinkId id)
{
    if (navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    int32_t s = -1;
    mnavResult result = Find(navmesh, id, &s);
    if (result != mnav_success)
    {
        return result;
    }
    for (int32_t c = s; c < navmesh->linkSlots; ++c)
    {
        mnavOffLink* link = &navmesh->links[c];
        if (link->parent != s || link->phase == MNAV_LINK_FREE)
        {
            continue;
        }
        if (link->phase == MNAV_LINK_ADDING)
        {
            link->phase = MNAV_LINK_FREE;
            navmesh->linksHeld -= 1;
            navmesh->linksPending -= 1;
        }
        else if (link->phase == MNAV_LINK_LIVE)
        {
            link->phase = MNAV_LINK_REMOVING;
            navmesh->linksPending += 1;
        }
    }
    return mnav_success;
}

mnavResult mnavStageLinkEnabled(mnavNavmesh* navmesh, mnavLinkId id, bool enabled)
{
    if (navmesh == nullptr)
    {
        return mnav_errorInvalid;
    }
    int32_t s = 0;
    mnavResult result = Find(navmesh, id, &s);
    mnavOffLink* link = result == mnav_success ? &navmesh->links[s] : nullptr;
    if (link != nullptr && link->phase == MNAV_LINK_REMOVING)
    {
        result = mnav_errorStale;
    }
    if (result == mnav_success && navmesh->def.tier < mnav_tierModifiers)
    {
        result = mnav_errorTier;
    }
    for (int32_t c = s; result == mnav_success && c < navmesh->linkSlots; ++c)
    {
        mnavOffLink* crossing = &navmesh->links[c];
        if (crossing->parent == s && crossing->phase != MNAV_LINK_FREE &&
            crossing->enabled != enabled)
        {
            crossing->enabled = enabled;
            navmesh->linksPending += 1;
        }
    }
    return result;
}

mnavResult mnavGetLink(const mnavNavmesh* navmesh, mnavLinkId id, mnavLinkState* stateOut)
{
    if (navmesh == nullptr || stateOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    int32_t s = -1;
    mnavResult result = Find(navmesh, id, &s);
    if (result == mnav_success && navmesh->links[s].phase == MNAV_LINK_ADDING)
    {
        result = mnav_errorNotLoaded;
    }
    *stateOut = (mnavLinkState){0};
    if (result != mnav_success)
    {
        return result;
    }
    // The first attached crossing along the width, and how many are.
    int32_t best = -1;
    int32_t attached = 0;
    for (int32_t c = s; c < navmesh->linkSlots; ++c)
    {
        const mnavOffLink* crossing = &navmesh->links[c];
        if (crossing->parent != s || crossing->phase == MNAV_LINK_FREE || !crossing->state.attached)
        {
            continue;
        }
        attached += 1;
        best = best < 0 || crossing->crossing < navmesh->links[best].crossing ? c : best;
    }
    *stateOut = best >= 0 ? navmesh->links[best].state : (mnavLinkState){0};
    stateOut->enabled = navmesh->links[s].state.enabled;
    stateOut->crossings = attached;
    return mnav_success;
}

// The half sizes of the box a link end snaps within: its radius on the
// ground and the agent's step in height.
static mnavPos3 SnapHalf(const mnavNavmesh* navmesh, float radius)
{
    double step =
        (double)(float)((double)navmesh->cells.agentStep * (double)navmesh->def.cellHeight);
    return (mnavPos3){(double)radius, step, (double)radius};
}

static bool Covers(mnavCover c, int32_t x, int32_t z)
{
    return x >= c.x0 && x <= c.x1 && z >= c.z0 && z <= c.z1;
}

// Whether the snap box round a point covers a place the commit changes.
static bool Changed(const mnavNavmesh* navmesh, mnavPos3 p, float radius)
{
    mnavCover c = mnavCoverOf(navmesh, p, SnapHalf(navmesh, radius));
    for (int32_t s = 0; s < navmesh->stagedCount; ++s)
    {
        if (Covers(c, navmesh->staged[s].x, navmesh->staged[s].z))
        {
            return true;
        }
    }
    for (int32_t a = 0; a < navmesh->areaChangeCount; ++a)
    {
        const mnavSlot* slot = &navmesh->slots[navmesh->areaChanges[a].polygon.slot - 1];
        if (Covers(c, slot->x, slot->z))
        {
            return true;
        }
    }
    return false;
}

mnavResult mnavPlanAttachments(mnavNavmesh* navmesh, mnavAttachmentPlan* plan)
{
    *plan = (mnavAttachmentPlan){0};
    int32_t kept = 0;
    for (int32_t s = 0; s < navmesh->linkSlots; ++s)
    {
        mnavLinkPhase phase = navmesh->links[s].phase;
        kept += phase == MNAV_LINK_ADDING || phase == MNAV_LINK_LIVE ? 1 : 0;
    }
    if (kept == 0)
    {
        return mnav_success;
    }
    // Two directions per link, and as much again to sort with.
    int32_t capacity = 4 * kept;
    mnavResult result = mnavAllocate(&navmesh->memory, (size_t)capacity, sizeof(uint64_t),
                                     alignof(uint64_t), (void**)&plan->keys);
    if (result == mnav_success)
    {
        plan->capacity = capacity;
        result = mnavAllocate(&navmesh->memory, (size_t)capacity, sizeof(uint64_t),
                              alignof(uint64_t), (void**)&plan->arrivals);
    }
    if (result == mnav_success)
    {
        result = mnavAllocate(&navmesh->memory, (size_t)navmesh->linkSlots, sizeof(bool),
                              alignof(bool), (void**)&plan->resnap);
    }
    if (result != mnav_success)
    {
        mnavDropAttachmentPlan(navmesh, plan);
        return result;
    }
    plan->resnapCount = navmesh->linkSlots;
    for (int32_t s = 0; s < navmesh->linkSlots; ++s)
    {
        const mnavOffLink* link = &navmesh->links[s];
        plan->resnap[s] = link->phase == MNAV_LINK_ADDING ||
                          (link->phase == MNAV_LINK_LIVE &&
                           (Changed(navmesh, link->def.start, link->def.radius) ||
                            Changed(navmesh, link->def.end, link->def.radius)));
    }
    return mnav_success;
}

void mnavDropAttachmentPlan(mnavNavmesh* navmesh, mnavAttachmentPlan* plan)
{
    size_t capacity = (size_t)plan->capacity;
    mnavRelease(&navmesh->memory, plan->keys, capacity, sizeof(uint64_t), alignof(uint64_t));
    mnavRelease(&navmesh->memory, plan->arrivals, capacity, sizeof(uint64_t), alignof(uint64_t));
    mnavRelease(&navmesh->memory, plan->resnap, (size_t)plan->resnapCount, sizeof(bool),
                alignof(bool));
    *plan = (mnavAttachmentPlan){0};
}

// Snaps an end to the nearest polygon within the radius on the ground and
// the agent's step in height.
static bool Snap(const mnavNavmesh* navmesh, mnavPos3 p, float radius, mnavPolygonId* polygon,
                 mnavPos3* at)
{
    mnavPos3 half = SnapHalf(navmesh, radius);
    mnavNearest n;
    if (mnavFindNearest(navmesh, nullptr, p, (mnavVec3){radius, (float)half.y, radius}, &n) !=
            mnav_success ||
        n.polygon.slot == 0)
    {
        return false;
    }
    double dx = n.point.x - p.x;
    double dz = n.point.z - p.z;
    *polygon = n.polygon;
    *at = n.point;
    return dx * dx + dz * dz <= (double)radius * (double)radius;
}

// Snaps a committed link's ends again.
static void Resnap(mnavNavmesh* navmesh, mnavOffLink* link)
{
    mnavLinkState state = {0};
    state.attached =
        Snap(navmesh, link->def.start, link->def.radius, &state.startPolygon, &state.start) &&
        Snap(navmesh, link->def.end, link->def.radius, &state.endPolygon, &state.end);
    state.crossings = state.attached ? 1 : 0;
    link->state = state.attached ? state : (mnavLinkState){0};
}

// Writes a committed link's attachments, when attached and enabled.
static int32_t Attach(mnavNavmesh* navmesh, int32_t s, uint64_t* keys, int32_t count)
{
    mnavOffLink* link = &navmesh->links[s];
    const mnavLinkState* state = &link->state;
    link->state.enabled = link->enabled;
    if (!state->attached || !link->enabled)
    {
        return count;
    }
    keys[count++] = mnavAttachmentKey((mnavAttachment){
        (int32_t)state->startPolygon.slot - 1, (int32_t)state->startPolygon.polygon, s, false});
    if (link->def.twoWay)
    {
        keys[count++] = mnavAttachmentKey((mnavAttachment){
            (int32_t)state->endPolygon.slot - 1, (int32_t)state->endPolygon.polygon, s, true});
    }
    return count;
}

// Writes each attachment again, by the polygon it lands on, and sorts
// them with the scratch from half on.
static void Arrive(mnavNavmesh* navmesh, int32_t half)
{
    for (int32_t i = 0; i < navmesh->attachmentCount; ++i)
    {
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavLinkState* state = &navmesh->links[a.link].state;
        mnavPolygonId landing = a.reverse ? state->startPolygon : state->endPolygon;
        navmesh->arrivals[i] = mnavAttachmentKey((mnavAttachment){
            (int32_t)landing.slot - 1, (int32_t)landing.polygon, a.link, a.reverse});
    }
    navmesh->arrivalCount =
        navmesh->attachmentCount > 0
            ? (int32_t)mnavSortUnique(navmesh->arrivals, navmesh->arrivals + half,
                                      (size_t)navmesh->attachmentCount)
            : 0;
}

void mnavApplyAttachments(mnavNavmesh* navmesh, mnavAttachmentPlan* plan)
{
    int32_t held = 0;
    for (int32_t s = 0; s < navmesh->linkSlots; ++s)
    {
        mnavOffLink* link = &navmesh->links[s];
        link->phase = link->phase == MNAV_LINK_ADDING ? MNAV_LINK_LIVE : link->phase;
        if (link->phase == MNAV_LINK_REMOVING)
        {
            link->phase = MNAV_LINK_FREE;
            link->state = (mnavLinkState){0};
        }
        held += link->phase == MNAV_LINK_LIVE ? 1 : 0;
    }
    navmesh->linksHeld = held;
    navmesh->linksPending = 0;
    mnavRelease(&navmesh->memory, navmesh->attachments, (size_t)navmesh->attachmentCapacity,
                sizeof(uint64_t), alignof(uint64_t));
    mnavRelease(&navmesh->memory, navmesh->arrivals, (size_t)navmesh->arrivalCapacity,
                sizeof(uint64_t), alignof(uint64_t));
    navmesh->attachments = plan->keys;
    navmesh->attachmentCapacity = plan->capacity;
    navmesh->arrivals = plan->arrivals;
    navmesh->arrivalCapacity = plan->capacity;
    plan->keys = nullptr;
    plan->arrivals = nullptr;
    plan->capacity = 0;
    int32_t count = 0;
    for (int32_t s = 0; s < navmesh->linkSlots; ++s)
    {
        mnavOffLink* link = &navmesh->links[s];
        if (link->phase == MNAV_LINK_LIVE)
        {
            if (plan->resnap[s])
            {
                Resnap(navmesh, link);
            }
            count = Attach(navmesh, s, navmesh->attachments, count);
        }
    }
    mnavDropAttachmentPlan(navmesh, plan);
    for (int32_t k = 0; k < MNAV_LINK_KINDS; ++k)
    {
        navmesh->costPerMeter[k] = (double)INFINITY;
    }
    for (int32_t s = 0; s < navmesh->linkSlots; ++s)
    {
        const mnavOffLink* link = &navmesh->links[s];
        double dx = link->state.end.x - link->state.start.x;
        double dy = link->state.end.y - link->state.start.y;
        double dz = link->state.end.z - link->state.start.z;
        double span = sqrt(dx * dx + dy * dy + dz * dz);
        if (link->state.attached && link->state.enabled && span > 0.0)
        {
            double perMeter = (double)link->def.cost / span;
            double* lowest = &navmesh->costPerMeter[link->def.kind];
            *lowest = perMeter < *lowest ? perMeter : *lowest;
        }
    }
    // The second half of the memory is the sort's scratch.
    int32_t half = navmesh->attachmentCapacity / 2;
    navmesh->attachmentCount =
        count > 0 ? (int32_t)mnavSortUnique(navmesh->attachments, navmesh->attachments + half,
                                            (size_t)count)
                  : 0;
    Arrive(navmesh, half);
}

// The keys of one polygon in a sorted key array: first receives the index
// of the first; returns how many.
static int32_t KeysOf(const uint64_t* keys, int32_t count, int32_t slot, int32_t polygon,
                      int32_t* first)
{
    uint64_t low = mnavAttachmentKey((mnavAttachment){slot, polygon, 0, false});
    int32_t lo = 0;
    int32_t hi = count;
    while (lo < hi)
    {
        int32_t middle = lo + (hi - lo) / 2;
        if (keys[middle] < low)
        {
            lo = middle + 1;
        }
        else
        {
            hi = middle;
        }
    }
    int32_t end = lo;
    while (end < count)
    {
        mnavAttachment a = mnavAttachmentOf(keys[end]);
        if (a.slot != slot || a.polygon != polygon)
        {
            break;
        }
        end += 1;
    }
    *first = lo;
    return end - lo;
}

int32_t mnavAttachmentsFrom(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon,
                            int32_t* first)
{
    return KeysOf(navmesh->attachments, navmesh->attachmentCount, slot, polygon, first);
}

int32_t mnavAttachmentsTo(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon, int32_t* first)
{
    return KeysOf(navmesh->arrivals, navmesh->arrivalCount, slot, polygon, first);
}
