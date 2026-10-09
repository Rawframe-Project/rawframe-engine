// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Multi-channel distance fields (record mui-0006), after Chlumský's
// "Shape Decomposition for Multi-channel Distance Fields" and msdfgen.
// The edge segments come in contour order; those joined end to start
// make chains, and a chain is cut into edges at corners: where it ends
// (the union leaving one contour for another), and where two lines or
// curves of the outline meet turning past msdfgen's threshold. Flattened
// pieces of one curve never make a corner, however tightly it turns.
// Edges are coloured so the two at a corner share one channel; past an
// edge's end at a corner, its distance is to its line, so the median of
// the channels keeps the corner. Where the median's sign is not the true
// inside's, the channels take the true distance.

#include "multi_field.h"

#include <math.h>

enum
{
    RED = 1,
    GREEN = 2,
    BLUE = 4,
    WHITE = RED | GREEN | BLUE,
    CYAN = GREEN | BLUE,
    MAGENTA = RED | BLUE,
    YELLOW = RED | GREEN,
    // Whether an edge segment starts or ends an edge at a corner.
    START = 8,
    END = 16,
    CHANNELS = 3
};

// msdfgen's corner threshold of 3 radians: a turn whose cross product of
// unit directions is past sin 3, or which turns back.
static const float CORNER_CROSS = 0.14112000806f;

static bool IsJoined(const muiSegment* a, const muiSegment* b)
{
    return a->x1 == b->x0 && a->y1 == b->y0;
}

// Whether the joint from one edge segment to the next turns at a corner.
static bool Turns(const muiSegment* a, const muiSegment* b)
{
    float ax = a->x1 - a->x0;
    float ay = a->y1 - a->y0;
    float bx = b->x1 - b->x0;
    float by = b->y1 - b->y0;
    float lengths = sqrtf(ax * ax + ay * ay) * sqrtf(bx * bx + by * by);
    if (!(lengths > 0.0f))
    {
        return false;
    }
    float dot = (ax * bx + ay * by) / lengths;
    float cross = (ax * by - ay * bx) / lengths;
    return dot <= 0.0f || fabsf(cross) > CORNER_CROSS;
}

// Whether the joint from edge segment a to b is a corner: they are parts
// of different lines or curves that turn there.
static bool IsCorner(const muiFieldScratch* scratch, const muiMultiScratch* multi, uint32_t a,
                     uint32_t b)
{
    return multi->curves[scratch->edgeOrigins[a]] != multi->curves[scratch->edgeOrigins[b]] &&
           Turns(&scratch->edge[a], &scratch->edge[b]);
}

// The colour after one, other than banned where it can be: cyan,
// magenta and yellow in turn.
static uint8_t NextColor(uint8_t color, uint8_t banned)
{
    static const uint8_t order[3] = {CYAN, MAGENTA, YELLOW};
    uint32_t at = color == CYAN ? 0u : color == MAGENTA ? 1u : 2u;
    uint8_t next = order[(at + 1) % 3];
    return next != banned ? next : order[(at + 2) % 3];
}

// Whether chain a's start comes before chain b's, by x, then y, then
// index: a total order, so the sort is the same everywhere.
static bool StartsBefore(const muiSegment* edge, const uint32_t* chains, uint32_t a, uint32_t b)
{
    const muiSegment* p = &edge[chains[a]];
    const muiSegment* q = &edge[chains[b]];
    return p->x0 != q->x0 ? p->x0 < q->x0 : p->y0 != q->y0 ? p->y0 < q->y0 : a < b;
}

// Moves entry root of a heap of count chains down to its place.
static void SiftDown(const muiSegment* edge, const uint32_t* chains, uint32_t* sorted,
                     uint32_t root, uint32_t count)
{
    for (;;)
    {
        uint32_t child = root * 2 + 1;
        if (child >= count)
        {
            return;
        }
        if (child + 1 < count && StartsBefore(edge, chains, sorted[child], sorted[child + 1]))
        {
            child++;
        }
        if (!StartsBefore(edge, chains, sorted[root], sorted[child]))
        {
            return;
        }
        uint32_t swap = sorted[root];
        sorted[root] = sorted[child];
        sorted[child] = swap;
        root = child;
    }
}

// Sorts the chains by their starts: a heap sort, in place and in time
// n log n however many chains crossings make.
static void SortChains(const muiSegment* edge, const uint32_t* chains, uint32_t* sorted,
                       uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        sorted[i] = i;
    }
    for (uint32_t root = count / 2; root-- > 0;)
    {
        SiftDown(edge, chains, sorted, root, count);
    }
    for (uint32_t end = count; end-- > 1;)
    {
        uint32_t swap = sorted[0];
        sorted[0] = sorted[end];
        sorted[end] = swap;
        SiftDown(edge, chains, sorted, 0, end);
    }
}

// The first place in sorted whose chain starts at or after (x, y).
static uint32_t FirstAt(const muiSegment* edge, const uint32_t* chains, const uint32_t* sorted,
                        uint32_t count, float x, float y)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        const muiSegment* p = &edge[chains[sorted[middle]]];
        if (p->x0 < x || (p->x0 == x && p->y0 < y))
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The chain after chain c in a loop that began with chain first: first
// when the loop closes there, else a chain not yet taken that starts
// where c ends, else count.
static uint32_t Successor(const muiSegment* edge, const muiMultiScratch* multi, uint32_t count,
                          uint32_t c, uint32_t first)
{
    const uint32_t* chains = multi->chains;
    const muiSegment* last = &edge[chains[c + 1] - 1];
    uint32_t next = count;
    for (uint32_t k = FirstAt(edge, chains, multi->sorted, count, last->x1, last->y1); k < count;
         k++)
    {
        uint32_t d = multi->sorted[k];
        const muiSegment* p = &edge[chains[d]];
        if (p->x0 != last->x1 || p->y0 != last->y1)
        {
            break;
        }
        if (d == first)
        {
            return first;
        }
        next = next == count && multi->edgeColors[chains[d]] == 0 ? d : next;
    }
    return next;
}

// Marks a loop's corners START, the rest 0, and counts them; first
// receives the place of a closed loop's first corner, else 0.
static uint32_t MarkCorners(const muiFieldScratch* scratch, const muiMultiScratch* multi,
                            uint32_t n, bool closed, uint32_t* first)
{
    const uint32_t* loop = multi->loop;
    uint32_t corners = 0;
    *first = 0;
    for (uint32_t k = 0; k < n; k++)
    {
        bool corner =
            (closed || k > 0) && IsCorner(scratch, multi, loop[k == 0 ? n - 1 : k - 1], loop[k]);
        multi->edgeColors[loop[k]] = corner ? START : 0;
        *first = corner && corners == 0 && closed ? k : *first;
        corners += corner;
    }
    return corners;
}

// Marks where a loop's edges start and end: where the colour changes,
// and at an open loop's ends.
static void MarkEnds(const muiMultiScratch* multi, uint32_t n, bool closed)
{
    const uint32_t* loop = multi->loop;
    uint8_t* colors = multi->edgeColors;
    for (uint32_t k = 0; k < n; k++)
    {
        uint32_t next = k + 1 < n ? k + 1 : 0;
        bool last = k + 1 == n && !closed;
        if (last || (colors[loop[k]] & WHITE) != (colors[loop[next]] & WHITE))
        {
            colors[loop[k]] |= END;
            colors[loop[next]] |= last ? 0 : START;
        }
    }
    colors[loop[0]] |= closed ? 0 : START;
}

// Colours a loop of edge segments, closed or open: a closed one white
// without corners, in three parts from its corner with one (magenta,
// white, yellow), else from its first corner taking colours in turn, the
// last unlike the first; an open one in turn from its start.
static void ColorLoop(const muiFieldScratch* scratch, const muiMultiScratch* multi, uint32_t n,
                      bool closed)
{
    static const uint8_t parts[3] = {MAGENTA, WHITE, YELLOW};
    const uint32_t* loop = multi->loop;
    uint8_t* colors = multi->edgeColors;
    uint32_t first = 0;
    uint32_t corners = MarkCorners(scratch, multi, n, closed, &first);
    uint8_t color = CYAN;
    uint32_t seen = 0;
    for (uint32_t step = 0; step < n; step++)
    {
        uint32_t k = (first + step) % n;
        if (step > 0 && (colors[loop[k]] & START) != 0)
        {
            seen++;
            color = NextColor(color, closed && seen + 1 == corners ? (uint8_t)CYAN : (uint8_t)0);
        }
        uint8_t shade = corners == 1 ? parts[(uint64_t)step * 3 / n] : color;
        colors[loop[k]] = closed && corners < 2 ? (corners == 0 ? (uint8_t)WHITE : shade) : color;
    }
    MarkEnds(multi, n, closed);
}

// Colours the edge segments loop by loop. Edge segments joined end to
// start make chains; a chain ends where the union's edge leaves one
// contour for another, and chains are joined end to start into the
// union's loops, so a loop's colours alternate across such places too.
static void Color(const muiFieldScratch* scratch, const muiMultiScratch* multi, uint32_t count)
{
    const muiSegment* edge = scratch->edge;
    uint8_t* colors = multi->edgeColors;
    uint32_t* chains = multi->chains;
    uint32_t chainCount = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        colors[i] = 0;
        if (i == 0 || !IsJoined(&edge[i - 1], &edge[i]))
        {
            chains[chainCount++] = i;
        }
    }
    chains[chainCount] = count;
    SortChains(edge, chains, multi->sorted, chainCount);
    for (uint32_t c = 0; c < chainCount; c++)
    {
        if (colors[chains[c]] != 0)
        {
            continue;
        }
        uint32_t n = 0;
        bool closed = false;
        for (uint32_t d = c; d != chainCount && !closed;)
        {
            // Taken: a chain's first colour is not 0 from here on.
            colors[chains[d]] = WHITE;
            for (uint32_t i = chains[d]; i < chains[d + 1]; i++)
            {
                multi->loop[n++] = i;
            }
            d = Successor(edge, multi, chainCount, d, c);
            closed = d == c;
        }
        ColorLoop(scratch, multi, n, closed);
    }
}

// The pixels whose centers are within the spread of a segment's box:
// columns c0 to c1 and rows r0 to r1; false for none.
static bool ReachOf(const muiSegment* s, const muiFieldGrid* grid, int64_t* c0, int64_t* c1,
                    int64_t* r0, int64_t* r1)
{
    float reach = (float)grid->spread;
    float left = (float)grid->left + 0.5f;
    float top = (float)grid->top - 0.5f;
    int64_t a = (int64_t)ceilf((s->x0 < s->x1 ? s->x0 : s->x1) - reach - left);
    int64_t b = (int64_t)floorf((s->x0 < s->x1 ? s->x1 : s->x0) + reach - left);
    int64_t c = (int64_t)ceilf(top - (s->y0 < s->y1 ? s->y1 : s->y0) - reach);
    int64_t d = (int64_t)floorf(top - (s->y0 < s->y1 ? s->y0 : s->y1) + reach);
    *c0 = a < 0 ? 0 : a;
    *r0 = c < 0 ? 0 : c;
    *c1 = b >= (int64_t)grid->width ? (int64_t)grid->width - 1 : b;
    *r1 = d >= (int64_t)grid->height ? (int64_t)grid->height - 1 : d;
    return *c0 <= *c1 && *r0 <= *r1;
}

// A pixel's channels: for each, the squared distance to the nearest edge
// segment of its colour, how far from perpendicular to it the pixel is,
// and its signed pseudo-distance.
typedef struct Channels
{
    float nearest[CHANNELS];
    float slant[CHANNELS];
    float value[CHANNELS];
} Channels;

// What a pixel is from an edge segment: its squared distance, slant and
// signed pseudo-distance.
typedef struct Reading
{
    float squared;
    float slant;
    float value;
} Reading;

static Reading Read(const muiSegment* s, uint8_t flags, int side, float px, float py)
{
    float dx = s->x1 - s->x0;
    float dy = s->y1 - s->y0;
    float length = dx * dx + dy * dy;
    float along = length > 0.0f ? (px * dx + py * dy) / length : 0.0f;
    float t = along < 0.0f ? 0.0f : along > 1.0f ? 1.0f : along;
    float qx = px - t * dx;
    float qy = py - t * dy;
    float squared = qx * qx + qy * qy;
    float cross = dx * py - dy * px;
    // On the line (cross 0) the sign does not matter: the distance or the
    // pseudo-distance is 0, or a piece joined at the end is as near and
    // less slanted.
    float sign = (cross > 0.0f) == (side > 0) ? 1.0f : -1.0f;
    float distance = sqrtf(squared);
    float slant = squared > 0.0f && length > 0.0f
                      ? fabsf(qx * dx + qy * dy) / (distance * sqrtf(length))
                      : 0.0f;
    bool beyond = (along < 0.0f && (flags & START) != 0) || (along > 1.0f && (flags & END) != 0);
    float value = beyond ? sign * fabsf(cross) / sqrtf(length) : sign * distance;
    return (Reading){squared, slant, value};
}

// Lowers each pixel's channels of a segment's colour to the segment's
// where it is nearer, or as near and more perpendicular.
static void MeasureSegment(const muiSegment* s, uint8_t colors, int side, const muiFieldGrid* grid,
                           Channels* channels)
{
    int64_t c0 = 0;
    int64_t c1 = 0;
    int64_t r0 = 0;
    int64_t r1 = 0;
    if (!ReachOf(s, grid, &c0, &c1, &r0, &r1))
    {
        return;
    }
    float left = (float)grid->left + 0.5f;
    float top = (float)grid->top - 0.5f;
    for (int64_t r = r0; r <= r1; r++)
    {
        for (int64_t c = c0; c <= c1; c++)
        {
            size_t at = (size_t)r * grid->width + (size_t)c;
            Reading reading =
                Read(s, colors, side, left + (float)c - s->x0, top - (float)r - s->y0);
            Channels* pixel = &channels[at];
            for (uint32_t k = 0; k < CHANNELS; k++)
            {
                bool nearer =
                    reading.squared < pixel->nearest[k] ||
                    (reading.squared == pixel->nearest[k] && reading.slant < pixel->slant[k]);
                if ((colors & (1u << k)) != 0 && nearer)
                {
                    pixel->nearest[k] = reading.squared;
                    pixel->slant[k] = reading.slant;
                    pixel->value[k] = reading.value;
                }
            }
        }
    }
}

static float Median(float a, float b, float c)
{
    return fmaxf(fminf(a, b), fminf(fmaxf(a, b), c));
}

static unsigned char Encode(float distance, float scale)
{
    float value = 128.0f + distance * scale + 0.5f;
    return value <= 0.0f ? 0 : value >= 255.0f ? 255 : (unsigned char)value;
}

void muiDrawMultiField(const muiSegment* pieces, const uint32_t* origins, uint32_t count,
                       const muiFieldGrid* grid, const muiFieldScratch* scratch,
                       const muiMultiScratch* multi, unsigned char* pixels)
{
    unsigned char* inside = multi->inside;
    uint32_t edges = muiFindFieldEdge(pieces, origins, count, grid, scratch, inside);
    muiMeasureField(scratch->edge, edges, grid, scratch->distances);
    Color(scratch, multi, edges);
    size_t area = (size_t)grid->width * grid->height;
    float spread = (float)grid->spread;
    Channels* channels = multi->channels;
    for (size_t at = 0; at < area; at++)
    {
        float outer = inside[at] != 0 ? spread : -spread;
        // Past the spread of every edge: as far as the field reaches.
        channels[at] =
            (Channels){{INFINITY, INFINITY, INFINITY}, {2.0f, 2.0f, 2.0f}, {outer, outer, outer}};
    }
    for (uint32_t i = 0; i < edges; i++)
    {
        MeasureSegment(&scratch->edge[i], multi->edgeColors[i], scratch->edgeSides[i], grid,
                       channels);
    }
    float scale = 128.0f / spread;
    for (size_t at = 0; at < area; at++)
    {
        float distance = sqrtf(scratch->distances[at]);
        float signedDistance = inside[at] != 0 ? distance : -distance;
        const float* value = channels[at].value;
        bool agrees = (Median(value[0], value[1], value[2]) > 0.0f) == (inside[at] != 0);
        unsigned char* pixel = pixels + at * 4;
        for (uint32_t k = 0; k < CHANNELS; k++)
        {
            pixel[k] = Encode(agrees ? value[k] : signedDistance, scale);
        }
        pixel[3] = Encode(signedDistance, scale);
    }
}
