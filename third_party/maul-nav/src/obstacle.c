// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Avoidance obstacles (mnav-0006). Edges give RVO2's obstacle lines (RVO2
// src/Agent.cc, computeNewVelocity, lines 274 to 506; the edge choice of
// src/KdTree.cc, queryObstacleTreeRecursive), in binary64. A moving
// obstacle is a static one in its own frame: its lines are built from the
// agent's velocity relative to it, then moved by its velocity. A circle
// gives the line of a disc that does not give way.

#include "obstacle.h"

#include "orca.h"

#include "maul-nav/avoidance.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// The largest cell index, as for agents.
#define CELL_LIMIT 0x1p52

// How far inside a line an obstacle's cut-off points may lie and still
// count as covered by it.
#define COVERED 1e-9

static bool Finite(mnavPos2 p)
{
    return isfinite(p.x) && isfinite(p.y);
}

static bool GoodPoint(mnavPos2 p)
{
    return Finite(p) && fabs(p.x) <= MNAV_MAX_AVOIDANCE_COORDINATE &&
           fabs(p.y) <= MNAV_MAX_AVOIDANCE_COORDINATE;
}

// det(a - c, b - a): more than 0 when c lies left of a to b.
static double LeftOf(mnavPos2 a, mnavPos2 b, mnavPos2 c)
{
    return mnavDet2(mnavSub2(a, c), mnavSub2(b, a));
}

// Whether an obstacle is well formed: finite points, a radius for one
// point alone, edges of some length, a polygon counterclockwise.
static bool GoodObstacle(const mnavObstacle* o)
{
    if (o->points == nullptr || o->pointCount < 1 || !Finite(o->velocity) ||
        fabs(o->velocity.x) > MNAV_MAX_AVOIDANCE_SPEED ||
        fabs(o->velocity.y) > MNAV_MAX_AVOIDANCE_SPEED || !isfinite(o->radius) ||
        (o->pointCount == 1) != (o->radius > 0.0) || o->radius < 0.0 ||
        o->radius > MNAV_MAX_AVOIDANCE_RADIUS)
    {
        return false;
    }
    double twiceArea = 0.0;
    for (int32_t i = 0; i < o->pointCount; ++i)
    {
        mnavPos2 a = o->points[i];
        mnavPos2 b = o->points[(i + 1) % o->pointCount];
        if (!GoodPoint(a) || (o->pointCount > 1 && a.x == b.x && a.y == b.y))
        {
            return false;
        }
        twiceArea += mnavDet2(a, b);
    }
    return o->pointCount < 3 || twiceArea > 0.0;
}

mnavResult mnavBuildObstacles(const mnavObstacle* obstacles, int32_t count,
                              mnavObstacleVertex* vertices, int32_t capacity, int32_t* vertexCount)
{
    int32_t n = 0;
    for (int32_t k = 0; k < count; ++k)
    {
        const mnavObstacle* o = &obstacles[k];
        if (!GoodObstacle(o))
        {
            return mnav_errorInvalid;
        }
        if (o->pointCount > capacity - n)
        {
            return mnav_errorLimit;
        }
        int32_t c = o->pointCount;
        for (int32_t i = 0; i < c; ++i)
        {
            mnavPos2 here = o->points[i];
            mnavPos2 next = o->points[(i + 1) % c];
            mnavPos2 previous = o->points[(i + c - 1) % c];
            mnavPos2 direction =
                c > 1 ? mnavNormalize2(mnavSub2(next, here)) : (mnavPos2){0.0, 0.0};
            bool convex = c < 3 || LeftOf(previous, here, next) >= 0.0;
            vertices[n + i] = (mnavObstacleVertex){
                here, direction,       o->velocity,         o->radius, o->id,
                i,    n + (i + 1) % c, n + (i + c - 1) % c, convex,    o->layers};
        }
        n += c;
    }
    *vertexCount = n;
    return mnav_success;
}

static bool NearBefore(const mnavObstacleVertex* vertices, mnavObstacleNear a, mnavObstacleNear b)
{
    if (a.distance != b.distance)
    {
        return a.distance < b.distance;
    }
    const mnavObstacleVertex* va = &vertices[a.vertex];
    const mnavObstacleVertex* vb = &vertices[b.vertex];
    if (va->id != vb->id)
    {
        return va->id < vb->id;
    }
    return va->index != vb->index ? va->index < vb->index : a.vertex < b.vertex;
}

// The squared distance from an agent to a vertex's circle or edge, or a
// negative value when it does not see it within the range.
static double Reach(const mnavAgent* agent, const mnavObstacleVertex* vertices, int32_t v,
                    double horizon)
{
    const mnavObstacleVertex* o = &vertices[v];
    double speed = sqrt(mnavDot2(o->velocity, o->velocity));
    double range = horizon * (agent->maxSpeed + speed) + agent->radius;
    mnavPos2 p = agent->position;
    if (o->radius > 0.0)
    {
        double gap = sqrt(mnavDot2(mnavSub2(o->point, p), mnavSub2(o->point, p))) - o->radius;
        gap = gap > 0.0 ? gap : 0.0;
        return gap < range ? gap * gap : -1.0;
    }
    mnavPos2 a = o->point;
    mnavPos2 b = vertices[o->next].point;
    mnavPos2 edge = mnavSub2(b, a);
    double length = mnavDot2(edge, edge);
    double left = LeftOf(a, b, p);
    // Only from outside, as RVO2's tree query: the agent on the edge's
    // right.
    if (left >= 0.0 || left * left / length >= range * range)
    {
        return -1.0;
    }
    double r = mnavDot2(mnavSub2(p, a), edge) / length;
    mnavPos2 at = r < 0.0 ? a : (r > 1.0 ? b : mnavAdd2(a, mnavScale2(edge, r)));
    double distance = mnavDot2(mnavSub2(p, at), mnavSub2(p, at));
    return distance < range * range ? distance : -1.0;
}

// The bounds of vertex v's circle or edge.
static void Bounds(const mnavObstacleVertex* vertices, int32_t v, mnavPos2* low, mnavPos2* high)
{
    const mnavObstacleVertex* o = &vertices[v];
    mnavPos2 a = o->point;
    mnavPos2 b = o->radius > 0.0 ? o->point : vertices[o->next].point;
    *low = (mnavPos2){(a.x < b.x ? a.x : b.x) - o->radius, (a.y < b.y ? a.y : b.y) - o->radius};
    *high = (mnavPos2){(a.x > b.x ? a.x : b.x) + o->radius, (a.y > b.y ? a.y : b.y) + o->radius};
}

// The cell holding v, saturated as for agents.
static int64_t CellOf(double v, double size)
{
    double cell = floor(v / size);
    return cell < -CELL_LIMIT ? (int64_t)-CELL_LIMIT
                              : (cell > CELL_LIMIT ? (int64_t)CELL_LIMIT : (int64_t)cell);
}

// The entries vertices' bounds take at a cell size, counted in binary64
// since a bound may span more cells than 64 bits count.
static double Entries(const mnavObstacleVertex* vertices, int32_t count, double size)
{
    double total = 0.0;
    for (int32_t v = 0; v < count; ++v)
    {
        mnavPos2 low;
        mnavPos2 high;
        Bounds(vertices, v, &low, &high);
        total += (double)(CellOf(high.x, size) - CellOf(low.x, size) + 1) *
                 (double)(CellOf(high.y, size) - CellOf(low.y, size) + 1);
    }
    return total;
}

static bool CellBefore(const mnavObstacleCell* a, const mnavObstacleCell* b)
{
    if (a->x != b->x)
    {
        return a->x < b->x;
    }
    return a->y != b->y ? a->y < b->y : a->vertex < b->vertex;
}

// A stable bottom-up merge sort through the scratch.
static void SortCells(mnavObstacleCell* cells, mnavObstacleCell* scratch, int32_t count)
{
    mnavObstacleCell* from = cells;
    mnavObstacleCell* to = scratch;
    for (int32_t width = 1; width < count; width *= 2)
    {
        for (int32_t start = 0; start < count; start += 2 * width)
        {
            int32_t middle = start + width < count ? start + width : count;
            int32_t end = start + 2 * width < count ? start + 2 * width : count;
            int32_t i = start;
            int32_t j = middle;
            for (int32_t k = start; k < end; ++k)
            {
                bool left = i < middle && (j >= end || !CellBefore(&from[j], &from[i]));
                to[k] = left ? from[i++] : from[j++];
            }
        }
        mnavObstacleCell* swap = from;
        from = to;
        to = swap;
    }
    for (int32_t k = 0; from != cells && k < count; ++k)
    {
        cells[k] = from[k];
    }
}

void mnavBuildObstacleGrid(mnavObstacleGrid* grid, const mnavObstacleVertex* vertices,
                           int32_t vertexCount, double size)
{
    // Long edges on small cells take many entries: the cells double until
    // they fit, which they do once no bound spans more than two cells.
    while (Entries(vertices, vertexCount, size) > (double)grid->capacity)
    {
        size *= 2.0;
    }
    grid->size = size;
    grid->count = 0;
    grid->fastest = 0.0;
    for (int32_t v = 0; v < vertexCount; ++v)
    {
        mnavPos2 low;
        mnavPos2 high;
        Bounds(vertices, v, &low, &high);
        for (int64_t x = CellOf(low.x, size); x <= CellOf(high.x, size); ++x)
        {
            for (int64_t y = CellOf(low.y, size); y <= CellOf(high.y, size); ++y)
            {
                grid->cells[grid->count++] = (mnavObstacleCell){x, y, v};
            }
        }
        double speed = sqrt(mnavDot2(vertices[v].velocity, vertices[v].velocity));
        grid->fastest = speed > grid->fastest ? speed : grid->fastest;
        grid->stamps[v] = -1;
    }
    SortCells(grid->cells, grid->scratch, grid->count);
    grid->stamp = 0;
}

// The first entry at or after cell (x, y).
static int32_t FirstAt(const mnavObstacleGrid* grid, int64_t x, int64_t y)
{
    int32_t low = 0;
    int32_t high = grid->count;
    while (low < high)
    {
        int32_t middle = low + (high - low) / 2;
        const mnavObstacleCell* c = &grid->cells[middle];
        bool before = c->x < x || (c->x == x && c->y < y);
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low;
}

// Keeps a candidate among the nearest, up to the limit; returns the count.
static int32_t Keep(const mnavObstacleVertex* vertices, mnavObstacleNear* list, int32_t count,
                    int32_t limit, mnavObstacleNear candidate)
{
    if (count == limit && !NearBefore(vertices, candidate, list[count - 1]))
    {
        return count;
    }
    int32_t i = count < limit ? count++ : count - 1;
    while (i > 0 && NearBefore(vertices, candidate, list[i - 1]))
    {
        list[i] = list[i - 1];
        i -= 1;
    }
    list[i] = candidate;
    return count;
}

int32_t mnavNearInGrid(mnavObstacleGrid* grid, const mnavObstacleVertex* vertices, mnavPos2 p,
                       double reach, mnavObstacleMeasure measure, const void* context,
                       mnavObstacleNear* list, int32_t limit)
{
    int64_t y0 = CellOf(p.y - reach, grid->size);
    int64_t y1 = CellOf(p.y + reach, grid->size);
    int32_t stamp = grid->stamp++;
    int32_t count = 0;
    int64_t x1 = CellOf(p.x + reach, grid->size);
    for (int64_t x = CellOf(p.x - reach, grid->size); x <= x1; ++x)
    {
        // Columns with no entries are skipped, so that a reach of many
        // cells costs the entries it covers, not its cells.
        int32_t first = FirstAt(grid, x, y0);
        if (first == grid->count)
        {
            break;
        }
        if (grid->cells[first].x > x)
        {
            x = grid->cells[first].x - 1;
            continue;
        }
        for (int32_t k = first; k < grid->count && grid->cells[k].x == x && grid->cells[k].y <= y1;
             ++k)
        {
            int32_t v = grid->cells[k].vertex;
            if (grid->stamps[v] == stamp)
            {
                continue;
            }
            grid->stamps[v] = stamp;
            double distance = measure(context, v);
            if (distance >= 0.0)
            {
                count = Keep(vertices, list, count, limit, (mnavObstacleNear){distance, v});
            }
        }
    }
    return count;
}

// An agent's view of the obstacles, for measuring them.
typedef struct Seen
{
    const mnavAgent* agent;
    const mnavObstacleVertex* vertices;
    double horizon;
} Seen;

static double MeasureSeen(const void* context, int32_t v)
{
    const Seen* seen = context;
    if ((seen->vertices[v].layers & seen->agent->ignores) != 0)
    {
        return -1.0;
    }
    return Reach(seen->agent, seen->vertices, v, seen->horizon);
}

int32_t mnavNearObstacles(const mnavAgent* agent, const mnavObstacleVertex* vertices,
                          mnavObstacleGrid* grid, double horizon, mnavObstacleNear* list,
                          int32_t limit)
{
    // Every circle and edge within any vertex's reach lies in the box.
    double reach = horizon * (agent->maxSpeed + grid->fastest) + agent->radius;
    Seen seen = {agent, vertices, horizon};
    return mnavNearInGrid(grid, vertices, agent->position, reach, MeasureSeen, &seen, list, limit);
}

// The agent seen from an edge's frame: positions relative to it, its
// velocity relative to the edge's, and the horizon's inverse.
typedef struct View
{
    const mnavObstacleVertex* vertices;
    mnavPos2 position;
    mnavPos2 velocity;
    double radius;
    double inverse;
} View;

// Whether the lines before already cover the edge from o1 to o2: both
// its cut-off points lie beyond one of them.
static bool Covered(const View* w, int32_t o1, int32_t o2, const mnavLine* lines, int32_t count)
{
    const mnavObstacleVertex* v1 = &w->vertices[o1];
    mnavPos2 shift = v1->velocity;
    mnavPos2 c1 = mnavAdd2(mnavScale2(mnavSub2(v1->point, w->position), w->inverse), shift);
    mnavPos2 c2 =
        mnavAdd2(mnavScale2(mnavSub2(w->vertices[o2].point, w->position), w->inverse), shift);
    double margin = w->inverse * w->radius;
    for (int32_t j = 0; j < count; ++j)
    {
        if (mnavDet2(mnavSub2(c1, lines[j].point), lines[j].direction) - margin >= -COVERED &&
            mnavDet2(mnavSub2(c2, lines[j].point), lines[j].direction) - margin >= -COVERED)
        {
            return true;
        }
    }
    return false;
}

// The unit vector along a, or the fallback when a has no length. The
// callers keep exact zeros away (an agent on a corner is taken as
// touching the edge, and a velocity on a cut-off point is not projected
// on it), so the fallback only guards their rounding.
static mnavPos2 UnitOr(mnavPos2 a, mnavPos2 fallback)
{
    mnavPos2 unit = mnavNormalize2(a);
    return unit.x == 0.0 && unit.y == 0.0 ? fallback : unit;
}

// The leg from a convex vertex at relative position rel, on the left
// (left true) or right of the agent's view.
static mnavPos2 Leg(mnavPos2 rel, double radius, bool left)
{
    double distanceSq = mnavDot2(rel, rel);
    double leg = sqrt(distanceSq - radius * radius);
    return left ? (mnavPos2){(rel.x * leg - rel.y * radius) / distanceSq,
                             (rel.x * radius + rel.y * leg) / distanceSq}
                : (mnavPos2){(rel.x * leg + rel.y * radius) / distanceSq,
                             (-rel.x * radius + rel.y * leg) / distanceSq};
}

// The line through a cut-off point offset by the radius along the normal
// of a direction.
static mnavLine Offset(const View* w, mnavPos2 cutoff, mnavPos2 direction)
{
    double by = w->radius * w->inverse;
    return (mnavLine){mnavAdd2(cutoff, mnavScale2((mnavPos2){-direction.y, direction.x}, by)),
                      direction};
}

// The legs of an edge's velocity obstacle, o1 and o2 possibly made one
// vertex when the agent sees the edge end on; false when RVO2 ignores it.
typedef struct Legs
{
    int32_t o1;
    int32_t o2;
    mnavPos2 left;
    mnavPos2 right;
    bool leftForeign;
    bool rightForeign;
} Legs;

static bool FindLegs(const View* w, double s, double distSqLine, Legs* legs)
{
    const mnavObstacleVertex* vs = w->vertices;
    double radiusSq = w->radius * w->radius;
    if (s < 0.0 && distSqLine <= radiusSq)
    {
        // Seen end on: the left vertex makes the velocity obstacle.
        if (!vs[legs->o1].convex)
        {
            return false;
        }
        legs->o2 = legs->o1;
        mnavPos2 rel = mnavSub2(vs[legs->o1].point, w->position);
        legs->left = Leg(rel, w->radius, true);
        legs->right = Leg(rel, w->radius, false);
    }
    else if (s > 1.0 && distSqLine <= radiusSq)
    {
        // Seen end on: the right vertex.
        if (!vs[legs->o2].convex)
        {
            return false;
        }
        legs->o1 = legs->o2;
        mnavPos2 rel = mnavSub2(vs[legs->o2].point, w->position);
        legs->left = Leg(rel, w->radius, true);
        legs->right = Leg(rel, w->radius, false);
    }
    else
    {
        // A non-convex vertex's leg extends the cut-off line.
        legs->left = vs[legs->o1].convex
                         ? Leg(mnavSub2(vs[legs->o1].point, w->position), w->radius, true)
                         : mnavScale2(vs[legs->o1].direction, -1.0);
        legs->right = vs[legs->o2].convex
                          ? Leg(mnavSub2(vs[legs->o2].point, w->position), w->radius, false)
                          : vs[legs->o1].direction;
    }
    // A convex vertex's leg never points into the neighbouring edge: that
    // edge's cut-off line stands in, and a velocity projected on such a
    // foreign leg gives no line.
    const mnavObstacleVertex* leftNeighbor = &vs[vs[legs->o1].previous];
    legs->leftForeign = vs[legs->o1].convex &&
                        mnavDet2(legs->left, mnavScale2(leftNeighbor->direction, -1.0)) >= 0.0;
    legs->left = legs->leftForeign ? mnavScale2(leftNeighbor->direction, -1.0) : legs->left;
    legs->rightForeign =
        vs[legs->o2].convex && mnavDet2(legs->right, vs[legs->o2].direction) <= 0.0;
    legs->right = legs->rightForeign ? vs[legs->o2].direction : legs->right;
    return true;
}

// Projects the velocity on the edge's truncated velocity obstacle and
// writes the line, or returns false when it lands on a foreign leg.
static bool Project(const View* w, const Legs* legs, mnavLine* line)
{
    const mnavObstacleVertex* vs = w->vertices;
    mnavPos2 leftCutoff = mnavScale2(mnavSub2(vs[legs->o1].point, w->position), w->inverse);
    mnavPos2 rightCutoff = mnavScale2(mnavSub2(vs[legs->o2].point, w->position), w->inverse);
    mnavPos2 cutoff = mnavSub2(rightCutoff, leftCutoff);
    bool one = legs->o1 == legs->o2;
    mnavPos2 v = w->velocity;
    double t = one ? 0.5 : mnavDot2(mnavSub2(v, leftCutoff), cutoff) / mnavDot2(cutoff, cutoff);
    double tLeft = mnavDot2(mnavSub2(v, leftCutoff), legs->left);
    double tRight = mnavDot2(mnavSub2(v, rightCutoff), legs->right);
    // With no direction from the cut-off point, the edge's outward normal
    // stands in.
    mnavPos2 outward = {vs[legs->o1].direction.y, -vs[legs->o1].direction.x};
    if ((t < 0.0 && tLeft < 0.0) || (one && tLeft < 0.0 && tRight < 0.0))
    {
        mnavPos2 unit = UnitOr(mnavSub2(v, leftCutoff), outward);
        *line = (mnavLine){mnavAdd2(leftCutoff, mnavScale2(unit, w->radius * w->inverse)),
                           (mnavPos2){unit.y, -unit.x}};
        return true;
    }
    if (t > 1.0 && tRight < 0.0)
    {
        mnavPos2 unit = UnitOr(mnavSub2(v, rightCutoff), outward);
        *line = (mnavLine){mnavAdd2(rightCutoff, mnavScale2(unit, w->radius * w->inverse)),
                           (mnavPos2){unit.y, -unit.x}};
        return true;
    }
    mnavPos2 toCut = mnavSub2(v, mnavAdd2(leftCutoff, mnavScale2(cutoff, t)));
    mnavPos2 toLeft = mnavSub2(v, mnavAdd2(leftCutoff, mnavScale2(legs->left, tLeft)));
    mnavPos2 toRight = mnavSub2(v, mnavAdd2(rightCutoff, mnavScale2(legs->right, tRight)));
    double cut = t < 0.0 || t > 1.0 || one ? (double)INFINITY : mnavDot2(toCut, toCut);
    double left = tLeft < 0.0 ? (double)INFINITY : mnavDot2(toLeft, toLeft);
    double right = tRight < 0.0 ? (double)INFINITY : mnavDot2(toRight, toRight);
    if (cut <= left && cut <= right)
    {
        *line = Offset(w, leftCutoff, mnavScale2(vs[legs->o1].direction, -1.0));
        return true;
    }
    if (left <= right)
    {
        *line = Offset(w, leftCutoff, legs->left);
        return !legs->leftForeign;
    }
    *line = Offset(w, rightCutoff, mnavScale2(legs->right, -1.0));
    return !legs->rightForeign;
}

// The line of an edge the agent already touches, or false when none: a
// vertex RVO2 leaves to its neighbour, or no collision.
static bool Touching(const View* w, int32_t o1, int32_t o2, double s, double distSqLine,
                     bool* handled, mnavLine* line)
{
    const mnavObstacleVertex* vs = w->vertices;
    mnavPos2 rel1 = mnavSub2(vs[o1].point, w->position);
    mnavPos2 rel2 = mnavSub2(vs[o2].point, w->position);
    double radiusSq = w->radius * w->radius;
    // An agent on the vertex itself keeps out of the edge, as one touching
    // its middle does.
    mnavPos2 away = mnavScale2(vs[o1].direction, -1.0);
    *handled = true;
    if (s < 0.0 && mnavDot2(rel1, rel1) <= radiusSq)
    {
        *line = (mnavLine){{0.0, 0.0}, UnitOr((mnavPos2){-rel1.y, rel1.x}, away)};
        return vs[o1].convex;
    }
    if (s > 1.0 && mnavDot2(rel2, rel2) <= radiusSq)
    {
        *line = (mnavLine){{0.0, 0.0}, UnitOr((mnavPos2){-rel2.y, rel2.x}, away)};
        return vs[o2].convex && mnavDet2(rel2, vs[o2].direction) >= 0.0;
    }
    if (s >= 0.0 && s <= 1.0 && distSqLine <= radiusSq)
    {
        *line = (mnavLine){{0.0, 0.0}, mnavScale2(vs[o1].direction, -1.0)};
        return true;
    }
    *handled = false;
    return false;
}

// The line of the edge from vertex o1, in the agent's frame, or false.
static bool EdgeLine(const View* w, int32_t o1, mnavLine* line)
{
    const mnavObstacleVertex* vs = w->vertices;
    int32_t o2 = vs[o1].next;
    mnavPos2 rel1 = mnavSub2(vs[o1].point, w->position);
    mnavPos2 edge = mnavSub2(vs[o2].point, vs[o1].point);
    double s = mnavDot2(mnavScale2(rel1, -1.0), edge) / mnavDot2(edge, edge);
    mnavPos2 off = mnavSub2(mnavScale2(rel1, -1.0), mnavScale2(edge, s));
    double distSqLine = mnavDot2(off, off);
    bool handled = false;
    bool touching = Touching(w, o1, o2, s, distSqLine, &handled, line);
    if (handled)
    {
        return touching;
    }
    Legs legs = {o1, o2, {0.0, 0.0}, {0.0, 0.0}, false, false};
    return FindLegs(w, s, distSqLine, &legs) && Project(w, &legs, line);
}

// The line of a circle: an agent that never gives way while apart. An
// agent already overlapping it is asked to leave it within the step, as
// overlapping agents are, but never faster than its maximum speed: the
// 3D program keeps obstacle lines, and one it could not keep would let
// the agent walk on in.
static mnavLine CircleLine(const mnavAgent* agent, const mnavObstacleVertex* o, double horizon,
                           double step)
{
    double combined = agent->radius + o->radius;
    mnavPos2 rel = mnavSub2(o->point, agent->position);
    double distanceSq = mnavDot2(rel, rel);
    if (distanceSq > combined * combined)
    {
        return mnavPairLine(agent->position, agent->velocity, o->point, o->velocity, combined, 1.0,
                            horizon, step, agent->id < o->id);
    }
    // On the center itself no way is out; a fixed one keeps it
    // deterministic.
    mnavPos2 away = UnitOr(mnavScale2(rel, -1.0), (mnavPos2){0.0, -1.0});
    double leave = mnavDot2(o->velocity, away) + (combined - sqrt(distanceSq)) / step;
    double offset = leave < agent->maxSpeed ? leave : agent->maxSpeed;
    return (mnavLine){mnavScale2(away, offset), (mnavPos2){away.y, -away.x}};
}

int32_t mnavObstacleLines(const mnavAgent* agent, const mnavObstacleVertex* vertices,
                          const mnavObstacleNear* near, int32_t nearCount, double horizon,
                          double step, mnavLine* lines)
{
    int32_t count = 0;
    for (int32_t k = 0; k < nearCount; ++k)
    {
        const mnavObstacleVertex* o = &vertices[near[k].vertex];
        if (o->radius > 0.0)
        {
            lines[count++] = CircleLine(agent, o, horizon, step);
            continue;
        }
        View w = {vertices, agent->position, mnavSub2(agent->velocity, o->velocity), agent->radius,
                  1.0 / horizon};
        if (Covered(&w, near[k].vertex, o->next, lines, count))
        {
            continue;
        }
        mnavLine line;
        if (EdgeLine(&w, near[k].vertex, &line))
        {
            line.point = mnavAdd2(line.point, o->velocity);
            lines[count++] = line;
        }
    }
    return count;
}
