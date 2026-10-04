// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow fields (mnav-0007): Dijkstra's search from every goal at once over
// a grid, with the grid path's steps. The step rule is symmetric, so the
// cost found from the goals to a cell is the cost of the cell's way to
// them. The open list is a binary heap by cost, then by cell index, with
// each cell's place in it kept for lowering its cost.

#include "maul-nav/flow.h"

#include "allocator.h"
#include "draw.h"
#include "flow.h"
#include "query_filter.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Marks a def built by mnavDefaultFlowFieldDef.
#define FLOW_DEF_COOKIE 0x4E415646u

// The diagonal's length in cells, rounded once to binary64.
#define DIAGONAL 1.4142135623730951

mnavFlowFieldDef mnavDefaultFlowFieldDef(void)
{
    return (mnavFlowFieldDef){FLOW_DEF_COOKIE, {0}, 65536};
}

mnavResult mnavCreateFlowField(const mnavFlowFieldDef* def, mnavFlowField** fieldOut)
{
    if (fieldOut == nullptr)
    {
        return mnav_errorInvalid;
    }
    *fieldOut = nullptr;
    if (def == nullptr || def->cookie != FLOW_DEF_COOKIE ||
        (def->allocator.alloc == nullptr) != (def->allocator.free == nullptr))
    {
        return mnav_errorInvalid;
    }
    if (def->cells < 1 || def->cells > MNAV_MAX_FLOW_CELLS)
    {
        return mnav_errorRange;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, UINT64_MAX);
    mnavFlowField* f = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavFlowField), alignof(mnavFlowField), (void**)&f);
    if (result != mnav_success)
    {
        return result;
    }
    *f = (mnavFlowField){0};
    f->memory = memory;
    f->def = *def;
    size_t cells = (size_t)def->cells;
    result = mnavAllocate(&f->memory, cells, sizeof(double), alignof(double), (void**)&f->costs);
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(int32_t), alignof(int32_t), (void**)&f->next);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(int32_t), alignof(int32_t), (void**)&f->places);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(int32_t), alignof(int32_t), (void**)&f->heap);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(int32_t), alignof(int32_t), (void**)&f->goals);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(int32_t), alignof(int32_t), (void**)&f->touched);
    }
    if (result == mnav_success)
    {
        result =
            mnavAllocate(&f->memory, cells, sizeof(uint8_t), alignof(uint8_t), (void**)&f->marks);
    }
    if (result == mnav_success)
    {
        memset(f->marks, 0, cells);
    }
    if (result != mnav_success)
    {
        mnavDestroyFlowField(f);
        return result;
    }
    *fieldOut = f;
    return mnav_success;
}

void mnavDestroyFlowField(mnavFlowField* field)
{
    if (field == nullptr)
    {
        return;
    }
    mnavMemory memory = field->memory;
    size_t cells = (size_t)field->def.cells;
    mnavRelease(&memory, field->costs, cells, sizeof(double), alignof(double));
    mnavRelease(&memory, field->next, cells, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->places, cells, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->heap, cells, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->goals, cells, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->touched, cells, sizeof(int32_t), alignof(int32_t));
    mnavRelease(&memory, field->marks, cells, sizeof(uint8_t), alignof(uint8_t));
    mnavRelease(&memory, field, 1, sizeof(mnavFlowField), alignof(mnavFlowField));
}

bool mnavFlowOpen(const mnavFlowSearch* s, int32_t x, int32_t y)
{
    const mnavFlowRegion* r = &s->field->region;
    if (x < r->x || y < r->y || x >= r->x + r->width || y >= r->y + r->height)
    {
        return false;
    }
    mnavAreaType area = s->areas[(size_t)y * (size_t)s->field->gridWidth + (size_t)x];
    return area != mnav_areaNone && mnavIncludes(&s->field->filter, area);
}

double mnavFlowAreaCost(const mnavFlowSearch* s, int32_t cell)
{
    const mnavFlowField* f = s->field;
    int32_t x = f->region.x + cell % f->region.width;
    int32_t y = f->region.y + cell / f->region.width;
    return (double)f->filter.costs[s->areas[(size_t)y * (size_t)f->gridWidth + (size_t)x]];
}

// The heap, by cost, then by cell index.
static bool Sooner(const mnavFlowField* f, int32_t a, int32_t b)
{
    return f->costs[a] != f->costs[b] ? f->costs[a] < f->costs[b] : a < b;
}

static void Place(mnavFlowField* f, int32_t at, int32_t cell)
{
    f->heap[at] = cell;
    f->places[cell] = at;
}

static void SiftUp(mnavFlowField* f, int32_t at)
{
    int32_t cell = f->heap[at];
    while (at > 0)
    {
        int32_t up = (at - 1) / 2;
        if (!Sooner(f, cell, f->heap[up]))
        {
            break;
        }
        Place(f, at, f->heap[up]);
        at = up;
    }
    Place(f, at, cell);
}

static int32_t Pop(mnavFlowField* f)
{
    int32_t top = f->heap[0];
    int32_t last = f->heap[--f->heapCount];
    int32_t at = 0;
    while (f->heapCount > 0)
    {
        int32_t child = 2 * at + 1;
        if (child >= f->heapCount)
        {
            break;
        }
        if (child + 1 < f->heapCount && Sooner(f, f->heap[child + 1], f->heap[child]))
        {
            child += 1;
        }
        if (!Sooner(f, f->heap[child], last))
        {
            break;
        }
        Place(f, at, f->heap[child]);
        at = child;
    }
    if (f->heapCount > 0)
    {
        Place(f, at, last);
    }
    f->places[top] = MNAV_FLOW_DONE;
    return top;
}

void mnavFlowLower(mnavFlowField* f, int32_t cell, double cost, int32_t next)
{
    f->costs[cell] = cost;
    f->next[cell] = next;
    if (f->places[cell] < 0)
    {
        Place(f, f->heapCount++, cell);
    }
    SiftUp(f, f->places[cell]);
    if (f->repairing && (f->marks[cell] & mnav_flowTouched) == 0)
    {
        f->marks[cell] |= mnav_flowTouched;
        f->touched[f->touchedCount++] = cell;
    }
}

// The eight directions, in the order a cell's neighbours are tried.
static const mnavCell s_steps[8] = {{1, 0}, {0, 1},  {-1, 0},  {0, -1},
                                    {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};

bool mnavFlowNeighbor(const mnavFlowSearch* s, int32_t cell, int32_t d, int32_t* other,
                      double* length)
{
    const mnavFlowField* f = s->field;
    int32_t width = f->region.width;
    int32_t x = f->region.x + cell % width;
    int32_t y = f->region.y + cell / width;
    int32_t dx = s_steps[d].x;
    int32_t dy = s_steps[d].y;
    bool diagonal = dx != 0 && dy != 0;
    if (!mnavFlowOpen(s, x + dx, y + dy) ||
        (diagonal && !(mnavFlowOpen(s, x + dx, y) && mnavFlowOpen(s, x, y + dy))))
    {
        return false;
    }
    *other = (y + dy - f->region.y) * width + (x + dx - f->region.x);
    *length = (diagonal ? DIAGONAL : 1.0) * (double)f->cellSize;
    return true;
}

double mnavFlowStepCost(const mnavFlowSearch* s, int32_t from, int32_t to, double length)
{
    return length * (mnavFlowAreaCost(s, to) + mnavFlowAreaCost(s, from)) * 0.5;
}

// Reaches the neighbours of a cell done: each may step to it. A build
// passes the cells done, which no later cost can lower; a repair may
// lower cells done before it began. The same sums as mnavFlowStepCost,
// with the cell's own area cost read once.
static void Expand(const mnavFlowSearch* s, int32_t cell)
{
    mnavFlowField* f = s->field;
    int32_t width = f->region.width;
    int32_t x = f->region.x + cell % width;
    int32_t y = f->region.y + cell / width;
    double here = mnavFlowAreaCost(s, cell);
    for (int32_t d = 0; d < 8; ++d)
    {
        int32_t dx = s_steps[d].x;
        int32_t dy = s_steps[d].y;
        bool diagonal = dx != 0 && dy != 0;
        if (!mnavFlowOpen(s, x + dx, y + dy) ||
            (diagonal && !(mnavFlowOpen(s, x + dx, y) && mnavFlowOpen(s, x, y + dy))))
        {
            continue;
        }
        int32_t other = cell + dy * width + dx;
        if (f->places[other] == MNAV_FLOW_DONE && !f->repairing)
        {
            continue;
        }
        mnavAreaType area = s->areas[(size_t)(y + dy) * (size_t)f->gridWidth + (size_t)(x + dx)];
        double length = (diagonal ? DIAGONAL : 1.0) * (double)f->cellSize;
        double cost = f->costs[cell] + length * ((double)f->filter.costs[area] + here) * 0.5;
        if (cost < f->costs[other])
        {
            mnavFlowLower(f, other, cost, cell);
        }
    }
}

static bool GoodGrid(const mnavGrid* grid)
{
    return grid->areas != nullptr && grid->width >= 1 && grid->width <= MNAV_MAX_GRID_SIDE &&
           grid->height >= 1 && grid->height <= MNAV_MAX_GRID_SIDE && isfinite(grid->cellSize) &&
           grid->cellSize > 0.0f;
}

static bool GoodGoals(const mnavGrid* grid, const mnavCell* goals, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        if (goals[i].x < 0 || goals[i].y < 0 || goals[i].x >= grid->width ||
            goals[i].y >= grid->height)
        {
            return false;
        }
    }
    return true;
}

static bool GoodRegion(const mnavGrid* grid, const mnavFlowRegion* r)
{
    return r->x >= 0 && r->y >= 0 && r->width >= 1 && r->height >= 1 &&
           r->x <= grid->width - r->width && r->y <= grid->height - r->height;
}

static mnavResult Check(mnavFlowField* field, const mnavGrid* grid, const mnavFlowRegion* region,
                        const mnavCell* goals, int32_t goalCount)
{
    if (field == nullptr || grid == nullptr || goalCount < 0 ||
        (goalCount > 0 && goals == nullptr) || !GoodGrid(grid) ||
        (region != nullptr && !GoodRegion(grid, region)) || !GoodGoals(grid, goals, goalCount))
    {
        return mnav_errorInvalid;
    }
    int64_t cells = region != nullptr ? (int64_t)region->width * (int64_t)region->height
                                      : (int64_t)grid->width * (int64_t)grid->height;
    return cells > (int64_t)field->def.cells ? mnav_errorLimit : mnav_success;
}

mnavResult mnavBeginFlowField(mnavFlowField* field, const mnavGrid* grid,
                              const mnavQueryFilter* filter, const mnavFlowRegion* region,
                              const mnavCell* goals, int32_t goalCount)
{
    if (field != nullptr)
    {
        field->state = mnav_flowNone;
    }
    mnavResult result = Check(field, grid, region, goals, goalCount);
    const mnavQueryFilter* usable = nullptr;
    if (result == mnav_success)
    {
        result = mnavCheckFilter(filter, &usable);
    }
    if (result != mnav_success)
    {
        return result;
    }
    field->region = region != nullptr ? *region : (mnavFlowRegion){0, 0, grid->width, grid->height};
    field->gridWidth = grid->width;
    field->gridHeight = grid->height;
    field->cellSize = grid->cellSize;
    field->filter = *usable;
    mnavFlowSearch s = {field, grid->areas};
    int32_t cells = field->region.width * field->region.height;
    for (int32_t c = 0; c < cells; ++c)
    {
        field->costs[c] = (double)INFINITY;
        field->next[c] = c;
        field->places[c] = MNAV_FLOW_NOT_OPEN;
    }
    field->heapCount = 0;
    field->goalCount = 0;
    field->repairing = false;
    for (int32_t i = 0; i < goalCount; ++i)
    {
        if (!mnavFlowOpen(&s, goals[i].x, goals[i].y))
        {
            continue;
        }
        int32_t cell =
            (goals[i].y - field->region.y) * field->region.width + (goals[i].x - field->region.x);
        if (field->costs[cell] != 0.0)
        {
            mnavFlowLower(field, cell, 0.0, cell);
            field->goals[field->goalCount++] = cell;
        }
    }
    field->state = mnav_flowWorking;
    return mnav_success;
}

bool mnavFlowSameGrid(const mnavFlowField* f, const mnavGrid* grid)
{
    return grid->areas != nullptr && grid->width == f->gridWidth && grid->height == f->gridHeight &&
           grid->cellSize == f->cellSize;
}

mnavResult mnavContinueFlowField(mnavFlowField* field, const mnavGrid* grid, int32_t cells,
                                 bool* endedOut)
{
    if (field == nullptr || grid == nullptr || field->state == mnav_flowNone || cells < 1 ||
        !mnavFlowSameGrid(field, grid))
    {
        return mnav_errorInvalid;
    }
    mnavFlowSearch s = {field, grid->areas};
    int32_t n = 0;
    for (; n < cells && field->heapCount > 0; ++n)
    {
        Expand(&s, Pop(field));
    }
    if (field->heapCount == 0 && field->repairing)
    {
        mnavFlowFix(&s, cells - n);
    }
    bool ended = field->heapCount == 0 && !field->repairing;
    field->state = ended ? mnav_flowEnded : mnav_flowWorking;
    if (endedOut != nullptr)
    {
        *endedOut = ended;
    }
    return mnav_success;
}

mnavResult mnavBuildFlowField(mnavFlowField* field, const mnavGrid* grid,
                              const mnavQueryFilter* filter, const mnavCell* goals,
                              int32_t goalCount)
{
    mnavResult result = mnavBeginFlowField(field, grid, filter, nullptr, goals, goalCount);
    return result == mnav_success ? mnavContinueFlowField(field, grid, INT32_MAX, nullptr) : result;
}

mnavResult mnavFlowAt(const mnavFlowField* field, mnavCell cell, mnavFlow* flowOut)
{
    if (field == nullptr || flowOut == nullptr || field->state == mnav_flowNone)
    {
        return mnav_errorInvalid;
    }
    const mnavFlowRegion* r = &field->region;
    if (cell.x < r->x || cell.y < r->y || cell.x >= r->x + r->width || cell.y >= r->y + r->height)
    {
        return mnav_errorInvalid;
    }
    if (field->state != mnav_flowEnded)
    {
        return mnav_errorStale;
    }
    int32_t at = (cell.y - r->y) * r->width + (cell.x - r->x);
    int32_t next = field->next[at];
    *flowOut = (mnavFlow){field->costs[at], {r->x + next % r->width, r->y + next / r->width}};
    return mnav_success;
}

mnavResult mnavDebugFlowField(const mnavFlowField* field, double height, mnavDebugBuffer* buffer)
{
    if (field == nullptr || !mnavGoodBuffer(buffer) || !isfinite(height))
    {
        return mnav_errorInvalid;
    }
    if (field->state == mnav_flowWorking)
    {
        return mnav_errorStale;
    }
    const mnavFlowRegion* r = &field->region;
    int32_t cells = field->state == mnav_flowEnded ? r->width * r->height : 0;
    double cellSize = (double)field->cellSize;
    for (int32_t c = 0; c < cells; ++c)
    {
        int32_t next = field->next[c];
        if (next == c)
        {
            continue;
        }
        int32_t column = c % r->width;
        int32_t row = c / r->width;
        int32_t nextColumn = next % r->width;
        int32_t nextRow = next / r->width;
        double x = ((double)(r->x + column) + 0.5) * cellSize;
        double z = ((double)(r->y + row) + 0.5) * cellSize;
        double dx = (double)(nextColumn - column);
        double dz = (double)(nextRow - row);
        double scale = 0.4 * cellSize / sqrt(dx * dx + dz * dz);
        dx *= scale;
        dz *= scale;
        mnavPos3 tail = {x - dx, height, z - dz};
        mnavPos3 head = {x + dx, height, z + dz};
        mnavDrawLine(buffer, tail, head, mnav_debugFlow, 0);
        // Barbs back from the head, a quarter turned either way.
        mnavDrawLine(buffer, head,
                     (mnavPos3){head.x - 0.5 * (dx - dz), height, head.z - 0.5 * (dz + dx)},
                     mnav_debugFlow, 0);
        mnavDrawLine(buffer, head,
                     (mnavPos3){head.x - 0.5 * (dx + dz), height, head.z - 0.5 * (dz - dx)},
                     mnav_debugFlow, 0);
    }
    return mnavDrawResult(buffer);
}
