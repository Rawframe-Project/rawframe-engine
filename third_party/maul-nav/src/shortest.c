// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The exact shortest-path search: Polyanya (Cui, Harabor and Grastien,
// 2017) over the navmesh's portals, with off-mesh links as roots of their
// own (mnav-0005).
//
// A node is an interval of a portal with a root that sees all of it: the
// start, a corner the way turns at, or a link's landing point. Expanding
// it pushes the interval through the polygon beyond: the parts of that
// polygon's portals the root sees through the interval keep the root, the
// parts beside them turn at the interval's end on that side when the end
// is a corner. Every point on a polygon's boundary where a wall may begin
// counts as a corner: a vertex or the end of a link along a tile side.
// The best cost known at each root, kept in the node table by the root's
// position, prunes a way to a root that is no cheaper.

#include "navmesh.h"
#include "offmesh.h"
#include "polymesh.h"
#include "query.h"
#include "query_filter.h"

#include "maul-nav/base.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// A node's tag: for an interval, the entry edge of the polygon it leads
// into in the low bits, and whether each end is a corner; else its kind.
enum
{
    EDGE_MASK = 7,
    CORNER_A = 8,
    CORNER_B = 16,
    KIND_START = -1,
    KIND_END = -2,
    KIND_TAKEOFF = -3,
    KIND_LANDING = -4,
};

// One search: the end it heads for, the one cost of every step on the
// ground, the heuristic's scale, and what stopped ways on.
typedef struct Shortest
{
    mnavQuery* query;
    const mnavNavmesh* navmesh;
    const mnavQueryFilter* filter;
    mnavPos3 end;
    int32_t endSlot;
    int32_t endPolygon;
    double cost;
    double scale;
    double limit;
    int32_t found;
    int32_t best;
    double bestAhead;
    // Nodes by root, interval and polygon, over the funnel's portal memory,
    // which this search never uses: a node the same as one before it
    // leads nowhere new. The node table's size, so at most half full.
    int32_t* made;
    bool outOfNodes;
    bool tooLong;
    bool notLoaded;
} Shortest;

// A root as a node sees it: where it stands, and the cost and length on
// the ground of the way to it.
typedef struct Seen
{
    mnavPos3 root;
    double cost;
    double length;
} Seen;

// A part of a polygon's edge that leads on: the edge's ends, the part
// from t0 to t1 along it, and the polygon beyond with its entry edge.
typedef struct Piece
{
    mnavPos3 p0;
    mnavPos3 p1;
    double t0;
    double t1;
    int32_t slot;
    int32_t polygon;
    int32_t entry;
} Piece;

// Twice the signed area of triangle o, a, b on the ground: positive when
// b lies left of the way from o to a.
static double Cross(mnavPos3 o, mnavPos3 a, mnavPos3 b)
{
    return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
}

static double Flat(mnavPos3 a, mnavPos3 b)
{
    double dx = a.x - b.x;
    double dz = a.z - b.z;
    return sqrt(dx * dx + dz * dz);
}

static double Distance(mnavPos3 a, mnavPos3 b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static mnavPos3 Lerp(mnavPos3 a, mnavPos3 b, double t)
{
    return (mnavPos3){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

static bool Same(mnavPos3 a, mnavPos3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// The point of segment a, b nearest p on the ground.
static mnavPos3 Nearest(mnavPos3 p, mnavPos3 a, mnavPos3 b)
{
    double dx = b.x - a.x;
    double dz = b.z - a.z;
    double length2 = dx * dx + dz * dz;
    double t = length2 > 0.0 ? ((p.x - a.x) * dx + (p.z - a.z) * dz) / length2 : 0.0;
    return Lerp(a, b, t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t));
}

// The shortest distance on the ground from root r through the interval
// from a to b to point e: an e on r's side of the interval's line is
// mirrored through it first, then the way is straight when it meets the
// interval and turns at the nearer end otherwise.
static double Through(mnavPos3 r, mnavPos3 a, mnavPos3 b, mnavPos3 e)
{
    double side = Cross(a, b, r);
    double eSide = Cross(a, b, e);
    double dx = b.x - a.x;
    double dz = b.z - a.z;
    double length2 = dx * dx + dz * dz;
    if (length2 > 0.0 && ((side > 0.0 && eSide > 0.0) || (side < 0.0 && eSide < 0.0)))
    {
        double t = ((e.x - a.x) * dx + (e.z - a.z) * dz) / length2;
        e.x = 2.0 * (a.x + t * dx) - e.x;
        e.z = 2.0 * (a.z + t * dz) - e.z;
    }
    if (Cross(r, a, e) >= 0.0 && Cross(r, b, e) <= 0.0)
    {
        return Flat(r, e);
    }
    double viaA = Flat(r, a) + Flat(a, e);
    double viaB = Flat(r, b) + Flat(b, e);
    return viaA < viaB ? viaA : viaB;
}

// The table cell for a root's position: the one holding a node with that
// root, or the empty cell where it would go.
static uint32_t RootCell(const mnavQuery* query, mnavPos3 root)
{
    uint64_t bits[3];
    memcpy(&bits[0], &root.x, sizeof(double));
    memcpy(&bits[1], &root.y, sizeof(double));
    memcpy(&bits[2], &root.z, sizeof(double));
    uint64_t h = 0x9E3779B97F4A7C15u;
    for (int32_t i = 0; i < 3; ++i)
    {
        h = (h ^ bits[i]) * 0xBF58476D1CE4E5B9u;
        h ^= h >> 31;
    }
    uint32_t cell = (uint32_t)h & query->tableMask;
    for (;;)
    {
        int32_t n = query->table[cell];
        if (n == MNAV_NO_NODE || Same(query->nodes[n].at, root))
        {
            return cell;
        }
        cell = (cell + 1) & query->tableMask;
    }
}

static uint64_t Mix(uint64_t h, double v)
{
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(double));
    h = (h ^ bits) * 0xBF58476D1CE4E5B9u;
    return h ^ (h >> 31);
}

static bool SameNode(const mnavSearchNode* a, const mnavSearchNode* b)
{
    return Same(a->at, b->at) && Same(a->a, b->a) && Same(a->b, b->b) && a->slot == b->slot &&
           a->polygon == b->polygon && a->tag == b->tag && a->low == b->low;
}

// The cell of s->made for a node: the one holding the same node, or the
// empty cell where it would go.
static uint32_t MadeCell(const Shortest* s, const mnavSearchNode* node)
{
    const mnavQuery* query = s->query;
    uint64_t h = 0x9E3779B97F4A7C15u ^ (uint64_t)(uint32_t)node->slot << 32 ^
                 (uint64_t)(uint32_t)node->polygon;
    const double values[9] = {node->at.x, node->at.y, node->at.z, node->a.x, node->a.y,
                              node->a.z,  node->b.x,  node->b.y,  node->b.z};
    for (int32_t i = 0; i < 9; ++i)
    {
        h = Mix(h, values[i]);
    }
    uint32_t cell = (uint32_t)h & query->tableMask;
    for (;;)
    {
        int32_t n = s->made[cell];
        if (n == MNAV_NO_NODE || SameNode(&query->nodes[n], node))
        {
            return cell;
        }
        cell = (cell + 1) & query->tableMask;
    }
}

// Whether a way to a root at a cost is no dearer than the best known.
static bool Cheapest(const mnavQuery* query, uint32_t cell, double cost)
{
    int32_t n = query->table[cell];
    return n == MNAV_NO_NODE || cost <= query->nodes[n].cost + 1e-9 * (1.0 + fabs(cost));
}

// Whether interval node made, whose root lies on its interval, comes back to a
// polygon an ancestor with the same root already led into: going round a
// root, the polygons about it form a ring when no wall meets it. A root
// on the interval's line but beyond the interval turns at its nearer
// end, a way of its own.
static bool Repeats(const mnavQuery* query, int32_t parent, const mnavSearchNode* made)
{
    double span = Flat(made->a, made->b);
    if (made->tag < 0 || fabs(Cross(made->a, made->b, made->at)) > 1e-9 * (1.0 + span * span) ||
        Flat(made->at, made->a) > span || Flat(made->at, made->b) > span)
    {
        return false;
    }
    for (int32_t n = parent; n != MNAV_NO_NODE && Same(query->nodes[n].at, made->at);
         n = query->nodes[n].parent)
    {
        if (query->nodes[n].slot == made->slot && query->nodes[n].polygon == made->polygon)
        {
            return true;
        }
    }
    return false;
}

// Opens node made, its parent and heap place aside, with ahead the least
// distance on the ground from its interval on to the end; drops it when
// a cheaper way to its root is known or a limit stops it.
static void Open(Shortest* s, int32_t parent, mnavSearchNode made, double ahead)
{
    mnavQuery* query = s->query;
    if (made.length + ahead > s->limit)
    {
        s->tooLong = true;
        return;
    }
    uint32_t cell = RootCell(query, made.at);
    uint32_t again = MadeCell(s, &made);
    int32_t before = s->made[again];
    if (!Cheapest(query, cell, made.cost) || Repeats(query, parent, &made) ||
        (before != MNAV_NO_NODE &&
         query->nodes[before].cost <= made.cost + 1e-9 * (1.0 + fabs(made.cost))))
    {
        return;
    }
    if (query->nodeCount == query->limits.nodes)
    {
        s->outOfNodes = true;
        return;
    }
    int32_t n = query->nodeCount++;
    made.parent = parent;
    query->nodes[n] = made;
    s->made[again] = n;
    int32_t held = query->table[cell];
    if (held == MNAV_NO_NODE || made.cost < query->nodes[held].cost)
    {
        query->table[cell] = n;
    }
    mnavPushNode(query, n);
    double toEnd = made.tag == KIND_END ? 0.0 : Flat(Nearest(s->end, made.a, made.b), s->end);
    if (toEnd < s->bestAhead || (toEnd == s->bestAhead && made.cost < query->nodes[s->best].cost))
    {
        s->best = n;
        s->bestAhead = toEnd;
    }
}

// The way from a node's root to a point p of the polygon beyond its
// interval: straight when the root sees p through the interval, else
// turning at the interval's end on p's side, when that end is a corner.
// With full, the root sees the whole polygon.
static bool Toward(const Shortest* s, const mnavSearchNode* node, Seen* seen, bool full, mnavPos3 p)
{
    if (full)
    {
        return true;
    }
    // A point on a side of the cone, to within rounding, is in it.
    double sideA = Cross(seen->root, node->a, p);
    double sideB = Cross(seen->root, node->b, p);
    double slackA = 1e-9 * Flat(seen->root, node->a) * (1.0 + Flat(node->a, p));
    double slackB = 1e-9 * Flat(seen->root, node->b) * (1.0 + Flat(node->b, p));
    if (sideA >= -slackA && sideB <= slackB)
    {
        return true;
    }
    bool viaB = sideB > 0.0;
    if ((node->tag & (viaB ? CORNER_B : CORNER_A)) == 0)
    {
        return false;
    }
    mnavPos3 corner = viaB ? node->b : node->a;
    double step = Flat(seen->root, corner);
    *seen = (Seen){corner, seen->cost + step * s->cost, seen->length + step};
    return true;
}

// Opens the node of the end point, in the polygon the node leads into.
static void ReachEnd(Shortest* s, int32_t n, Seen seen, bool full)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    if (!Toward(s, node, &seen, full, s->end))
    {
        return;
    }
    double rest = Flat(seen.root, s->end);
    mnavSearchNode made = {s->end,         s->end,      seen.root,     seen.cost, seen.length,
                           rest * s->cost, node->slot,  node->polygon, KIND_END,  0,
                           MNAV_NO_NODE,   MNAV_NO_NODE};
    Open(s, n, made, rest);
}

// Opens the nodes of the takeoff points of the off-mesh links leaving the
// polygon the node leads into, for the kinds and areas the filter
// includes.
static void ReachTakeoffs(Shortest* s, int32_t n, Seen seen, bool full)
{
    const mnavNavmesh* navmesh = s->navmesh;
    const mnavSearchNode* node = &s->query->nodes[n];
    int32_t first = 0;
    int32_t count = mnavAttachmentsFrom(navmesh, node->slot, node->polygon, &first);
    for (int32_t i = first; i < first + count; ++i)
    {
        mnavAttachment attachment = mnavAttachmentOf(navmesh->attachments[i]);
        const mnavOffLink* link = &navmesh->links[attachment.link];
        const mnavLinkState* state = &link->state;
        mnavPolygonId landing = attachment.reverse ? state->startPolygon : state->endPolygon;
        const mnavTile* tile = navmesh->slots[landing.slot - 1].tile;
        Seen way = seen;
        mnavPos3 takeoff = attachment.reverse ? state->end : state->start;
        mnavPos3 land = attachment.reverse ? state->start : state->end;
        if (!mnavCrosses(s->filter, link->def.kind) ||
            !mnavIncludes(s->filter, tile->mesh.polygons[landing.polygon].area) ||
            !Toward(s, node, &way, full, takeoff))
        {
            continue;
        }
        double walk = Flat(way.root, takeoff);
        double after = Flat(land, s->end);
        double remaining = walk * s->cost + (double)link->def.cost + after * s->scale;
        mnavSearchNode made = {takeoff,      takeoff,
                               way.root,     way.cost,
                               way.length,   remaining,
                               node->slot,   node->polygon,
                               KIND_TAKEOFF, attachment.link * 2 + (attachment.reverse ? 1 : 0),
                               MNAV_NO_NODE, MNAV_NO_NODE};
        Open(s, n, made, walk + Flat(takeoff, land) + after);
    }
}

// Crosses the off-mesh link of takeoff node n: opens its landing point as
// a root.
static void Land(Shortest* s, int32_t n)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavOffLink* link = &s->navmesh->links[node->low / 2];
    const mnavLinkState* state = &link->state;
    bool reverse = (node->low & 1) != 0;
    mnavPolygonId landing = reverse ? state->startPolygon : state->endPolygon;
    mnavPos3 land = reverse ? state->start : state->end;
    double walk = Flat(node->at, node->a);
    double span = Flat(node->a, land);
    double after = Flat(land, s->end);
    mnavSearchNode made = {land,
                           land,
                           land,
                           node->cost + walk * s->cost + (double)link->def.cost,
                           node->length + walk + span,
                           after * s->scale,
                           (int32_t)landing.slot - 1,
                           (int32_t)landing.polygon,
                           KIND_LANDING,
                           node->low,
                           MNAV_NO_NODE,
                           MNAV_NO_NODE};
    Open(s, n, made, after);
}

// Narrows [lo, hi] to where f, linear from f0 at t = 0 to f1 at t = 1, is
// at least 0; false when nothing of it remains. A crossing within 1e-9 of
// either end is that end: a cone's side through a vertex, rounded, would
// else make the vertex a point of no corner.
static bool AtLeastZero(double f0, double f1, double* lo, double* hi)
{
    if (f0 < 0.0 && f1 < 0.0)
    {
        return false;
    }
    if (f0 < 0.0 || f1 < 0.0)
    {
        double t = f0 / (f0 - f1);
        t = fabs(t - *lo) < 1e-9 ? *lo : (fabs(t - *hi) < 1e-9 ? *hi : t);
        *lo = f0 < 0.0 && t > *lo ? t : *lo;
        *hi = f1 < 0.0 && t < *hi ? t : *hi;
    }
    return *hi - *lo > 1e-12;
}

// Opens the interval of piece from u0 to u1 along its edge, seen from
// seen's root. In the polygon beyond, the edge runs from p1 to p0, so the
// interval's a lies toward p1.
static void Emit(Shortest* s, int32_t n, const Piece* piece, Seen seen, double u0, double u1)
{
    mnavPos3 a = Lerp(piece->p0, piece->p1, u1);
    mnavPos3 b = Lerp(piece->p0, piece->p1, u0);
    int32_t tag =
        piece->entry | (u1 == piece->t1 ? CORNER_A : 0) | (u0 == piece->t0 ? CORNER_B : 0);
    double ahead = Through(seen.root, a, b, s->end);
    mnavSearchNode made = {a,
                           b,
                           seen.root,
                           seen.cost,
                           seen.length,
                           ahead * s->scale,
                           piece->slot,
                           piece->polygon,
                           tag,
                           0,
                           MNAV_NO_NODE,
                           MNAV_NO_NODE};
    Open(s, n, made, ahead);
}

// Opens the successors of node n on a piece: the part its root sees
// through its interval keeps the root, the parts beside it turn at the
// interval's ends that are corners.
static void Spread(Shortest* s, int32_t n, const Piece* piece, Seen seen, bool full)
{
    if (full)
    {
        Emit(s, n, piece, seen, piece->t0, piece->t1);
        return;
    }
    const mnavSearchNode* node = &s->query->nodes[n];
    mnavPos3 r = seen.root;
    double a0 = Cross(r, node->a, piece->p0);
    double a1 = Cross(r, node->a, piece->p1);
    double b0 = Cross(r, node->b, piece->p0);
    double b1 = Cross(r, node->b, piece->p1);
    double lo = piece->t0;
    double hi = piece->t1;
    if ((node->tag & CORNER_B) != 0 && AtLeastZero(b0, b1, &lo, &hi))
    {
        double step = Flat(r, node->b);
        Emit(s, n, piece, (Seen){node->b, seen.cost + step * s->cost, seen.length + step}, lo, hi);
    }
    lo = piece->t0;
    hi = piece->t1;
    if (AtLeastZero(a0, a1, &lo, &hi) && AtLeastZero(-b0, -b1, &lo, &hi))
    {
        Emit(s, n, piece, seen, lo, hi);
    }
    lo = piece->t0;
    hi = piece->t1;
    if ((node->tag & CORNER_A) != 0 && AtLeastZero(-a0, -a1, &lo, &hi))
    {
        double step = Flat(r, node->a);
        Emit(s, n, piece, (Seen){node->a, seen.cost + step * s->cost, seen.length + step}, lo, hi);
    }
}

// The edge of polygon other that runs from vertex to back to vertex from:
// the entry edge of the polygon across an inner edge.
static int32_t EntryOf(const mnavPolygon* other, uint16_t from, uint16_t to)
{
    for (int32_t i = 0; i < other->count; ++i)
    {
        if (other->vertices[i] == to && other->vertices[(i + 1) % other->count] == from)
        {
            return i;
        }
    }
    return 0;
}

// The edge of a polygon on a tile side: at most one edge of a convex
// polygon lies on one line.
static int32_t SideEdgeOf(const mnavPolygon* polygon, int32_t side)
{
    for (int32_t i = 0; i < polygon->count; ++i)
    {
        if (polygon->sides[i] == side)
        {
            return i;
        }
    }
    return 0;
}

// Spreads node n over the tile links of edge j of its polygon, or notes
// that no tile is loaded across it.
static void SpreadSide(Shortest* s, int32_t n, const mnavTile* tile, int32_t j, Piece piece,
                       Seen seen, bool full)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavSlot* slot = &s->navmesh->slots[node->slot];
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    int32_t side = polygon->sides[j];
    int32_t x = slot->x;
    int32_t z = slot->z;
    int32_t facing = 0;
    int32_t across = -1;
    mnavAcross(side, &x, &z, &facing);
    if (mnavTileAt(s->navmesh, x, z, &across) == nullptr)
    {
        s->notLoaded = true;
        return;
    }
    const mnavMeshVertex* va = &tile->mesh.vertices[polygon->vertices[j]];
    const mnavMeshVertex* vb = &tile->mesh.vertices[polygon->vertices[(j + 1) % polygon->count]];
    bool alongZ = side == 1 || side == 3;
    double au = alongZ ? va->z : va->x;
    double bu = alongZ ? vb->z : vb->x;
    for (int32_t l = tile->firstLink[node->polygon]; l < tile->firstLink[node->polygon + 1]; ++l)
    {
        const mnavLink* link = &tile->links[l];
        const mnavTile* beyond = s->navmesh->slots[link->target.slot - 1].tile;
        const mnavPolygon* other = &beyond->mesh.polygons[link->target.polygon];
        if (link->edge != j || !mnavIncludes(s->filter, other->area))
        {
            continue;
        }
        double tLow = ((double)link->low - au) / (bu - au);
        double tHigh = ((double)link->high - au) / (bu - au);
        piece.t0 = tLow < tHigh ? tLow : tHigh;
        piece.t1 = tLow < tHigh ? tHigh : tLow;
        piece.slot = (int32_t)link->target.slot - 1;
        piece.polygon = (int32_t)link->target.polygon;
        piece.entry = SideEdgeOf(other, facing);
        Spread(s, n, &piece, seen, full);
    }
}

// Spreads node n over every edge of its polygon but the one it came in
// through.
static void SpreadEdges(Shortest* s, int32_t n, Seen seen, bool full)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    const mnavTile* tile = s->navmesh->slots[node->slot].tile;
    const mnavPolygon* polygon = &tile->mesh.polygons[node->polygon];
    const mnavSlot* slot = &s->navmesh->slots[node->slot];
    const mnavFrame f = mnavFrameOf(s->navmesh, slot->x, slot->z);
    int32_t entry = node->tag >= 0 ? (node->tag & EDGE_MASK) : -1;
    for (int32_t j = 0; j < polygon->count; ++j)
    {
        uint16_t from = polygon->vertices[j];
        uint16_t to = polygon->vertices[(j + 1) % polygon->count];
        Piece piece = {mnavVertexWorld(&f, &tile->mesh.vertices[from]),
                       mnavVertexWorld(&f, &tile->mesh.vertices[to]),
                       0.0,
                       1.0,
                       node->slot,
                       polygon->neighbors[j],
                       0};
        if (j == entry)
        {
            continue;
        }
        if (polygon->neighbors[j] != MNAV_NO_INDEX)
        {
            const mnavPolygon* other = &tile->mesh.polygons[polygon->neighbors[j]];
            if (mnavIncludes(s->filter, other->area))
            {
                piece.entry = EntryOf(other, from, to);
                Spread(s, n, &piece, seen, full);
            }
        }
        else if (polygon->sides[j] != 0)
        {
            SpreadSide(s, n, tile, j, piece, seen, full);
        }
    }
}

// How node n's root sees the polygon its interval leads into. A root on
// the interval's line sees it whole from the interval, or else from the
// interval's nearer end, turning there when that end is a corner; false
// when it cannot.
static bool Facing(const Shortest* s, int32_t n, Seen* seen, bool* full)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    *full = node->tag == KIND_START || node->tag == KIND_LANDING;
    if (*full)
    {
        return true;
    }
    double span = Flat(node->a, node->b);
    double side = Cross(node->a, node->b, seen->root);
    if (fabs(side) > 1e-9 * (1.0 + span * span))
    {
        return true;
    }
    *full = true;
    double toA = Flat(seen->root, node->a);
    double toB = Flat(seen->root, node->b);
    if (toA <= span && toB <= span)
    {
        return true;
    }
    bool nearA = toA < toB;
    if ((node->tag & (nearA ? CORNER_A : CORNER_B)) == 0)
    {
        return false;
    }
    double step = nearA ? toA : toB;
    *seen = (Seen){nearA ? node->a : node->b, seen->cost + step * s->cost, seen->length + step};
    return true;
}

static void Expand(Shortest* s, int32_t n)
{
    const mnavSearchNode* node = &s->query->nodes[n];
    if (node->tag == KIND_END)
    {
        s->found = n;
        return;
    }
    if (node->tag == KIND_TAKEOFF)
    {
        Land(s, n);
        return;
    }
    Seen seen = {node->at, node->cost, node->length};
    bool full = false;
    if (!Facing(s, n, &seen, &full))
    {
        return;
    }
    if (node->slot == s->endSlot && node->polygon == s->endPolygon)
    {
        ReachEnd(s, n, seen, full);
    }
    ReachTakeoffs(s, n, seen, full);
    SpreadEdges(s, n, seen, full);
}

// The one cost of every area the filter includes, or -1 when they differ.
static double OneCost(const mnavQueryFilter* filter)
{
    double cost = 1.0;
    bool any = false;
    for (int32_t area = 0; area < MNAV_AREA_TYPES; ++area)
    {
        if (!mnavIncludes(filter, (mnavAreaType)area))
        {
            continue;
        }
        double c = (double)filter->costs[area];
        if (any && c != cost)
        {
            return -1.0;
        }
        cost = c;
        any = true;
    }
    return cost;
}

// Writes the polygons from the start to node last into the corridor:
// nodes in one polygon are one visit, except across an off-mesh link.
static int32_t Polygons(mnavQuery* query, const mnavNavmesh* navmesh, int32_t last)
{
    int32_t count = 0;
    bool crossed = false;
    for (int32_t n = last; n != MNAV_NO_NODE; n = query->nodes[n].parent)
    {
        const mnavSearchNode* node = &query->nodes[n];
        mnavPolygonId id = {(uint32_t)node->slot + 1, navmesh->slots[node->slot].generation,
                            (uint32_t)node->polygon};
        const mnavPolygonId* previous = count > 0 ? &query->corridor[count - 1] : nullptr;
        if (previous == nullptr || crossed || previous->slot != id.slot ||
            previous->polygon != id.polygon)
        {
            query->corridor[count++] = id;
        }
        crossed = node->tag == KIND_LANDING;
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPolygonId swap = query->corridor[i];
        query->corridor[i] = query->corridor[count - 1 - i];
        query->corridor[count - 1 - i] = swap;
    }
    return count;
}

static void Push(mnavQuery* query, int32_t* count, mnavPos3 p)
{
    if (*count == 0 || !Same(query->points[*count - 1], p))
    {
        query->points[(*count)++] = p;
    }
}

// Writes the points from the start to node last, last first: the end
// point, or short of it the point of the last interval nearest the end;
// each root; a takeoff point before the landing point after it. Then
// turns them and the links around. linkCost receives the links' costs.
static int32_t Points(Shortest* s, int32_t last, int32_t* linkCount, double* linkCost)
{
    mnavQuery* query = s->query;
    const mnavSearchNode* tail = &query->nodes[last];
    int32_t count = 0;
    *linkCount = 0;
    *linkCost = 0.0;
    Push(query, &count, tail->tag < 0 ? tail->a : Nearest(s->end, tail->a, tail->b));
    for (int32_t n = last; n != MNAV_NO_NODE; n = query->nodes[n].parent)
    {
        const mnavSearchNode* node = &query->nodes[n];
        if (node->tag == KIND_TAKEOFF && n != last)
        {
            Push(query, &count, node->a);
            *linkCost += (double)s->navmesh->links[node->low / 2].def.cost;
            const mnavOffLink* link = &s->navmesh->links[s->navmesh->links[node->low / 2].parent];
            query->links[(*linkCount)++] = (mnavPathLink){
                {(uint32_t)s->navmesh->links[node->low / 2].parent + 1, link->generation},
                link->def.kind,
                count - 1};
        }
        Push(query, &count, node->at);
    }
    for (int32_t i = 0; i < count / 2; ++i)
    {
        mnavPos3 swap = query->points[i];
        query->points[i] = query->points[count - 1 - i];
        query->points[count - 1 - i] = swap;
    }
    for (int32_t i = 0; i < *linkCount; ++i)
    {
        query->links[i].point = count - 1 - query->links[i].point;
    }
    for (int32_t i = 0; i < *linkCount / 2; ++i)
    {
        mnavPathLink swap = query->links[i];
        query->links[i] = query->links[*linkCount - 1 - i];
        query->links[*linkCount - 1 - i] = swap;
    }
    return count;
}

// The path to node last: its corridor and points, and its cost and length
// along the points, each link at its own cost.
static void Finish(Shortest* s, int32_t last, mnavPathEnd end, mnavPath* pathOut)
{
    mnavQuery* query = s->query;
    int32_t linkCount = 0;
    double cost = 0.0;
    int32_t pointCount = Points(s, last, &linkCount, &cost);
    double length = 0.0;
    int32_t next = 0;
    for (int32_t i = 1; i < pointCount; ++i)
    {
        double step = Distance(query->points[i - 1], query->points[i]);
        bool link = next < linkCount && query->links[next].point == i - 1;
        cost += link ? 0.0 : step * s->cost;
        length += step;
        next += link ? 1 : 0;
    }
    *pathOut = (mnavPath){
        end,           cost,       length,       query->corridor, Polygons(query, s->navmesh, last),
        query->points, pointCount, query->links, linkCount};
}

// Checks the arguments and starts the search from its start node.
static mnavResult Begin(Shortest* s, const mnavQueryFilter* filter, mnavPolygonId startPolygon,
                        mnavPos3 start, mnavPolygonId endPolygon)
{
    mnavQuery* query = s->query;
    mnavResult result = mnavCheckPolygon(s->navmesh, startPolygon);
    result = result == mnav_success ? mnavCheckPolygon(s->navmesh, endPolygon) : result;
    result = result == mnav_success ? mnavCheckFilter(filter, &s->filter) : result;
    if (result != mnav_success)
    {
        return result;
    }
    s->cost = OneCost(s->filter);
    if (s->cost < 0.0)
    {
        return mnav_errorInvalid;
    }
    // A sliced path search in this context ends here: its memory is reused.
    query->search.active = false;
    query->filter = *s->filter;
    s->filter = &query->filter;
    s->scale = mnavHeuristicScale(s->navmesh, s->filter);
    s->endSlot = (int32_t)endPolygon.slot - 1;
    s->endPolygon = (int32_t)endPolygon.polygon;
    s->limit = (double)query->limits.pathLength;
    memset(query->table, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    s->made = query->madeTable;
    memset(s->made, 0xFF, ((size_t)query->tableMask + 1) * sizeof(int32_t));
    double ahead = Flat(start, s->end);
    query->nodes[0] = (mnavSearchNode){start,
                                       start,
                                       start,
                                       0.0,
                                       0.0,
                                       ahead * s->scale,
                                       (int32_t)startPolygon.slot - 1,
                                       (int32_t)startPolygon.polygon,
                                       KIND_START,
                                       0,
                                       MNAV_NO_NODE,
                                       MNAV_NO_NODE};
    query->table[RootCell(query, start)] = 0;
    s->made[MadeCell(s, &query->nodes[0])] = 0;
    query->nodeCount = 1;
    query->heapCount = 0;
    mnavPushNode(query, 0);
    s->best = 0;
    s->bestAhead = ahead;
    return mnav_success;
}

mnavResult mnavFindShortestPath(mnavQuery* query, const mnavNavmesh* navmesh,
                                const mnavQueryFilter* filter, mnavPolygonId startPolygon,
                                mnavPos3 start, mnavPolygonId endPolygon, mnavPos3 end,
                                mnavPath* pathOut)
{
    if (query == nullptr || navmesh == nullptr || pathOut == nullptr || !isfinite(start.x) ||
        !isfinite(start.y) || !isfinite(start.z) || !isfinite(end.x) || !isfinite(end.y) ||
        !isfinite(end.z))
    {
        return mnav_errorInvalid;
    }
    Shortest s = {.query = query, .navmesh = navmesh, .end = end, .found = MNAV_NO_NODE};
    mnavResult result = Begin(&s, filter, startPolygon, start, endPolygon);
    if (result != mnav_success)
    {
        return result;
    }
    while (query->heapCount > 0 && s.found == MNAV_NO_NODE)
    {
        int32_t n = mnavPopNode(query);
        const mnavSearchNode* node = &query->nodes[n];
        // A node whose root has since been reached more cheaply leads nowhere new.
        if (Cheapest(query, RootCell(query, node->at), node->cost))
        {
            Expand(&s, n);
        }
    }
    mnavPathEnd ending = s.found != MNAV_NO_NODE ? mnav_pathFound
                         : s.outOfNodes          ? mnav_pathOutOfNodes
                         : s.tooLong             ? mnav_pathTooLong
                         : s.notLoaded           ? mnav_pathNotLoaded
                                                 : mnav_pathNone;
    Finish(&s, s.found != MNAV_NO_NODE ? s.found : s.best, ending, pathOut);
    // An end the start cannot reach leaves Polyanya to spend its nodes on
    // every way there is; the A* search, a node per portal, tells it. It
    // uses the nodes, never the path written.
    mnavPathEnd reach = mnav_pathOutOfNodes;
    if (ending == mnav_pathOutOfNodes &&
        mnavCheckReachable(query, navmesh, filter, startPolygon, start, endPolygon, end, &reach) ==
            mnav_success &&
        reach != mnav_pathFound && reach != mnav_pathOutOfNodes)
    {
        pathOut->end = reach;
    }
    return mnav_success;
}
