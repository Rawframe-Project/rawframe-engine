// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flow-field repairs (mnav-0007), after Ramalingam and Reps' incremental
// shortest paths. Costs raised by what changed are those of the cells
// whose next-cell chains run through it: they are reset and given the
// best cost their neighbours offer. Lowered costs spread from new
// goals and the changed cells' neighbourhoods; the search then settles
// them, opening done cells again. A cell's cost is the fixed point of its
// neighbours' offers, whatever the order, and each touched cell and its
// neighbours then take the next cell a build chooses: the neighbour giving
// the cost exactly, the lowest by cost and then index. So a repair leaves
// the field a rebuild would make.

#include "flow.h"

#include "maul-nav/base.h"
#include "maul-nav/flow.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Marks a cell raised and lists it, once.
static void Raise(mnavFlowField* f, int32_t cell)
{
    if ((f->marks[cell] & mnav_flowRaised) == 0)
    {
        f->marks[cell] |= mnav_flowRaised | mnav_flowTouched;
        f->touched[f->touchedCount++] = cell;
    }
}

// The cell of the region at grid place (x, y), or -1 outside it.
static int32_t CellAt(const mnavFlowField* f, int32_t x, int32_t y)
{
    const mnavFlowRegion* r = &f->region;
    if (x < r->x || y < r->y || x >= r->x + r->width || y >= r->y + r->height)
    {
        return -1;
    }
    return (y - r->y) * r->width + (x - r->x);
}

// Raises a changed cell and the cells beside it whose diagonal step to
// their next cell passes its corner.
static void RaiseChanged(mnavFlowField* f, int32_t x, int32_t y)
{
    int32_t cell = CellAt(f, x, y);
    if (cell < 0)
    {
        return;
    }
    Raise(f, cell);
    static const int32_t sides[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    int32_t width = f->region.width;
    for (int32_t k = 0; k < 4; ++k)
    {
        int32_t cx = x + sides[k][0];
        int32_t cy = y + sides[k][1];
        int32_t c = CellAt(f, cx, cy);
        if (c < 0 || f->next[c] == c)
        {
            continue;
        }
        int32_t nx = f->region.x + f->next[c] % width;
        int32_t ny = f->region.y + f->next[c] / width;
        bool diagonal = nx != cx && ny != cy;
        if (diagonal && ((nx == x && cy == y) || (cx == x && ny == y)))
        {
            Raise(f, c);
        }
    }
}

// Raises the subtrees of the raised cells listed: every cell whose next
// cell is raised.
static void RaiseSubtrees(mnavFlowField* f)
{
    int32_t width = f->region.width;
    for (int32_t i = 0; i < f->touchedCount; ++i)
    {
        int32_t cell = f->touched[i];
        int32_t x = f->region.x + cell % width;
        int32_t y = f->region.y + cell / width;
        for (int32_t dy = -1; dy <= 1; ++dy)
        {
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                int32_t n = CellAt(f, x + dx, y + dy);
                if (n >= 0 && n != cell && f->next[n] == cell)
                {
                    Raise(f, n);
                }
            }
        }
    }
}

// Lowers a cell to the best cost its neighbours offer. Every cost held is
// that of a way the grid has, so any offer is one.
static void Offer(const mnavFlowSearch* s, int32_t cell)
{
    mnavFlowField* f = s->field;
    for (int32_t d = 0; d < 8; ++d)
    {
        int32_t n = 0;
        double length = 0.0;
        if (!mnavFlowNeighbor(s, cell, d, &n, &length) || !isfinite(f->costs[n]))
        {
            continue;
        }
        double cost = f->costs[n] + mnavFlowStepCost(s, n, cell, length);
        if (cost < f->costs[cell])
        {
            mnavFlowLower(f, cell, cost, n);
        }
    }
}

// Whether grid cell (x, y) is open: in the region and walkable.
static bool OpenCell(const mnavFlowSearch* s, int32_t cell)
{
    const mnavFlowField* f = s->field;
    return mnavFlowOpen(s, f->region.x + cell % f->region.width,
                        f->region.y + cell / f->region.width);
}

// Gives every open cell around a changed one the offers of its
// neighbours: an opened corner or a cheaper cell may lower them.
static void OfferAround(const mnavFlowSearch* s, int32_t x, int32_t y)
{
    for (int32_t dy = -1; dy <= 1; ++dy)
    {
        for (int32_t dx = -1; dx <= 1; ++dx)
        {
            int32_t cell = CellAt(s->field, x + dx, y + dy);
            if (cell >= 0 && OpenCell(s, cell))
            {
                Offer(s, cell);
            }
        }
    }
}

static mnavResult Check(const mnavFlowField* field, const mnavGrid* grid, const mnavCell* goals,
                        int32_t goalCount, const mnavCell* changed, int32_t changedCount)
{
    if (field == nullptr || grid == nullptr || goalCount < 0 ||
        (goalCount > 0 && goals == nullptr) || changedCount < 0 ||
        (changedCount > 0 && changed == nullptr) || field->state == mnav_flowNone ||
        !mnavFlowSameGrid(field, grid))
    {
        return mnav_errorInvalid;
    }
    for (int32_t i = 0; i < goalCount + changedCount; ++i)
    {
        mnavCell c = i < goalCount ? goals[i] : changed[i - goalCount];
        if (c.x < 0 || c.y < 0 || c.x >= grid->width || c.y >= grid->height)
        {
            return mnav_errorInvalid;
        }
    }
    return field->state == mnav_flowWorking ? mnav_errorStale : mnav_success;
}

// Marks the new goals, raises the old ones no longer goals, and keeps the
// new list.
static void SwapGoals(const mnavFlowSearch* s, const mnavCell* goals, int32_t goalCount)
{
    mnavFlowField* f = s->field;
    for (int32_t i = 0; i < goalCount; ++i)
    {
        int32_t cell = CellAt(f, goals[i].x, goals[i].y);
        if (cell >= 0 && mnavFlowOpen(s, goals[i].x, goals[i].y))
        {
            f->marks[cell] |= mnav_flowNewGoal;
        }
    }
    for (int32_t i = 0; i < f->goalCount; ++i)
    {
        if ((f->marks[f->goals[i]] & mnav_flowNewGoal) == 0)
        {
            Raise(f, f->goals[i]);
        }
    }
    f->goalCount = 0;
    for (int32_t i = 0; i < goalCount; ++i)
    {
        int32_t cell = CellAt(f, goals[i].x, goals[i].y);
        if (cell >= 0 && (f->marks[cell] & mnav_flowNewGoal) != 0)
        {
            f->marks[cell] &= (uint8_t)~mnav_flowNewGoal;
            f->goals[f->goalCount++] = cell;
        }
    }
}

mnavResult mnavUpdateFlowField(mnavFlowField* field, const mnavGrid* grid, const mnavCell* goals,
                               int32_t goalCount, const mnavCell* changed, int32_t changedCount)
{
    mnavResult result = Check(field, grid, goals, goalCount, changed, changedCount);
    if (result != mnav_success)
    {
        return result;
    }
    mnavFlowSearch s = {field, grid->areas};
    field->repairing = true;
    field->touchedCount = 0;
    field->fixed = 0;
    SwapGoals(&s, goals, goalCount);
    for (int32_t i = 0; i < changedCount; ++i)
    {
        RaiseChanged(field, changed[i].x, changed[i].y);
    }
    RaiseSubtrees(field);
    int32_t raised = field->touchedCount;
    for (int32_t i = 0; i < raised; ++i)
    {
        int32_t cell = field->touched[i];
        field->costs[cell] = (double)INFINITY;
        field->next[cell] = cell;
        field->places[cell] = MNAV_FLOW_NOT_OPEN;
    }
    for (int32_t i = 0; i < raised; ++i)
    {
        if (OpenCell(&s, field->touched[i]))
        {
            Offer(&s, field->touched[i]);
        }
    }
    for (int32_t i = 0; i < field->goalCount; ++i)
    {
        int32_t cell = field->goals[i];
        if (field->costs[cell] != 0.0)
        {
            mnavFlowLower(field, cell, 0.0, cell);
        }
    }
    for (int32_t i = 0; i < changedCount; ++i)
    {
        OfferAround(&s, changed[i].x, changed[i].y);
    }
    field->state = mnav_flowWorking;
    return mnav_success;
}

// The next cell a build gives a cell: itself at a goal or when no goal is
// reached, otherwise the neighbour whose offer is the cell's cost exactly,
// the lowest by cost and then by index.
static int32_t NextOf(const mnavFlowSearch* s, int32_t cell)
{
    const mnavFlowField* f = s->field;
    double cost = f->costs[cell];
    if (cost == 0.0 || !isfinite(cost))
    {
        return cell;
    }
    int32_t best = cell;
    for (int32_t d = 0; d < 8; ++d)
    {
        int32_t n = 0;
        double length = 0.0;
        if (!mnavFlowNeighbor(s, cell, d, &n, &length) ||
            f->costs[n] + mnavFlowStepCost(s, n, cell, length) != cost)
        {
            continue;
        }
        if (best == cell || f->costs[n] < f->costs[best] ||
            (f->costs[n] == f->costs[best] && n < best))
        {
            best = n;
        }
    }
    return best;
}

int32_t mnavFlowFix(const mnavFlowSearch* s, int32_t budget)
{
    mnavFlowField* f = s->field;
    int32_t width = f->region.width;
    int32_t done = 0;
    for (; done < budget && f->fixed < f->touchedCount; ++done)
    {
        int32_t cell = f->touched[f->fixed++];
        int32_t x = f->region.x + cell % width;
        int32_t y = f->region.y + cell / width;
        for (int32_t dy = -1; dy <= 1; ++dy)
        {
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                int32_t n = CellAt(f, x + dx, y + dy);
                if (n >= 0)
                {
                    f->next[n] = NextOf(s, n);
                }
            }
        }
    }
    if (f->fixed == f->touchedCount)
    {
        for (int32_t i = 0; i < f->touchedCount; ++i)
        {
            f->marks[f->touched[i]] = 0;
        }
        f->touchedCount = 0;
        f->repairing = false;
    }
    return done;
}
