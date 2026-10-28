// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flier's path: Lazy Theta* over the flight volume's open
// blocks, each entered at the point of its face nearest the point the
// path comes from, in voxels of the volume's frame.

#include "flight_frame.h"
#include "flight_space.h"
#include "flight_volume.h"
#include "fpenv.h"
#include "invariant.h"
#include "query.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static_assert(sizeof(mnavFlightSearchNode) <= sizeof(mnavSearchNode), "flight nodes fit the nodes");

typedef mnavFlightSearchNode Node;

typedef struct Search
{
    mnavQuery* query;
    Node* nodes;
    mnavFlightCursor cursor;
    double end[3];
    int32_t endBlock[4];
    // The longest way allowed, in voxels.
    double limit;
    // The node being expanded, and the node its children are reached
    // from: its parent, or itself at the start.
    int32_t current;
    int32_t from;
    bool outOfNodes;
    bool tooLong;
    bool notLoaded;
    int32_t found;
    int32_t best;
} Search;

static double Distance(const double a[3], const double b[3])
{
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static bool SameBlock(const int32_t a[4], const int32_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static bool Sooner(const Node* nodes, int32_t a, int32_t b)
{
    double ta = nodes[a].cost + nodes[a].remaining;
    double tb = nodes[b].cost + nodes[b].remaining;
    return ta != tb ? ta < tb : a < b;
}

static void Place(Search* s, int32_t at, int32_t node)
{
    s->query->indexHeap[at] = node;
    s->nodes[node].heap = at;
}

static void SiftUp(Search* s, int32_t at)
{
    int32_t node = s->query->indexHeap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(s->nodes, node, s->query->indexHeap[up]))
        {
            break;
        }
        Place(s, at, s->query->indexHeap[up]);
        at = up;
    }
    Place(s, at, node);
}

static int32_t Pop(Search* s)
{
    mnavQuery* q = s->query;
    MNAV_ASSERT(q->heapCount > 0 && s->nodes[q->indexHeap[0]].heap == 0);
    int32_t top = q->indexHeap[0];
    int32_t last = q->indexHeap[--q->heapCount];
    int32_t at = 0;
    while (q->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= q->heapCount)
        {
            break;
        }
        if (child + 1 < q->heapCount &&
            Sooner(s->nodes, q->indexHeap[child + 1], q->indexHeap[child]))
        {
            child += 1;
        }
        if (!Sooner(s->nodes, q->indexHeap[child], last))
        {
            break;
        }
        Place(s, at, q->indexHeap[child]);
        at = child;
    }
    if (q->heapCount > 0)
    {
        Place(s, at, last);
    }
    s->nodes[top].heap = MNAV_CLOSED;
    return top;
}

// The table cell for a block's node, or the empty one where it goes.
static uint32_t Slot(const Search* s, const int32_t block[4])
{
    uint32_t key = (uint32_t)block[0] * 73856093u ^ (uint32_t)block[1] * 19349663u ^
                   (uint32_t)block[2] * 83492791u ^ (uint32_t)block[3];
    uint32_t at = (key * 2654435761u) & s->query->tableMask;
    for (;;)
    {
        int32_t node = s->query->table[at];
        if (node == MNAV_NO_NODE || SameBlock(s->nodes[node].block, block))
        {
            return at;
        }
        at = (at + 1) & s->query->tableMask;
    }
}

static bool InSight(Search* s, const double a[3], const double b[3])
{
    return mnavFlightWalk(&s->cursor, a, b, nullptr) == MNAV_SPACE_OPEN;
}

// How far a face point lies inside the block it enters, and in from the
// face's edges, in voxels. A point on a voxel's face or edge would count
// as in the voxel above it; inside, a step from the center of the block
// left crosses the face within its edges.
#define INSET      (1.0 / 1024.0)
#define EDGE_INSET (4.0 / 1024.0)

// The point of the face between block a and block b, entered, nearest p:
// inside b, and in from the face's edges.
static void FacePoint(const int32_t a[4], const int32_t b[4], const double p[3], double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        double lo = (double)(a[k] > b[k] ? a[k] : b[k]);
        double hi = (double)(a[k] + a[3] < b[k] + b[3] ? a[k] + a[3] : b[k] + b[3]);
        if (lo == hi)
        {
            // The axis the blocks meet on: just inside b.
            out[k] = b[k] == (int32_t)lo ? lo + INSET : lo - INSET;
            continue;
        }
        lo += EDGE_INSET;
        hi -= EDGE_INSET;
        out[k] = p[k] < lo ? lo : (p[k] > hi ? hi : p[k]);
    }
}

// The center of a node's block.
static void Center(const Node* n, double out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (double)n->block[k] + (double)n->block[3] * 0.5;
    }
}

// The node of a block reached, made if new; MNAV_NO_NODE past the budget.
static int32_t NodeOf(Search* s, const int32_t block[4])
{
    mnavQuery* q = s->query;
    uint32_t at = Slot(s, block);
    if (q->table[at] != MNAV_NO_NODE)
    {
        return q->table[at];
    }
    if (q->nodeCount == q->limits.nodes)
    {
        s->outOfNodes = true;
        return MNAV_NO_NODE;
    }
    int32_t node = q->nodeCount++;
    q->table[at] = node;
    s->nodes[node] = (Node){{block[0], block[1], block[2], block[3]},
                            {0.0, 0.0, 0.0},
                            (double)INFINITY,
                            0.0,
                            MNAV_NO_NODE,
                            MNAV_NO_NODE,
                            0,
                            MNAV_NO_NODE};
    return node;
}

// Reaches a block across a face of the current node's: from the node the
// children are reached from, to the face point nearest it, or to the end
// in the end's block.
static void Reach(void* context, mnavFlightBlock block)
{
    Search* s = context;
    const int32_t b[4] = {block.x, block.y, block.z, block.size};
    int32_t node = NodeOf(s, b);
    if (node == MNAV_NO_NODE || s->nodes[node].heap == MNAV_CLOSED)
    {
        return;
    }
    const Node* from = &s->nodes[s->from];
    double p[3];
    if (SameBlock(b, s->endBlock))
    {
        memcpy(p, s->end, sizeof(p));
    }
    else
    {
        FacePoint(s->nodes[s->current].block, b, from->point, p);
    }
    double cost = from->cost + Distance(from->point, p);
    double remaining = Distance(p, s->end);
    if (cost + remaining > s->limit)
    {
        s->tooLong = true;
        return;
    }
    Node* n = &s->nodes[node];
    if (cost >= n->cost)
    {
        return;
    }
    n->cost = cost;
    n->remaining = remaining;
    n->parent = s->from;
    n->via = s->current;
    n->bend = 0;
    memcpy(n->point, p, sizeof(p));
    if (n->heap == MNAV_NO_NODE)
    {
        // Each node enters the open list once, and the nodes made are at
        // most its room.
        MNAV_ASSERT(s->query->heapCount < s->query->limits.nodes);
        Place(s, s->query->heapCount++, node);
    }
    // A node still open is where the open list holds it.
    MNAV_ASSERT(s->query->indexHeap[n->heap] == node);
    SiftUp(s, n->heap);
}

// Visits every block across the six faces of a node's, noting faces with
// no tile loaded.
static void Around(Search* s, int32_t node, mnavFlightVisit visit)
{
    mnavFlightBlock b = {s->nodes[node].block[0], s->nodes[node].block[1], s->nodes[node].block[2],
                         s->nodes[node].block[3]};
    for (int32_t f = 0; f < 6; ++f)
    {
        if (mnavFlightFace(&s->cursor, &b, f, visit, s) == MNAV_SPACE_UNLOADED)
        {
            s->notLoaded = true;
        }
    }
}

// Lazy Theta*'s repair: a closed neighbor in sight that reaches the
// current node cheaper than the one that reached it.
static void Repair(void* context, mnavFlightBlock block)
{
    Search* s = context;
    const int32_t b[4] = {block.x, block.y, block.z, block.size};
    int32_t node = s->query->table[Slot(s, b)];
    if (node == MNAV_NO_NODE || s->nodes[node].heap != MNAV_CLOSED)
    {
        return;
    }
    Node* n = &s->nodes[s->current];
    double cost = s->nodes[node].cost + Distance(s->nodes[node].point, n->point);
    if (cost < n->cost && InSight(s, s->nodes[node].point, n->point))
    {
        n->cost = cost;
        n->parent = node;
        n->bend = 0;
    }
}

// The points a node's way bends through from the neighbor that reached
// it, in order: that neighbor's block's center and, for the end, whose
// point is anywhere in its block, the face point nearest the center. The
// neighbor's block holds the first step, the face is crossed within its
// edges, and the end's block holds the last. Returns their number.
static int32_t Bend(const Search* s, const Node* n, double out[2][3])
{
    const Node* via = &s->nodes[n->via];
    Center(via, out[0]);
    if (!SameBlock(n->block, s->endBlock))
    {
        return 1;
    }
    FacePoint(via->block, n->block, out[0], out[1]);
    return 2;
}

// Checks a node just taken off the open list against its parent: out of
// sight, it takes the neighbor that reached it, straight when in sight
// and else bending (Bend); or a closed neighbor in sight that reaches it
// cheaper.
static void Settle(Search* s, int32_t node)
{
    Node* n = &s->nodes[node];
    if (n->parent == MNAV_NO_NODE || InSight(s, s->nodes[n->parent].point, n->point))
    {
        return;
    }
    // Reach names the neighbor whenever it names the parent; Bend relies
    // on it too, for a node that bent here.
    MNAV_ASSERT(n->via != MNAV_NO_NODE);
    const Node* via = &s->nodes[n->via];
    if (n->parent != n->via && InSight(s, via->point, n->point))
    {
        n->cost = via->cost + Distance(via->point, n->point);
        n->bend = 0;
    }
    else
    {
        double bend[2][3];
        int32_t count = Bend(s, n, bend);
        n->cost = via->cost + Distance(via->point, bend[0]) + Distance(bend[count - 1], n->point) +
                  (count == 2 ? Distance(bend[0], bend[1]) : 0.0);
        n->bend = 1;
    }
    n->parent = n->via;
    s->current = node;
    Around(s, node, Repair);
}

static bool Nearer(const Node* nodes, int32_t a, int32_t b)
{
    return b == MNAV_NO_NODE || nodes[a].remaining < nodes[b].remaining;
}

// Closes up to budget nodes; returns whether the search has ended.
static bool Run(Search* s, int32_t budget)
{
    mnavQuery* q = s->query;
    for (int32_t closed = 0; closed < budget && q->heapCount > 0; ++closed)
    {
        int32_t node = Pop(s);
        Settle(s, node);
        s->best = Nearer(s->nodes, node, s->best) ? node : s->best;
        if (SameBlock(s->nodes[node].block, s->endBlock))
        {
            s->found = node;
            return true;
        }
        s->current = node;
        s->from = s->nodes[node].parent != MNAV_NO_NODE ? s->nodes[node].parent : node;
        Around(s, node, Reach);
    }
    return q->heapCount == 0;
}

// Writes the path to a node, in world coordinates, from the start.
static mnavFlightPath PathTo(const Search* s, const mnavFlightFrame* frame, int32_t last,
                             mnavPathEnd end)
{
    mnavPos3* points = s->query->points;
    int32_t count = 0;
    for (int32_t n = last; n != MNAV_NO_NODE; n = s->nodes[n].parent)
    {
        MNAV_ASSERT(count < 2 * s->query->limits.nodes + 1);
        points[count++] = mnavFlightToWorld(frame, s->nodes[n].point);
        if (s->nodes[n].bend != 0)
        {
            // The end alone bends through two points, and the start never
            // bends, so the points still number at most twice the nodes.
            double bend[2][3];
            for (int32_t k = Bend(s, &s->nodes[n], bend) - 1; k >= 0; --k)
            {
                MNAV_ASSERT(count < 2 * s->query->limits.nodes + 1);
                points[count++] = mnavFlightToWorld(frame, bend[k]);
            }
        }
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPos3 swap = points[i];
        points[i] = points[count - 1 - i];
        points[count - 1 - i] = swap;
    }
    return (mnavFlightPath){end, s->nodes[last].cost * frame->voxel, points, count};
}

// The path when the search cannot begin: the start alone.
static mnavFlightPath StartOnly(mnavQuery* query, mnavPos3 start, mnavPathEnd end)
{
    query->points[0] = start;
    return (mnavFlightPath){end, 0.0, query->points, 1};
}

static mnavPathEnd Refused(int32_t held)
{
    return held == MNAV_SPACE_UNLOADED ? mnav_pathNotLoaded : mnav_pathNone;
}

// The most blocks of the end's space Pocket searches: more than the
// closed space inside a hollow object of a few meters holds.
#define POCKET_BLOCKS 512

// The end's space as Pocket spreads over it: the start's block, the
// blocks it may make, and whether it has found the space open.
typedef struct Spread
{
    Search* search;
    const int32_t* start;
    int32_t budget;
    bool open;
} Spread;

// Adds a block across a face to the end's space, unless it was in it.
static void SpreadTo(void* context, mnavFlightBlock block)
{
    Spread* p = context;
    mnavQuery* q = p->search->query;
    const int32_t b[4] = {block.x, block.y, block.z, block.size};
    if (p->open || SameBlock(b, p->start))
    {
        p->open = true;
        return;
    }
    uint32_t at = Slot(p->search, b);
    if (q->table[at] != MNAV_NO_NODE)
    {
        return;
    }
    if (q->nodeCount == p->budget)
    {
        p->open = true;
        return;
    }
    q->table[at] = q->nodeCount;
    memcpy(p->search->nodes[q->nodeCount++].block, b, sizeof(b));
}

// Whether the end's block lies in a closed space: the open blocks linked
// to it through shared faces, searched breadth first, run out within
// POCKET_BLOCKS (and the node budget) without the start's block or a face
// with no tile loaded. No path reaches the end from outside it. Uses the
// search's nodes and table, which Begin clears.
static bool Pocket(Search* s, const int32_t start[4])
{
    mnavQuery* q = s->query;
    mnavClearTable(q);
    q->nodeCount = 0;
    Spread p = {s, start, q->limits.nodes < POCKET_BLOCKS ? q->limits.nodes : POCKET_BLOCKS, false};
    const mnavFlightBlock end = {s->endBlock[0], s->endBlock[1], s->endBlock[2], s->endBlock[3]};
    SpreadTo(&p, end);
    for (int32_t at = 0; at < q->nodeCount && !p.open; ++at)
    {
        const int32_t* b = s->nodes[at].block;
        const mnavFlightBlock block = {b[0], b[1], b[2], b[3]};
        for (int32_t f = 0; f < 6 && !p.open; ++f)
        {
            if (mnavFlightFace(&s->cursor, &block, f, SpreadTo, &p) == MNAV_SPACE_UNLOADED)
            {
                p.open = true;
            }
        }
    }
    return !p.open;
}

// Starts the search at the start's block; the start is node 0.
static void Begin(Search* s, const mnavFlightBlock* block, const double start[3])
{
    mnavQuery* q = s->query;
    mnavClearTable(q);
    q->heapCount = 0;
    q->nodeCount = 0;
    const int32_t b[4] = {block->x, block->y, block->z, block->size};
    int32_t node = NodeOf(s, b);
    Node* n = &s->nodes[node];
    memcpy(n->point, start, sizeof(n->point));
    n->cost = 0.0;
    n->remaining = Distance(start, s->end);
    Place(s, q->heapCount++, node);
}

// The search kept in the context, ready to run another slice.
static Search Load(mnavQuery* query)
{
    const mnavFlightSliced* f = &query->flight;
    Search s = {.query = query,
                .nodes = query->flightNodes,
                .cursor = mnavMakeFlightCursor(f->volume),
                .limit = f->limit,
                .current = f->current,
                .from = f->from,
                .outOfNodes = f->outOfNodes,
                .tooLong = f->tooLong,
                .notLoaded = f->notLoaded,
                .found = f->found,
                .best = f->best};
    memcpy(s.end, f->end, sizeof(s.end));
    memcpy(s.endBlock, f->endBlock, sizeof(s.endBlock));
    return s;
}

// Keeps what a slice changed in the context.
static void Store(const Search* s)
{
    mnavFlightSliced* f = &s->query->flight;
    f->current = s->current;
    f->from = s->from;
    f->outOfNodes = s->outOfNodes;
    f->tooLong = s->tooLong;
    f->notLoaded = s->notLoaded;
    f->found = s->found;
    f->best = s->best;
}

// Whether the context holds a search on this volume, unchanged since it
// began: mnav_success; mnav_errorState for none begun or a commit to the
// volume since; mnav_errorInvalid for another volume.
static mnavResult Holds(const mnavQuery* query, const mnavFlightVolume* volume)
{
    if (query == nullptr || volume == nullptr)
    {
        return mnav_errorInvalid;
    }
    const mnavFlightSliced* f = &query->flight;
    if (!f->active)
    {
        return mnav_errorState;
    }
    if (f->volume != volume)
    {
        return mnav_errorInvalid;
    }
    return f->commits == volume->commits ? mnav_success : mnav_errorState;
}

static MNAV_NOINLINE mnavResult BeginFlightPathWork(mnavQuery* query,
                                                    const mnavFlightVolume* volume, mnavPos3 start,
                                                    mnavPos3 end)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (query == nullptr || volume == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavFlightFrame frame = mnavMakeFlightFrame(volume);
    Search s = {.query = query,
                .nodes = query->flightNodes,
                .cursor = mnavMakeFlightCursor(volume),
                .found = MNAV_NO_NODE,
                .best = MNAV_NO_NODE};
    double a[3];
    int32_t at[3];
    int32_t to[3];
    mnavResult result = mnavFlightToVoxels(&frame, start, a, at);
    result = result == mnav_success ? mnavFlightToVoxels(&frame, end, s.end, to) : result;
    if (result != mnav_success)
    {
        return result;
    }
    // Below the floor or above the ceiling, the volume's def's bounds.
    if (at[1] < 0 || at[1] >= frame.layers || to[1] < 0 || to[1] >= frame.layers)
    {
        return mnav_errorOutside;
    }
    query->search.active = false;
    query->flight = (mnavFlightSliced){.volume = volume,
                                       .commits = volume->commits,
                                       .start = start,
                                       .endPoint = end,
                                       .found = MNAV_NO_NODE,
                                       .best = MNAV_NO_NODE,
                                       .ready = mnav_pathFound,
                                       .active = true};
    mnavFlightSliced* f = &query->flight;
    mnavFlightBlock first;
    mnavFlightBlock last;
    int32_t held = mnavFlightHolder(&s.cursor, at[0], at[1], at[2], &first);
    int32_t heldEnd = mnavFlightHolder(&s.cursor, to[0], to[1], to[2], &last);
    if (held != MNAV_SPACE_OPEN || heldEnd != MNAV_SPACE_OPEN)
    {
        f->ready = Refused(held != MNAV_SPACE_OPEN ? held : heldEnd);
        return mnav_success;
    }
    if (first.x == last.x && first.y == last.y && first.z == last.z && first.size == last.size)
    {
        // One open block holds both: the straight way.
        f->straight = true;
        f->straightLength = Distance(a, s.end) * frame.voxel;
        return mnav_success;
    }
    s.endBlock[0] = last.x;
    s.endBlock[1] = last.y;
    s.endBlock[2] = last.z;
    s.endBlock[3] = last.size;
    const int32_t from[4] = {first.x, first.y, first.z, first.size};
    if (Pocket(&s, from))
    {
        f->ready = mnav_pathNone;
        return mnav_success;
    }
    s.limit = (double)query->limits.pathLength / frame.voxel;
    memcpy(f->end, s.end, sizeof(f->end));
    memcpy(f->endBlock, s.endBlock, sizeof(f->endBlock));
    f->limit = s.limit;
    Begin(&s, &first, a);
    Store(&s);
    return mnav_success;
}

static MNAV_NOINLINE mnavResult BeginFlightPathCounted(mnavQuery* query,
                                                       const mnavFlightVolume* volume,
                                                       mnavPos3 start, mnavPos3 end)
{
    return mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(volume)),
                                BeginFlightPathWork(query, volume, start, end));
}

mnavResult mnavBeginFlightPath(mnavQuery* query, const mnavFlightVolume* volume, mnavPos3 start,
                               mnavPos3 end)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = BeginFlightPathCounted(query, volume, start, end);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult ContinueFlightPathWork(mnavQuery* query,
                                                       const mnavFlightVolume* volume,
                                                       int32_t nodes, bool* endedOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    mnavResult held = Holds(query, volume);
    if (held != mnav_success || endedOut == nullptr || nodes < 1)
    {
        return held != mnav_success ? held : mnav_errorInvalid;
    }
    const mnavFlightSliced* f = &query->flight;
    if (f->ready != mnav_pathFound || f->straight || f->found != MNAV_NO_NODE)
    {
        *endedOut = true;
        return mnav_success;
    }
    Search s = Load(query);
    *endedOut = Run(&s, nodes);
    Store(&s);
    return mnav_success;
}

static MNAV_NOINLINE mnavResult ContinueFlightPathCounted(mnavQuery* query,
                                                          const mnavFlightVolume* volume,
                                                          int32_t nodes, bool* endedOut)
{
    return mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(volume)),
                                ContinueFlightPathWork(query, volume, nodes, endedOut));
}

mnavResult mnavContinueFlightPath(mnavQuery* query, const mnavFlightVolume* volume, int32_t nodes,
                                  bool* endedOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = ContinueFlightPathCounted(query, volume, nodes, endedOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult FinishFlightPathWork(mnavQuery* query,
                                                     const mnavFlightVolume* volume,
                                                     mnavFlightPath* pathOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    mnavResult held = Holds(query, volume);
    if (held != mnav_success || pathOut == nullptr)
    {
        return held != mnav_success ? held : mnav_errorInvalid;
    }
    mnavFlightSliced* f = &query->flight;
    f->active = false;
    if (f->ready != mnav_pathFound)
    {
        *pathOut = StartOnly(query, f->start, f->ready);
        return mnav_success;
    }
    if (f->straight)
    {
        query->points[0] = f->start;
        query->points[1] = f->endPoint;
        *pathOut = (mnavFlightPath){mnav_pathFound, f->straightLength, query->points, 2};
        return mnav_success;
    }
    Search s = Load(query);
    bool ended = s.found != MNAV_NO_NODE || query->heapCount == 0;
    mnavPathEnd ending = s.found != MNAV_NO_NODE ? mnav_pathFound
                         : !ended                ? mnav_pathUnfinished
                         : s.outOfNodes          ? mnav_pathOutOfNodes
                         : s.tooLong             ? mnav_pathTooLong
                         : s.notLoaded           ? mnav_pathNotLoaded
                                                 : mnav_pathNone;
    mnavFlightFrame frame = mnavMakeFlightFrame(volume);
    *pathOut = PathTo(&s, &frame, s.found != MNAV_NO_NODE ? s.found : s.best, ending);
    return mnav_success;
}

static MNAV_NOINLINE mnavResult FinishFlightPathCounted(mnavQuery* query,
                                                        const mnavFlightVolume* volume,
                                                        mnavFlightPath* pathOut)
{
    return mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(volume)),
                                FinishFlightPathWork(query, volume, pathOut));
}

mnavResult mnavFinishFlightPath(mnavQuery* query, const mnavFlightVolume* volume,
                                mnavFlightPath* pathOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = FinishFlightPathCounted(query, volume, pathOut);
    mnavFloatEnvLeave(env);
    return result;
}

static MNAV_NOINLINE mnavResult FindFlightPathWork(mnavQuery* query, const mnavFlightVolume* volume,
                                                   mnavPos3 start, mnavPos3 end,
                                                   mnavFlightPath* pathOut)
{
    if (query != nullptr && mnavHostCalledBack(&query->host))
    {
        return mnav_errorBusy;
    }
    if (pathOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    mnavResult result = BeginFlightPathWork(query, volume, start, end);
    bool ended = false;
    while (result == mnav_success && !ended)
    {
        result = ContinueFlightPathWork(query, volume, INT32_MAX, &ended);
    }
    return result == mnav_success ? FinishFlightPathWork(query, volume, pathOut) : result;
}

static MNAV_NOINLINE mnavResult FindFlightPathCounted(mnavQuery* query,
                                                      const mnavFlightVolume* volume,
                                                      mnavPos3 start, mnavPos3 end,
                                                      mnavFlightPath* pathOut)
{
    return mnavHostCountInvalid(mnavHostFirst(MNAV_HOST(query), MNAV_HOST(volume)),
                                FindFlightPathWork(query, volume, start, end, pathOut));
}

mnavResult mnavFindFlightPath(mnavQuery* query, const mnavFlightVolume* volume, mnavPos3 start,
                              mnavPos3 end, mnavFlightPath* pathOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = FindFlightPathCounted(query, volume, start, end, pathOut);
    mnavFloatEnvLeave(env);
    return result;
}
