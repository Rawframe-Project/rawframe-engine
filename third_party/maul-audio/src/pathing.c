// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The step's pathing (pathing.h). Each job's path is kept in its own
// row of vertices, so the refinement's tasks share nothing.

#include "pathing.h"

#include "allocator.h"
#include "direct_step.h"

#include <math.h>

// Searches a source gets at most.
#define SEARCHES 3

struct maudPathing
{
    maudAllocator allocator;
    uint32_t maxPaths;
    maudPathSearch search;
    maudDiffraction diffraction;
    maudPathJob* jobs;
    maudVector3* vertices;
    uint32_t* counts;
    // A step's: the jobs, the query, the listener.
    uint32_t jobCount;
    const maudProbeQueries* queries;
    const maudPose* listener;
};

static size_t Bytes(uint32_t maxPaths)
{
    return sizeof(maudPathing) +
           (size_t)maxPaths *
               (sizeof(maudPathJob) + MAUD_PATH_VERTICES * sizeof(maudVector3) + sizeof(uint32_t));
}

maudPathing* maudCreatePathing(const maudAllocator* allocator, uint32_t maxProbes,
                               uint32_t maxPaths)
{
    maudPathing* p = maudAllocate(allocator, Bytes(maxPaths), alignof(maudPathing));
    if (p == nullptr)
    {
        return nullptr;
    }
    *p = (maudPathing){.allocator = *allocator, .maxPaths = maxPaths};
    unsigned char* after = (unsigned char*)(p + 1);
    p->jobs = (maudPathJob*)after;
    p->vertices = (maudVector3*)(after + (size_t)maxPaths * sizeof(maudPathJob));
    p->counts = (uint32_t*)((unsigned char*)p->vertices +
                            (size_t)maxPaths * MAUD_PATH_VERTICES * sizeof(maudVector3));
    if (!maudCreatePathSearch(allocator, maxProbes, &p->search))
    {
        maudRelease(allocator, p, Bytes(maxPaths), alignof(maudPathing));
        return nullptr;
    }
    maudSetupDiffraction(&p->diffraction);
    return p;
}

void maudDestroyPathing(maudPathing* pathing)
{
    if (pathing == nullptr)
    {
        return;
    }
    maudAllocator allocator = pathing->allocator;
    maudDestroyPathSearch(&allocator, &pathing->search);
    maudRelease(&allocator, pathing, Bytes(pathing->maxPaths), alignof(maudPathing));
}

maudPathJob* maudPathJobs(maudPathing* pathing)
{
    return pathing->jobs;
}

static float Distance(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

// Whether every link of a found path is clear; blocked ones are added
// to the search's list.
static bool Clear(maudPathing* p, const maudProbeGraph* g, const uint32_t* probes, uint32_t links)
{
    maudRay rays[MAUD_PATH_VERTICES];
    uint8_t occluded[MAUD_PATH_VERTICES] = {0};
    for (uint32_t k = 0; k < links; ++k)
    {
        maudVector3 a = g->points[probes[k]];
        maudVector3 b = g->points[probes[k + 1]];
        float d = Distance(a, b);
        rays[k] =
            (maudRay){a,
                      d > 0.0f ? (maudVector3){(b.x - a.x) / d, (b.y - a.y) / d, (b.z - a.z) / d}
                               : (maudVector3){0.0f, 1.0f, 0.0f},
                      0.0f, d};
    }
    if (links > 0 && p->queries->anyHit != nullptr)
    {
        p->queries->anyHit(rays, links, occluded, p->queries->rayContext);
    }
    bool clear = true;
    for (uint32_t k = 0; k < links; ++k)
    {
        if (occluded[k] != 0)
        {
            clear = false;
            maudPathSearch* s = &p->search;
            if (s->blockedCount < MAUD_PATH_BLOCKED)
            {
                uint32_t a = probes[k];
                uint32_t b = probes[k + 1];
                s->blocked[2 * s->blockedCount] = a < b ? a : b;
                s->blocked[2 * s->blockedCount + 1] = a < b ? b : a;
                s->blockedCount += 1;
            }
        }
    }
    return clear;
}

// A job's path: its vertex count, 0 for none.
static uint32_t Find(maudPathing* p, const maudProbeGraph* g, const maudPathEnd* listener,
                     uint32_t job)
{
    const maudProbeQueries* q = p->queries;
    maudVector3 source = p->jobs[job].pose->position;
    maudPathEnd end;
    maudAttachPath(g, g->range, source, q->anyHit, q->rayContext, &end);
    maudVector3* v = p->vertices + (size_t)job * MAUD_PATH_VERTICES;
    uint32_t probes[MAUD_PATH_VERTICES];
    p->search.blockedCount = 0;
    for (int attempt = 0; attempt < SEARCHES && end.count > 0; ++attempt)
    {
        uint32_t n =
            maudSearchPath(&p->search, g, &end, listener, source, p->listener->position, v, probes);
        if (n == 0)
        {
            return 0;
        }
        if (Clear(p, g, probes, n - 3))
        {
            return n;
        }
    }
    return 0;
}

static maudVector3 Toward(maudVector3 from, maudVector3 to)
{
    float d = Distance(from, to);
    return d > 0.0f ? (maudVector3){(to.x - from.x) / d, (to.y - from.y) / d, (to.z - from.z) / d}
                    : (maudVector3){0.0f, 0.0f, -1.0f};
}

// Feeds a refined path of count vertices into a job's result.
static void Feed(const maudPathing* p, const maudPathJob* job, const maudVector3* v, uint32_t count)
{
    maudDirectResult* r = job->result;
    float length = 0.0f;
    for (uint32_t k = 0; k + 1 < count; ++k)
    {
        length += Distance(v[k], v[k + 1]);
    }
    float around[MAUD_DIRECT_BANDS];
    maudPathDiffraction(&p->diffraction, v, count, around);
    float spread = r->distance > 0.0f ? length / r->distance : 1.0f;
    float o = r->occlusion;
    float through = r->transmission[1] * spread;
    float straight = (1.0f - o) + o * through;
    float ws = straight * straight;
    float wp = o * around[1] * o * around[1];
    float directivity[MAUD_DIRECT_BANDS];
    maudVector3 leaving = Toward(job->pose->position, v[count - 2]);
    if (maudGetDirectivity(job->pattern, maudUnrotate(job->pose->orientation, leaving),
                           directivity) != maud_success)
    {
        // Patterns are checked when sources are made; keep the straight
        // one's should one not be.
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            directivity[b] = r->directivity[b];
        }
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        float t = r->transmission[b] * spread;
        r->transmission[b] = fminf(1.0f, sqrtf(around[b] * around[b] + t * t));
        float d = r->directivity[b];
        r->directivity[b] =
            ws + wp > 0.0f ? sqrtf((ws * d * d + wp * directivity[b] * directivity[b]) / (ws + wp))
                           : d;
    }
    maudVector3 arrival = maudUnrotate(p->listener->orientation, Toward(v[0], v[1]));
    maudVector3 blend = {ws * r->direction.x + wp * arrival.x, ws * r->direction.y + wp * arrival.y,
                         ws * r->direction.z + wp * arrival.z};
    float norm = sqrtf(blend.x * blend.x + blend.y * blend.y + blend.z * blend.z);
    r->direction =
        norm > 0.0f ? (maudVector3){blend.x / norm, blend.y / norm, blend.z / norm} : arrival;
    r->distance = length;
    r->pathed = true;
}

static void RefineJobs(uint32_t start, uint32_t end, void* context)
{
    const maudPathing* p = context;
    for (uint32_t j = start; j < end; ++j)
    {
        uint32_t n = p->counts[j];
        if (n > 0)
        {
            maudVector3* v = p->vertices + (size_t)j * MAUD_PATH_VERTICES;
            n = maudRefinePath(p->queries->anyHit, p->queries->rayContext, v, n);
            Feed(p, &p->jobs[j], v, n);
        }
    }
}

void maudRunPathing(maudPathing* pathing, const maudProbeGraph* graph,
                    const maudProbeQueries* queries, const maudPose* listener, uint32_t count)
{
    maudPathing* p = pathing;
    p->queries = queries;
    p->listener = listener;
    p->jobCount = count < p->maxPaths ? count : p->maxPaths;
    maudPathEnd end;
    maudAttachPath(graph, graph->range, listener->position, queries->anyHit, queries->rayContext,
                   &end);
    if (end.count == 0 || p->jobCount == 0)
    {
        return;
    }
    for (uint32_t j = 0; j < p->jobCount; ++j)
    {
        p->counts[j] = Find(p, graph, &end, j);
    }
    if (queries->enqueueTask != nullptr)
    {
        void* task = queries->enqueueTask(RefineJobs, p->jobCount, 1, p, queries->userTaskContext);
        queries->finishTask(task, queries->userTaskContext);
    }
    else
    {
        RefineJobs(0, p->jobCount, p);
    }
}
