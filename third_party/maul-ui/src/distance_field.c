// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Signs by scanline winding; the union's edge by where pieces meet and
// the windings on either side of each run between; distances by the
// edge's bounding boxes.

#include "distance_field.h"

#include <math.h>
#include <string.h>

enum
{
    // The most places other pieces may cross a piece for it to be cut
    // there; past that it is kept whole.
    MAX_SPLITS = 16
};

// Places along a piece within this of an end are at that end.
static const float END = 1.0f / 4096.0f;
// How far across a piece, for each unit of its coordinates' size,
// windings are taken on either side: well past the floats' rounding.
static const float NUDGE = 1.0f / 262144.0f;

// How many pieces of at most a pixel a segment is cut into.
static uint32_t PieceCount(const muiSegment* segment)
{
    float dx = segment->x1 - segment->x0;
    float dy = segment->y1 - segment->y0;
    float pieces = ceilf(sqrtf(dx * dx + dy * dy));
    return pieces < 1.0f ? 1u : (uint32_t)pieces;
}

size_t muiCountPieces(const muiSegment* segments, uint32_t count)
{
    size_t total = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        total += PieceCount(&segments[i]);
    }
    return total;
}

uint32_t muiCutPieces(const muiSegment* segments, uint32_t count, muiSegment* pieces,
                      uint32_t* origins)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const muiSegment* s = &segments[i];
        uint32_t n = PieceCount(s);
        float dx = s->x1 - s->x0;
        float dy = s->y1 - s->y0;
        float x = s->x0;
        float y = s->y0;
        for (uint32_t k = 1; k <= n; k++)
        {
            float t = (float)k / (float)n;
            float nx = k == n ? s->x1 : s->x0 + t * dx;
            float ny = k == n ? s->y1 : s->y0 + t * dy;
            pieces[total] = (muiSegment){x, y, nx, ny};
            origins[total++] = i;
            x = nx;
            y = ny;
        }
    }
    return total;
}

// The rows whose center lines cross a piece, its lower end included and
// its upper end not, so a shared end is counted once; false for none.
// Row r's center line is at top - r - 0.5.
static bool Crossed(const muiSegment* s, const muiFieldGrid* grid, int64_t* firstOut,
                    int64_t* lastOut)
{
    float low = s->y0 < s->y1 ? s->y0 : s->y1;
    float high = s->y0 < s->y1 ? s->y1 : s->y0;
    float origin = (float)grid->top - 0.5f;
    int64_t first = (int64_t)floorf(origin - high) + 1;
    int64_t last = (int64_t)floorf(origin - low);
    first = first < 0 ? 0 : first;
    last = last >= (int64_t)grid->height ? (int64_t)grid->height - 1 : last;
    *firstOut = first;
    *lastOut = last;
    return s->y0 != s->y1 && first <= last;
}

size_t muiCountCrossings(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid)
{
    size_t total = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        int64_t first = 0;
        int64_t last = 0;
        if (Crossed(&pieces[i], grid, &first, &last))
        {
            total += (size_t)(last - first + 1);
        }
    }
    return total;
}

// Sets starts to where each row's crossings begin.
static void Count(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                  uint32_t* starts)
{
    memset(starts, 0, ((size_t)grid->height + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++)
    {
        int64_t first = 0;
        int64_t last = 0;
        if (Crossed(&pieces[i], grid, &first, &last))
        {
            for (int64_t row = first; row <= last; row++)
            {
                starts[row + 1]++;
            }
        }
    }
    for (uint32_t row = 0; row < grid->height; row++)
    {
        starts[row + 1] += starts[row];
    }
}

// Adds a piece's crossings, each at its row's next place.
static void Place(const muiSegment* s, const muiFieldGrid* grid, uint32_t* next,
                  muiCrossing* crossings)
{
    int64_t first = 0;
    int64_t last = 0;
    if (!Crossed(s, grid, &first, &last))
    {
        return;
    }
    int32_t winding = s->y1 > s->y0 ? 1 : -1;
    for (int64_t row = first; row <= last; row++)
    {
        float y = (float)grid->top - 0.5f - (float)row;
        float x = s->x0 + (y - s->y0) * (s->x1 - s->x0) / (s->y1 - s->y0);
        crossings[next[row]++] = (muiCrossing){x, winding};
    }
}

// Sorts each row's crossings along it; rows hold few.
static void Sort(const uint32_t* starts, uint32_t rows, muiCrossing* crossings)
{
    for (uint32_t r = 0; r < rows; r++)
    {
        muiCrossing* row = crossings + starts[r];
        uint32_t n = starts[r + 1] - starts[r];
        for (uint32_t i = 1; i < n; i++)
        {
            muiCrossing crossing = row[i];
            uint32_t j = i;
            for (; j > 0 && row[j - 1].at > crossing.at; j--)
            {
                row[j] = row[j - 1];
            }
            row[j] = crossing;
        }
    }
}

// Fills each row's crossings, from starts, sorted along the row.
static void Cross(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                  uint32_t* starts, muiCrossing* crossings)
{
    Count(pieces, count, grid, starts);
    // starts[row] counts up as the row fills, ending where the next
    // begins; moved back by one row afterwards.
    for (uint32_t i = 0; i < count; i++)
    {
        Place(&pieces[i], grid, starts, crossings);
    }
    for (uint32_t row = grid->height; row > 0; row--)
    {
        starts[row] = starts[row - 1];
    }
    starts[0] = 0;
    Sort(starts, grid->height, crossings);
}

static bool IsInside(const muiFieldGrid* grid, int32_t winding)
{
    return grid->evenOdd ? (winding & 1) != 0 : winding != 0;
}

// Marks each pixel 1 whose center is inside, 0 otherwise.
static void Fill(const muiFieldGrid* grid, const uint32_t* starts, const muiCrossing* crossings,
                 unsigned char* inside)
{
    for (uint32_t r = 0; r < grid->height; r++)
    {
        const muiCrossing* row = crossings + starts[r];
        uint32_t n = starts[r + 1] - starts[r];
        uint32_t k = 0;
        int32_t winding = 0;
        for (uint32_t c = 0; c < grid->width; c++)
        {
            float x = (float)grid->left + (float)c + 0.5f;
            for (; k < n && row[k].at < x; k++)
            {
                winding += grid->evenOdd ? 1 : row[k].winding;
            }
            inside[(size_t)r * grid->width + c] = IsInside(grid, winding) ? 1 : 0;
        }
    }
}

// The cell of the pixel a point is in, held within the grid. Truncating
// is flooring once held at zero.
static size_t CellOf(const muiFieldGrid* grid, float x, float y)
{
    float c = x - (float)grid->left;
    float r = (float)grid->top - y;
    uint32_t column = !(c >= 1.0f) ? 0 : (c >= (float)grid->width ? grid->width - 1 : (uint32_t)c);
    uint32_t row = !(r >= 1.0f) ? 0 : (r >= (float)grid->height ? grid->height - 1 : (uint32_t)r);
    return (size_t)row * grid->width + column;
}

// The cell of the top left corner of a piece's box.
static size_t CornerCell(const muiFieldGrid* grid, const muiSegment* s)
{
    return CellOf(grid, s->x0 < s->x1 ? s->x0 : s->x1, s->y0 < s->y1 ? s->y1 : s->y0);
}

// Sorts the pieces by the cell their box's top left corner is in: cell
// k's are order's entries from starts[k] to starts[k + 1].
static void Bucket(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                   uint32_t* starts, uint32_t* order)
{
    size_t cells = (size_t)grid->width * grid->height;
    memset(starts, 0, (cells + 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++)
    {
        starts[CornerCell(grid, &pieces[i]) + 1]++;
    }
    for (size_t k = 0; k < cells; k++)
    {
        starts[k + 1] += starts[k];
    }
    for (uint32_t i = 0; i < count; i++)
    {
        order[starts[CornerCell(grid, &pieces[i])]++] = i;
    }
    for (size_t k = cells; k > 0; k--)
    {
        starts[k] = starts[k - 1];
    }
    starts[0] = 0;
}

static float Cross2(float ax, float ay, float bx, float by)
{
    return ax * by - ay * bx;
}

// Where other pieces meet a piece: the places along it, from 0 to 1,
// where they cross it, MAX_SPLITS + 1 when there are more, and whether
// one meets it at its start or its end other than the piece it is joined
// to there.
typedef struct Hits
{
    float places[MAX_SPLITS];
    uint32_t splits;
    bool start;
    bool end;
} Hits;

// Notes a piece meeting another at place t along it; joined tells
// whether that is the piece joined to it at its start (-1), its end (1)
// or neither (0).
static void Hit(Hits* hits, float t, int joined)
{
    if (t <= END)
    {
        hits->start = hits->start || joined >= 0;
    }
    else if (t >= 1.0f - END)
    {
        hits->end = hits->end || joined <= 0;
    }
    else if (hits->splits <= MAX_SPLITS)
    {
        hits->places[hits->splits < MAX_SPLITS ? hits->splits : 0] = t;
        hits->splits++;
    }
}

// Notes where piece q meets piece p. Pieces along one line need no
// note: where one leaves the line, the next piece of its contour meets p.
static void Meet(const muiSegment* p, const muiSegment* q, int joined, Hits* hits)
{
    float dx = p->x1 - p->x0;
    float dy = p->y1 - p->y0;
    float ex = q->x1 - q->x0;
    float ey = q->y1 - q->y0;
    float denominator = Cross2(dx, dy, ex, ey);
    if (denominator == 0.0f)
    {
        return;
    }
    float wx = q->x0 - p->x0;
    float wy = q->y0 - p->y0;
    float t = Cross2(wx, wy, ex, ey) / denominator;
    float u = Cross2(wx, wy, dx, dy) / denominator;
    if (t >= -END && t <= 1.0f + END && u >= -END && u <= 1.0f + END)
    {
        Hit(hits, t, joined);
    }
}

static bool IsJoined(const muiSegment* a, const muiSegment* b)
{
    return a->x1 == b->x0 && a->y1 == b->y0;
}

// A box: its least and greatest x and y.
typedef struct Box
{
    float minX;
    float maxX;
    float minY;
    float maxY;
} Box;

static bool IsApart(const muiSegment* s, const Box* box)
{
    return (s->x0 < box->minX && s->x1 < box->minX) || (s->x0 > box->maxX && s->x1 > box->maxX) ||
           (s->y0 < box->minY && s->y1 < box->minY) || (s->y0 > box->maxY && s->y1 > box->maxY);
}

static void SortPlaces(Hits* hits)
{
    for (uint32_t k = 1; k < hits->splits && hits->splits <= MAX_SPLITS; k++)
    {
        float t = hits->places[k];
        uint32_t m = k;
        for (; m > 0 && hits->places[m - 1] > t; m--)
        {
            hits->places[m] = hits->places[m - 1];
        }
        hits->places[m] = t;
    }
}

// Finds where the other pieces meet piece i, among those whose boxes'
// corners are in the cells within a piece's length left of and above
// its box, with its crossings sorted.
static void FindHits(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                     const uint32_t* starts, const uint32_t* order, uint32_t i, Hits* hits)
{
    *hits = (Hits){.splits = 0};
    const muiSegment* p = &pieces[i];
    bool before = i > 0 && IsJoined(&pieces[i - 1], p);
    bool after = i + 1 < count && IsJoined(p, &pieces[i + 1]);
    // Its box, and a little more, as pieces may meet a little past an end
    // and cut pieces may round past a pixel.
    float margin = 1.0f / 64.0f;
    Box box = {(p->x0 < p->x1 ? p->x0 : p->x1) - margin, (p->x0 < p->x1 ? p->x1 : p->x0) + margin,
               (p->y0 < p->y1 ? p->y0 : p->y1) - margin, (p->y0 < p->y1 ? p->y1 : p->y0) + margin};
    size_t first = CellOf(grid, box.minX - 1.0f, box.maxY + 1.0f);
    size_t last = CellOf(grid, box.maxX, box.minY);
    size_t columns = last % grid->width - first % grid->width + 1;
    for (size_t row = first; row <= last; row += grid->width)
    {
        for (uint32_t k = starts[row]; k < starts[row + columns]; k++)
        {
            uint32_t j = order[k];
            if (j != i && !IsApart(&pieces[j], &box))
            {
                int joined = before && j == i - 1 ? -1 : (after && j == i + 1 ? 1 : 0);
                Meet(p, &pieces[j], joined, hits);
            }
        }
    }
    SortPlaces(hits);
}

// Whether a point is inside, by the winding of the pieces a ray from it
// to the left crosses.
static bool IsPointInside(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                          float x, float y)
{
    int32_t winding = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const muiSegment* s = &pieces[i];
        bool upward = s->y0 <= y && y < s->y1;
        bool downward = s->y1 <= y && y < s->y0;
        if ((upward || downward) && s->x0 + (y - s->y0) * (s->x1 - s->x0) / (s->y1 - s->y0) < x)
        {
            winding += grid->evenOdd || upward ? 1 : -1;
        }
    }
    return IsInside(grid, winding);
}

// Whether the part of a piece from a to b along it is on the edge:
// inside on one side of its middle and not on the other.
static bool IsOnEdge(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                     const muiSegment* p, float a, float b)
{
    float dx = p->x1 - p->x0;
    float dy = p->y1 - p->y0;
    float t = (a + b) * 0.5f;
    float mx = p->x0 + t * dx;
    float my = p->y0 + t * dy;
    float nudge = (fabsf(mx) + fabsf(my) + 64.0f) * NUDGE / sqrtf(dx * dx + dy * dy);
    float nx = -dy * nudge;
    float ny = dx * nudge;
    return IsPointInside(pieces, count, grid, mx + nx, my + ny) !=
           IsPointInside(pieces, count, grid, mx - nx, my - ny);
}

// Adds the parts of a piece other pieces cross that are on the edge: it
// is cut where they cross. When more cross it than are noted, or the room
// left is short, it is kept whole.
static uint32_t SplitPiece(const muiSegment* pieces, uint32_t count, const muiFieldGrid* grid,
                           const muiSegment* p, const Hits* hits, muiSegment* edge, uint32_t found,
                           size_t room)
{
    if (hits->splits > MAX_SPLITS || (size_t)found + hits->splits + 1 > room)
    {
        edge[found++] = *p;
        return found;
    }
    float dx = p->x1 - p->x0;
    float dy = p->y1 - p->y0;
    for (uint32_t k = 0; k <= hits->splits; k++)
    {
        float a = k == 0 ? 0.0f : hits->places[k - 1];
        float b = k == hits->splits ? 1.0f : hits->places[k];
        if (IsOnEdge(pieces, count, grid, p, a, b))
        {
            edge[found++] =
                (muiSegment){k == 0 ? p->x0 : p->x0 + a * dx, k == 0 ? p->y0 : p->y0 + a * dy,
                             k == hits->splits ? p->x1 : p->x0 + b * dx,
                             k == hits->splits ? p->y1 : p->y0 + b * dy};
        }
    }
    return found;
}

size_t muiEdgeRoom(uint32_t count)
{
    return (size_t)count * 2 + MAX_SPLITS;
}

// Writes the edge's segments. Along a contour, whether pieces are on the
// edge changes only where other pieces meet them, so it is found once
// for each run between such places, and runs of one segment are joined.
// A piece others cross is cut there and each part found on its own.
static uint32_t FindEdge(const muiSegment* pieces, const uint32_t* origins, uint32_t count,
                         const muiFieldGrid* grid, const muiFieldScratch* scratch, muiSegment* edge)
{
    size_t room = muiEdgeRoom(count);
    uint32_t found = 0;
    // Whether the last edge segment may grow by the next piece, and
    // whether on holds for it.
    bool joinable = false;
    bool known = false;
    bool on = false;
    for (uint32_t i = 0; i < count; i++)
    {
        const muiSegment* p = &pieces[i];
        if (p->x0 == p->x1 && p->y0 == p->y1)
        {
            // A point is no nearer than the pieces beside it.
            continue;
        }
        Hits hits;
        FindHits(pieces, count, grid, scratch->cellStarts, scratch->cellPieces, i, &hits);
        known = known && i > 0 && IsJoined(&pieces[i - 1], p) && !hits.start;
        if (hits.splits > 0)
        {
            // A whole piece leaves room for the rest, one each.
            found = SplitPiece(pieces, count, grid, p, &hits, edge, found, room - (count - i - 1));
            known = false;
            joinable = false;
            continue;
        }
        on = known ? on : IsOnEdge(pieces, count, grid, p, 0.0f, 1.0f);
        known = !hits.end;
        if (!on)
        {
            joinable = false;
            continue;
        }
        if (joinable && origins[i - 1] == origins[i])
        {
            edge[found - 1].x1 = p->x1;
            edge[found - 1].y1 = p->y1;
            continue;
        }
        edge[found++] = *p;
        joinable = true;
    }
    return found;
}

// The pixels whose centers are within reach of a segment's box: columns
// c0 to c1 and rows r0 to r1, in the grid; false for none.
typedef struct Reach
{
    int64_t c0;
    int64_t c1;
    int64_t r0;
    int64_t r1;
} Reach;

static bool ReachOf(const muiSegment* s, const muiFieldGrid* grid, Reach* out)
{
    float reach = (float)grid->spread;
    float left = (float)grid->left + 0.5f;
    float top = (float)grid->top - 0.5f;
    float minX = (s->x0 < s->x1 ? s->x0 : s->x1) - reach;
    float maxX = (s->x0 < s->x1 ? s->x1 : s->x0) + reach;
    float minY = (s->y0 < s->y1 ? s->y0 : s->y1) - reach;
    float maxY = (s->y0 < s->y1 ? s->y1 : s->y0) + reach;
    int64_t c0 = (int64_t)ceilf(minX - left);
    int64_t c1 = (int64_t)floorf(maxX - left);
    int64_t r0 = (int64_t)ceilf(top - maxY);
    int64_t r1 = (int64_t)floorf(top - minY);
    out->c0 = c0 < 0 ? 0 : c0;
    out->r0 = r0 < 0 ? 0 : r0;
    out->c1 = c1 >= (int64_t)grid->width ? (int64_t)grid->width - 1 : c1;
    out->r1 = r1 >= (int64_t)grid->height ? (int64_t)grid->height - 1 : r1;
    return out->c0 <= out->c1 && out->r0 <= out->r1;
}

// Lowers the squared distances of the pixels within reach of a segment
// to the segment's where it is nearer.
static void MeasureSegment(const muiSegment* s, const muiFieldGrid* grid, const Reach* reach,
                           float* distances)
{
    float left = (float)grid->left + 0.5f;
    float top = (float)grid->top - 0.5f;
    float dx = s->x1 - s->x0;
    float dy = s->y1 - s->y0;
    float length = dx * dx + dy * dy;
    float inverse = length > 0.0f ? 1.0f / length : 0.0f;
    for (int64_t r = reach->r0; r <= reach->r1; r++)
    {
        float py = top - (float)r - s->y0;
        float* row = distances + (size_t)r * grid->width;
        for (int64_t c = reach->c0; c <= reach->c1; c++)
        {
            float px = left + (float)c - s->x0;
            float t = (px * dx + py * dy) * inverse;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            float qx = px - t * dx;
            float qy = py - t * dy;
            float d = qx * qx + qy * qy;
            row[c] = d < row[c] ? d : row[c];
        }
    }
}

// Sets each pixel's squared distance to that of the nearest segment
// within reach, else the reach's.
static void Measure(const muiSegment* segments, uint32_t count, const muiFieldGrid* grid,
                    float* distances)
{
    float reach = (float)grid->spread;
    for (size_t i = 0; i < (size_t)grid->width * grid->height; i++)
    {
        distances[i] = reach * reach;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        Reach pixels;
        if (ReachOf(&segments[i], grid, &pixels))
        {
            MeasureSegment(&segments[i], grid, &pixels, distances);
        }
    }
}

void muiDrawDistanceField(const muiSegment* pieces, const uint32_t* origins, uint32_t count,
                          const muiFieldGrid* grid, const muiFieldScratch* scratch,
                          unsigned char* pixels)
{
    Cross(pieces, count, grid, scratch->rowStarts, scratch->crossings);
    Bucket(pieces, count, grid, scratch->cellStarts, scratch->cellPieces);
    uint32_t edges = FindEdge(pieces, origins, count, grid, scratch, scratch->edge);
    Measure(scratch->edge, edges, grid, scratch->distances);
    // The pixels say which centers are inside until the field replaces
    // them.
    Fill(grid, scratch->rowStarts, scratch->crossings, pixels);
    float scale = 128.0f / (float)grid->spread;
    for (size_t at = 0; at < (size_t)grid->width * grid->height; at++)
    {
        float distance = sqrtf(scratch->distances[at]) * scale;
        float value = 128.0f + (pixels[at] != 0 ? distance : -distance) + 0.5f;
        pixels[at] = value <= 0.0f ? 0 : value >= 255.0f ? 255 : (unsigned char)value;
    }
}
