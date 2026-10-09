// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Grid paths (mnav-0005): A* over weighted grids, and jump point search
// (Harabor and Grastien, 2011) when every included area costs the same,
// with no blocked corner cut: a diagonal step needs both cells beside it
// open. Nodes, the open list and the node table live in the query
// context's memory, which serves one query at a time.

#include "query.h"
#include "query_filter.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// The diagonal's length in cells, rounded once to binary64.
#define DIAGONAL 1.4142135623730951

static_assert(sizeof(mnavGridNode) <= sizeof(mnavSearchNode), "grid nodes fit the nodes");
static_assert(sizeof(mnavCell) <= sizeof(mnavPos3), "cells fit the points");

typedef mnavGridNode GridNode;

// A step between neighbouring cells.
typedef struct Step
{
    int32_t dx;
    int32_t dy;
} Step;

typedef struct Grid
{
    mnavQuery* query;
    GridNode* nodes;
    const mnavGrid* grid;
    const mnavQueryFilter* filter;
    mnavCell end;
    // The cost of every included area, when they all cost the same; 0
    // otherwise.
    double uniform;
    // The heuristic's scale per meter.
    double cheapest;
    double limit;
    bool outOfNodes;
    bool tooLong;
    int32_t found;
    int32_t best;
} Grid;

static bool Open(const Grid* g, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= g->grid->width || y >= g->grid->height)
    {
        return false;
    }
    mnavAreaType area = g->grid->areas[(size_t)y * (size_t)g->grid->width + (size_t)x];
    return area != mnav_areaNone && mnavIncludes(g->filter, area);
}

static double AreaCost(const Grid* g, int32_t x, int32_t y)
{
    return (double)g->filter->costs[g->grid->areas[(size_t)y * (size_t)g->grid->width + (size_t)x]];
}

// The octile distance between two cells, in meters.
static double Octile(const Grid* g, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    double dx = fabs((double)x1 - (double)x0);
    double dy = fabs((double)y1 - (double)y0);
    double low = dx < dy ? dx : dy;
    double high = dx < dy ? dy : dx;
    return ((high - low) + low * DIAGONAL) * (double)g->grid->cellSize;
}

// The open list, a binary heap by total cost, then by node index.
static bool Sooner(const GridNode* nodes, int32_t a, int32_t b)
{
    double ta = nodes[a].cost + nodes[a].remaining;
    double tb = nodes[b].cost + nodes[b].remaining;
    return ta != tb ? ta < tb : a < b;
}

static void Place(Grid* g, int32_t at, int32_t node)
{
    g->query->indexHeap[at] = node;
    g->nodes[node].heap = at;
}

static void SiftUp(Grid* g, int32_t at)
{
    int32_t node = g->query->indexHeap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(g->nodes, node, g->query->indexHeap[up]))
        {
            break;
        }
        Place(g, at, g->query->indexHeap[up]);
        at = up;
    }
    Place(g, at, node);
}

static int32_t Pop(Grid* g)
{
    mnavQuery* q = g->query;
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
            Sooner(g->nodes, q->indexHeap[child + 1], q->indexHeap[child]))
        {
            child += 1;
        }
        if (!Sooner(g->nodes, q->indexHeap[child], last))
        {
            break;
        }
        Place(g, at, q->indexHeap[child]);
        at = child;
    }
    if (q->heapCount > 0)
    {
        Place(g, at, last);
    }
    g->nodes[top].heap = MNAV_CLOSED;
    return top;
}

// The table cell for a grid cell's node, or the empty one where it goes.
static uint32_t Slot(const Grid* g, int32_t x, int32_t y)
{
    uint32_t key = (uint32_t)y * (uint32_t)g->grid->width + (uint32_t)x;
    uint32_t at = (key * 2654435761u) & g->query->tableMask;
    for (;;)
    {
        int32_t node = g->query->table[at];
        if (node == MNAV_NO_NODE || (g->nodes[node].x == x && g->nodes[node].y == y))
        {
            return at;
        }
        at = (at + 1) & g->query->tableMask;
    }
}

// Reaches cell (x, y) from node from by a step or a jump of the length and
// cost given; opens or improves its node.
static void Reach(Grid* g, int32_t from, int32_t x, int32_t y, double length, double cost)
{
    mnavQuery* q = g->query;
    const GridNode* parent = &g->nodes[from];
    double remaining = Octile(g, x, y, g->end.x, g->end.y);
    if (parent->length + length + remaining > g->limit)
    {
        g->tooLong = true;
        return;
    }
    uint32_t at = Slot(g, x, y);
    int32_t node = q->table[at];
    if (node == MNAV_NO_NODE)
    {
        if (q->nodeCount == q->limits.nodes)
        {
            g->outOfNodes = true;
            return;
        }
        node = q->nodeCount++;
        q->table[at] = node;
        g->nodes[node] = (GridNode){
            x, y, MNAV_NO_NODE, MNAV_NO_NODE, (double)INFINITY, 0.0, remaining * g->cheapest};
    }
    GridNode* n = &g->nodes[node];
    double total = parent->cost + cost;
    // Closed nodes are final, as in the navmesh search: with a consistent
    // heuristic only rounding finds a cheaper way to one.
    if (n->heap == MNAV_CLOSED || total >= n->cost)
    {
        return;
    }
    n->cost = total;
    n->length = parent->length + length;
    n->parent = from;
    if (n->heap == MNAV_NO_NODE)
    {
        Place(g, q->heapCount++, node);
    }
    SiftUp(g, n->heap);
}

// The eight directions, in the order a node's neighbours are tried.
static const Step s_steps[8] = {{1, 0}, {0, 1},  {-1, 0},  {0, -1},
                                {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};

// Whether a step from (x, y) by (dx, dy) is allowed.
static bool CanStep(const Grid* g, int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    if (!Open(g, x + dx, y + dy))
    {
        return false;
    }
    return dx == 0 || dy == 0 || (Open(g, x + dx, y) && Open(g, x, y + dy));
}

static void ExpandAStar(Grid* g, int32_t node)
{
    int32_t x = g->nodes[node].x;
    int32_t y = g->nodes[node].y;
    double here = AreaCost(g, x, y);
    for (int32_t d = 0; d < 8; ++d)
    {
        int32_t dx = s_steps[d].dx;
        int32_t dy = s_steps[d].dy;
        if (!CanStep(g, x, y, dx, dy))
        {
            continue;
        }
        double length = (dx != 0 && dy != 0 ? DIAGONAL : 1.0) * (double)g->grid->cellSize;
        double cost = length * (here + AreaCost(g, x + dx, y + dy)) * 0.5;
        Reach(g, node, x + dx, y + dy, length, cost);
    }
}

// Whether cell (x, y), reached moving straight by (dx, dy), has a forced
// neighbour: a side cell open with the one behind it blocked.
static bool Forced(const Grid* g, int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    if (dx != 0)
    {
        return (Open(g, x, y - 1) && !Open(g, x - dx, y - 1)) ||
               (Open(g, x, y + 1) && !Open(g, x - dx, y + 1));
    }
    return (Open(g, x - 1, y) && !Open(g, x - 1, y - dy)) ||
           (Open(g, x + 1, y) && !Open(g, x + 1, y - dy));
}

static bool IsEnd(const Grid* g, int32_t x, int32_t y)
{
    return x == g->end.x && y == g->end.y;
}

// Whether a straight jump from (x, y), not counting it, finds a jump
// point.
static bool StraightFinds(const Grid* g, int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    for (;;)
    {
        x += dx;
        y += dy;
        if (!Open(g, x, y))
        {
            return false;
        }
        if (IsEnd(g, x, y) || Forced(g, x, y, dx, dy))
        {
            return true;
        }
    }
}

// Jumps from (x, y) by (dx, dy), the first step allowed; returns the
// steps to the jump point found, or 0.
static int32_t Jump(const Grid* g, int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    for (int32_t steps = 1;; ++steps)
    {
        x += dx;
        y += dy;
        if (!Open(g, x, y))
        {
            return 0;
        }
        bool diagonal = dx != 0 && dy != 0;
        if (IsEnd(g, x, y) ||
            (diagonal ? StraightFinds(g, x, y, dx, 0) || StraightFinds(g, x, y, 0, dy)
                      : Forced(g, x, y, dx, dy)))
        {
            return steps;
        }
        if (diagonal && !(Open(g, x + dx, y) && Open(g, x, y + dy)))
        {
            return 0;
        }
    }
}

static int32_t Sign(int32_t v)
{
    return (v > 0) - (v < 0);
}

// The directions jump point search tries from a node: all allowed steps
// at the start, else those pruning by the way it came keeps.
static int32_t Directions(const Grid* g, int32_t node, Step out[8])
{
    const GridNode* n = &g->nodes[node];
    Step candidates[8];
    int32_t c = 0;
    if (n->parent == MNAV_NO_NODE)
    {
        for (int32_t d = 0; d < 8; ++d)
        {
            candidates[c++] = s_steps[d];
        }
    }
    else
    {
        int32_t dx = Sign(n->x - g->nodes[n->parent].x);
        int32_t dy = Sign(n->y - g->nodes[n->parent].y);
        if (dx != 0 && dy != 0)
        {
            // On a diagonal: its two axes and itself.
            candidates[c++] = (Step){dx, 0};
            candidates[c++] = (Step){0, dy};
            candidates[c++] = (Step){dx, dy};
        }
        else
        {
            // Straight: ahead, the two forward diagonals and the sides.
            int32_t sx = dy;
            int32_t sy = dx;
            candidates[c++] = (Step){dx, dy};
            candidates[c++] = (Step){dx + sx, dy + sy};
            candidates[c++] = (Step){dx - sx, dy - sy};
            candidates[c++] = (Step){sx, sy};
            candidates[c++] = (Step){-sx, -sy};
        }
    }
    int32_t count = 0;
    for (int32_t i = 0; i < c; ++i)
    {
        if (CanStep(g, n->x, n->y, candidates[i].dx, candidates[i].dy))
        {
            out[count++] = candidates[i];
        }
    }
    return count;
}

static void ExpandJump(Grid* g, int32_t node)
{
    Step directions[8];
    int32_t count = Directions(g, node, directions);
    int32_t x = g->nodes[node].x;
    int32_t y = g->nodes[node].y;
    for (int32_t d = 0; d < count; ++d)
    {
        int32_t dx = directions[d].dx;
        int32_t dy = directions[d].dy;
        int32_t steps = Jump(g, x, y, dx, dy);
        if (steps == 0)
        {
            continue;
        }
        double length =
            (double)steps * (dx != 0 && dy != 0 ? DIAGONAL : 1.0) * (double)g->grid->cellSize;
        Reach(g, node, x + steps * dx, y + steps * dy, length, length * g->uniform);
    }
}

// Whether node a lies nearer the end than node b: by the heuristic, then
// the cost, then the index.
static bool Nearer(const GridNode* nodes, int32_t a, int32_t b)
{
    if (nodes[a].remaining != nodes[b].remaining)
    {
        return nodes[a].remaining < nodes[b].remaining;
    }
    return nodes[a].cost != nodes[b].cost ? nodes[a].cost < nodes[b].cost : a < b;
}

// The cost every included area has, or 0 when they differ.
static double Uniform(const mnavQueryFilter* filter)
{
    double cost = 0.0;
    for (int32_t a = 1; a < MNAV_AREA_TYPES; ++a)
    {
        if (!mnavIncludes(filter, (mnavAreaType)a))
        {
            continue;
        }
        double c = (double)filter->costs[a];
        if (cost != 0.0 && c != cost)
        {
            return 0.0;
        }
        cost = c;
    }
    return cost;
}

// Writes the path to a node: its turning cells, from the start.
static mnavGridPath PathTo(const Grid* g, int32_t last, mnavPathEnd end)
{
    mnavCell* cells = g->query->cells;
    int32_t count = 0;
    for (int32_t n = last; n != MNAV_NO_NODE; n = g->nodes[n].parent)
    {
        cells[count++] = (mnavCell){g->nodes[n].x, g->nodes[n].y};
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavCell swap = cells[i];
        cells[i] = cells[count - 1 - i];
        cells[count - 1 - i] = swap;
    }
    // Keep the ends and the cells where the direction changes.
    int32_t kept = count > 0 ? 1 : 0;
    for (int32_t i = 1; i < count; ++i)
    {
        bool turn = i + 1 == count ||
                    Sign(cells[i].x - cells[i - 1].x) != Sign(cells[i + 1].x - cells[i].x) ||
                    Sign(cells[i].y - cells[i - 1].y) != Sign(cells[i + 1].y - cells[i].y);
        if (turn)
        {
            cells[kept++] = cells[i];
        }
    }
    return (mnavGridPath){end, g->nodes[last].cost, g->nodes[last].length, cells, kept};
}

static bool GoodGrid(const mnavGrid* grid, mnavCell a, mnavCell b)
{
    return grid->areas != nullptr && grid->width >= 1 && grid->width <= MNAV_MAX_GRID_SIDE &&
           grid->height >= 1 && grid->height <= MNAV_MAX_GRID_SIDE && isfinite(grid->cellSize) &&
           grid->cellSize > 0.0f && a.x >= 0 && a.y >= 0 && a.x < grid->width &&
           a.y < grid->height && b.x >= 0 && b.y >= 0 && b.x < grid->width && b.y < grid->height;
}

mnavResult mnavFindGridPath(mnavQuery* query, const mnavGrid* grid, const mnavQueryFilter* filter,
                            mnavCell start, mnavCell end, mnavGridPath* pathOut)
{
    if (query == nullptr || grid == nullptr || pathOut == nullptr || !GoodGrid(grid, start, end))
    {
        return mnav_errorInvalid;
    }
    const mnavQueryFilter* usable = nullptr;
    mnavResult checked = mnavCheckFilter(filter, &usable);
    if (checked != mnav_success)
    {
        return checked;
    }
    // The filter is copied: the caller's may change before the next query.
    query->filter = *usable;
    query->search.active = false;
    Grid g = {query,
              query->gridNodes,
              grid,
              &query->filter,
              end,
              Uniform(&query->filter),
              mnavCheapest(&query->filter),
              (double)query->limits.pathLength,
              false,
              false,
              MNAV_NO_NODE,
              0};
    memset(query->table, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    query->heapCount = 0;
    query->nodeCount = 1;
    g.nodes[0] = (GridNode){start.x,
                            start.y,
                            MNAV_NO_NODE,
                            MNAV_NO_NODE,
                            0.0,
                            0.0,
                            Octile(&g, start.x, start.y, end.x, end.y) * g.cheapest};
    query->table[Slot(&g, start.x, start.y)] = 0;
    if (!Open(&g, start.x, start.y))
    {
        *pathOut = PathTo(&g, 0, mnav_pathNone);
        return mnav_success;
    }
    Place(&g, query->heapCount++, 0);
    while (query->heapCount > 0)
    {
        int32_t node = Pop(&g);
        g.best = Nearer(g.nodes, node, g.best) ? node : g.best;
        if (IsEnd(&g, g.nodes[node].x, g.nodes[node].y))
        {
            g.found = node;
            break;
        }
        if (g.uniform > 0.0)
        {
            ExpandJump(&g, node);
        }
        else
        {
            ExpandAStar(&g, node);
        }
    }
    mnavPathEnd ending = g.found != MNAV_NO_NODE ? mnav_pathFound
                         : g.outOfNodes          ? mnav_pathOutOfNodes
                         : g.tooLong             ? mnav_pathTooLong
                                                 : mnav_pathNone;
    *pathOut = PathTo(&g, g.found != MNAV_NO_NODE ? g.found : g.best, ending);
    return mnav_success;
}
