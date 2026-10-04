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
    return id.generation < link->generation || link->phase == MNAV_LINK_FREE ? mnav_errorStale
                                                                             : mnav_success;
}

static mnavResult CheckDef(const mnavLinkDef* def)
{
    if (!FinitePoint(def->start) || !FinitePoint(def->end))
    {
        return mnav_errorInvalid;
    }
    bool radius = def->radius >= 0.0f && def->radius <= MNAV_MAX_LINK_RADIUS;
    bool cost = def->cost >= 0.0f && def->cost <= MNAV_MAX_LINK_COST;
    return radius && cost && def->kind < MNAV_LINK_KINDS ? mnav_success : mnav_errorRange;
}

mnavResult mnavStageLink(mnavNavmesh* navmesh, const mnavLinkDef* def, mnavLinkId* linkOut)
{
    if (navmesh == nullptr || def == nullptr || linkOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckDef(def);
    if (result != mnav_success)
    {
        return result;
    }
    if (navmesh->linksHeld == navmesh->def.limits.links)
    {
        return mnav_errorLimit;
    }
    int32_t s = 0;
    while (s < navmesh->linkSlots && (navmesh->links[s].phase != MNAV_LINK_FREE ||
                                      navmesh->links[s].generation == UINT32_MAX))
    {
        s += 1;
    }
    if (s == navmesh->linkSlots)
    {
        result = mnavReserve(&navmesh->memory, (void**)&navmesh->links, &navmesh->linkCapacity,
                             navmesh->linkSlots, navmesh->linkSlots + 1, sizeof(mnavOffLink),
                             alignof(mnavOffLink));
        if (result != mnav_success)
        {
            return result;
        }
        navmesh->links[navmesh->linkSlots++] = (mnavOffLink){0};
    }
    mnavOffLink* link = &navmesh->links[s];
    *link = (mnavOffLink){*def, link->generation + 1, MNAV_LINK_ADDING, true, {0}};
    navmesh->linksHeld += 1;
    navmesh->linksPending += 1;
    *linkOut = (mnavLinkId){(uint32_t)s + 1, link->generation};
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
    mnavOffLink* link = &navmesh->links[s];
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
    if (result == mnav_success && link->enabled != enabled)
    {
        link->enabled = enabled;
        navmesh->linksPending += 1;
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
    *stateOut = result == mnav_success ? navmesh->links[s].state : (mnavLinkState){0};
    return result;
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
    mnavRelease(&navmesh->memory, plan->keys, (size_t)plan->capacity, sizeof(uint64_t),
                alignof(uint64_t));
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
    navmesh->attachments = plan->keys;
    navmesh->attachmentCapacity = plan->capacity;
    plan->keys = nullptr;
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
}

int32_t mnavAttachmentsFrom(const mnavNavmesh* navmesh, int32_t slot, int32_t polygon,
                            int32_t* first)
{
    uint64_t low = mnavAttachmentKey((mnavAttachment){slot, polygon, 0, false});
    int32_t lo = 0;
    int32_t hi = navmesh->attachmentCount;
    while (lo < hi)
    {
        int32_t middle = lo + (hi - lo) / 2;
        if (navmesh->attachments[middle] < low)
        {
            lo = middle + 1;
        }
        else
        {
            hi = middle;
        }
    }
    int32_t end = lo;
    while (end < navmesh->attachmentCount)
    {
        mnavAttachment a = mnavAttachmentOf(navmesh->attachments[end]);
        if (a.slot != slot || a.polygon != polygon)
        {
            break;
        }
        end += 1;
    }
    *first = lo;
    return end - lo;
}
