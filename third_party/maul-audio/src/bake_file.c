// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake file (bake_file.h). Every number is written and read byte by
// byte in little-endian order, so the bytes do not depend on the host;
// reading checks the header, the exact size, the checksum, then every
// probe, row, neighbour (in range, rising, listed from both ends) and
// value, and allocates only once all of it holds. Link lengths are the
// probes' distances, computed as the graph's builder computes them.

#include "bake_file.h"

#include "allocator.h"
#include "crc32.h"

#include <math.h>
#include <string.h>

// Written; version 1 (without tails) is still read.
#define VERSION      2u
#define LAYER_REVERB 1u
#define LAYER_FIELDS 2u
#define MIN_RANGE    0.1f
#define MAX_RANGE    1000.0f
#define MAX_ORDER    3u
#define MAX_BINS     401u
#define MIN_TIME     0.1f
#define MAX_TIME     20.0f
#define MIN_LEVEL    (-96.0f)
#define MAX_LEVEL    24.0f

static const uint8_t MAGIC[8] = {'M', 'A', 'U', 'D', 'B', 'A', 'K', 'E'};

typedef struct Header
{
    uint32_t version;
    uint32_t probes;
    uint32_t links;
    float range;
    uint32_t layers;
    uint32_t order;
    uint32_t bins;
} Header;

// Where each section starts, and the end.
typedef struct Layout
{
    uint64_t probes;
    uint64_t rows;
    uint64_t neighbours;
    uint64_t times;
    uint64_t levels;
    uint64_t tailTimes;
    uint64_t tailLevels;
    uint64_t fields;
    uint64_t end;
} Layout;

static void PutU32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void PutF32(uint8_t* p, float v)
{
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    PutU32(p, bits);
}

static uint32_t U32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float F32(const uint8_t* p)
{
    uint32_t bits = U32(p);
    float v = 0.0f;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static uint64_t FieldFloats(const Header* h)
{
    return h->layers & LAYER_FIELDS
               ? (uint64_t)(h->order + 1) * (h->order + 1) * MAUD_DIRECT_BANDS * h->bins
               : 0;
}

static void Measure(const Header* h, Layout* l)
{
    uint64_t at = MAUD_BAKE_HEADER;
    l->probes = at;
    at += (uint64_t)h->probes * 12;
    l->rows = at;
    at += ((uint64_t)h->probes + 1) * 4;
    l->neighbours = at;
    at += (uint64_t)h->links * 8;
    bool reverb = (h->layers & LAYER_REVERB) != 0;
    l->times = at;
    at += reverb ? (uint64_t)h->probes * 12 : 0;
    l->levels = at;
    at += reverb ? (uint64_t)h->probes * 12 : 0;
    bool tails = reverb && h->version >= 2;
    l->tailTimes = at;
    at += tails ? (uint64_t)h->probes * 12 : 0;
    l->tailLevels = at;
    at += tails ? (uint64_t)h->probes * 12 : 0;
    l->fields = at;
    at += (uint64_t)h->probes * FieldFloats(h) * 4;
    l->end = at;
}

static Header HeaderOf(const maudProbeGraph* graph, const maudProbeBake* bake, uint32_t order,
                       uint32_t bins)
{
    bool baked = bake != nullptr && bake->memory != nullptr;
    bool fields = baked && bake->fieldFloats > 0;
    return (Header){VERSION,
                    graph->count,
                    graph->links,
                    graph->range,
                    baked ? (fields ? LAYER_REVERB | LAYER_FIELDS : LAYER_REVERB) : 0,
                    fields ? order : 0,
                    fields ? bins : 0};
}

size_t maudBakeFileBytes(const maudProbeGraph* graph, const maudProbeBake* bake,
                         uint32_t fieldOrder, uint32_t fieldBins)
{
    Header h = HeaderOf(graph, bake, fieldOrder, fieldBins);
    Layout l;
    Measure(&h, &l);
    return l.end <= (uint64_t)SIZE_MAX ? (size_t)l.end : 0;
}

static void PutFloats(uint8_t* out, const float* values, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i)
    {
        PutF32(out + 4 * i, values[i]);
    }
}

void maudWriteBakeFile(const maudProbeGraph* graph, const maudProbeBake* bake, uint32_t fieldOrder,
                       uint32_t fieldBins, uint8_t* out)
{
    Header h = HeaderOf(graph, bake, fieldOrder, fieldBins);
    Layout l;
    Measure(&h, &l);
    memcpy(out, MAGIC, sizeof(MAGIC));
    PutU32(out + 8, VERSION);
    PutU32(out + 12, h.probes);
    PutU32(out + 16, h.links);
    PutF32(out + 20, h.range);
    PutU32(out + 24, h.layers);
    PutU32(out + 28, h.order);
    PutU32(out + 32, h.bins);
    for (uint64_t i = 0; i < h.probes; ++i)
    {
        PutF32(out + l.probes + 12 * i, graph->points[i].x);
        PutF32(out + l.probes + 12 * i + 4, graph->points[i].y);
        PutF32(out + l.probes + 12 * i + 8, graph->points[i].z);
    }
    for (uint64_t i = 0; i <= h.probes; ++i)
    {
        PutU32(out + l.rows + 4 * i, graph->offsets[i]);
    }
    for (uint64_t e = 0; e < 2 * (uint64_t)h.links; ++e)
    {
        PutU32(out + l.neighbours + 4 * e, graph->neighbours[e]);
    }
    if (h.layers & LAYER_REVERB)
    {
        PutFloats(out + l.times, bake->times, (uint64_t)h.probes * MAUD_DIRECT_BANDS);
        PutFloats(out + l.levels, bake->levels, (uint64_t)h.probes * MAUD_DIRECT_BANDS);
        PutFloats(out + l.tailTimes, bake->tailTimes, (uint64_t)h.probes * MAUD_DIRECT_BANDS);
        PutFloats(out + l.tailLevels, bake->tailLevels, (uint64_t)h.probes * MAUD_DIRECT_BANDS);
    }
    if (h.layers & LAYER_FIELDS)
    {
        PutFloats(out + l.fields, bake->fields, (uint64_t)h.probes * FieldFloats(&h));
    }
    PutU32(out + 36, maudCrc32(out + MAUD_BAKE_HEADER, (size_t)l.end - MAUD_BAKE_HEADER));
}

static maudVector3 Point(const uint8_t* data, const Layout* l, uint32_t i)
{
    const uint8_t* p = data + l->probes + 12 * (uint64_t)i;
    return (maudVector3){F32(p), F32(p + 4), F32(p + 8)};
}

static float Distance(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

static uint32_t Row(const uint8_t* data, const Layout* l, uint32_t i)
{
    return U32(data + l->rows + 4 * (uint64_t)i);
}

static uint32_t Neighbour(const uint8_t* data, const Layout* l, uint32_t e)
{
    return U32(data + l->neighbours + 4 * (uint64_t)e);
}

// Whether probe j's row lists i: a bisection, the row rising.
static bool Lists(const uint8_t* data, const Layout* l, uint32_t j, uint32_t i)
{
    uint32_t low = Row(data, l, j);
    uint32_t high = Row(data, l, j + 1);
    while (low < high)
    {
        uint32_t mid = low + (high - low) / 2;
        uint32_t v = Neighbour(data, l, mid);
        if (v == i)
        {
            return true;
        }
        low = v < i ? mid + 1 : low;
        high = v < i ? high : mid;
    }
    return false;
}

static bool HeaderValid(const uint8_t* data, size_t size, Header* h, Layout* l, maudResult* why)
{
    *why = maud_errorInvalid;
    if (size < MAUD_BAKE_HEADER || memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    {
        return false;
    }
    uint32_t version = U32(data + 8);
    if (version < 1 || version > VERSION)
    {
        *why = maud_errorUnsupported;
        return false;
    }
    *h = (Header){version,        U32(data + 12), U32(data + 16), F32(data + 20),
                  U32(data + 24), U32(data + 28), U32(data + 32)};
    bool fields = h->layers == (LAYER_REVERB | LAYER_FIELDS);
    if (!(h->range >= MIN_RANGE && h->range <= MAX_RANGE) ||
        (h->layers != 0 && h->layers != LAYER_REVERB && !fields) ||
        (fields ? (h->order < 1 || h->order > MAX_ORDER || h->bins < 1 || h->bins > MAX_BINS)
                : (h->order != 0 || h->bins != 0)))
    {
        return false;
    }
    Measure(h, l);
    return l->end == (uint64_t)size &&
           maudCrc32(data + MAUD_BAKE_HEADER, size - MAUD_BAKE_HEADER) == U32(data + 36);
}

static bool LinksValid(const uint8_t* data, const Header* h, const Layout* l)
{
    if (Row(data, l, 0) != 0 || Row(data, l, h->probes) != 2 * (uint64_t)h->links)
    {
        return false;
    }
    for (uint32_t i = 0; i < h->probes; ++i)
    {
        maudVector3 p = Point(data, l, i);
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
            Row(data, l, i + 1) < Row(data, l, i))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < h->probes; ++i)
    {
        for (uint32_t e = Row(data, l, i); e < Row(data, l, i + 1); ++e)
        {
            uint32_t j = Neighbour(data, l, e);
            if (j >= h->probes || j == i ||
                (e > Row(data, l, i) && j <= Neighbour(data, l, e - 1)) ||
                !(Distance(Point(data, l, i), Point(data, l, j)) <= h->range) ||
                !Lists(data, l, j, i))
            {
                return false;
            }
        }
    }
    return true;
}

static bool ValuesValid(const uint8_t* data, const Header* h, const Layout* l)
{
    uint64_t values = (h->layers & LAYER_REVERB) ? (uint64_t)h->probes * MAUD_DIRECT_BANDS : 0;
    for (uint64_t i = 0; i < values; ++i)
    {
        float t = F32(data + l->times + 4 * i);
        float v = F32(data + l->levels + 4 * i);
        if (!(t >= MIN_TIME && t <= MAX_TIME) || !(v >= MIN_LEVEL && v <= MAX_LEVEL))
        {
            return false;
        }
    }
    uint64_t tails = h->version >= 2 ? values : 0;
    for (uint64_t i = 0; i < tails; ++i)
    {
        float t = F32(data + l->tailTimes + 4 * i);
        float v = F32(data + l->tailLevels + 4 * i);
        if (!(t == 0.0f || (t >= MIN_TIME && t <= MAX_TIME)) || !(v >= MIN_LEVEL && v <= MAX_LEVEL))
        {
            return false;
        }
    }
    uint64_t perProbe = FieldFloats(h);
    uint64_t w = (uint64_t)MAUD_DIRECT_BANDS * h->bins;
    for (uint64_t i = 0; i < (uint64_t)h->probes * perProbe; ++i)
    {
        float f = F32(data + l->fields + 4 * i);
        if (!isfinite(f) || (i % perProbe < w && f < 0.0f))
        {
            return false;
        }
    }
    return true;
}

static void Fill(const uint8_t* data, const Header* h, const Layout* l, maudProbeGraph* graph,
                 maudProbeBake* bake)
{
    for (uint32_t i = 0; i < h->probes; ++i)
    {
        graph->points[i] = Point(data, l, i);
    }
    for (uint32_t i = 0; i <= h->probes; ++i)
    {
        graph->offsets[i] = Row(data, l, i);
    }
    for (uint32_t i = 0; i < h->probes; ++i)
    {
        for (uint32_t e = graph->offsets[i]; e < graph->offsets[i + 1]; ++e)
        {
            graph->neighbours[e] = Neighbour(data, l, e);
            graph->lengths[e] = Distance(graph->points[i], graph->points[graph->neighbours[e]]);
        }
    }
    graph->range = h->range;
    uint64_t values = (h->layers & LAYER_REVERB) ? (uint64_t)h->probes * MAUD_DIRECT_BANDS : 0;
    for (uint64_t i = 0; i < values; ++i)
    {
        bake->times[i] = F32(data + l->times + 4 * i);
        bake->levels[i] = F32(data + l->levels + 4 * i);
        bool tail = h->version >= 2;
        bake->tailTimes[i] = tail ? F32(data + l->tailTimes + 4 * i) : 0.0f;
        bake->tailLevels[i] = tail ? F32(data + l->tailLevels + 4 * i) : MIN_LEVEL;
    }
    for (uint64_t i = 0; i < (uint64_t)h->probes * FieldFloats(h); ++i)
    {
        bake->fields[i] = F32(data + l->fields + 4 * i);
    }
}

maudResult maudReadBakeFile(const uint8_t* data, size_t size, const maudBakeLimits* limits,
                            const maudAllocator* allocator, maudProbeGraph* graph,
                            maudProbeBake* bake)
{
    *graph = (maudProbeGraph){0};
    *bake = (maudProbeBake){0};
    Header h;
    Layout l;
    maudResult why = maud_errorInvalid;
    if (data == nullptr || !HeaderValid(data, size, &h, &l, &why))
    {
        return why;
    }
    if (!LinksValid(data, &h, &l) || !ValuesValid(data, &h, &l))
    {
        return maud_errorInvalid;
    }
    if (h.probes > limits->maxProbes || h.links > limits->maxLinks)
    {
        return maud_errorCapacity;
    }
    if (h.layers != 0 && (h.order != limits->fieldOrder ||
                          h.bins != (limits->fieldOrder > 0 ? limits->fieldBins : 0)))
    {
        return maud_errorUnsupported;
    }
    if (!maudLayProbeGraph(allocator, graph, h.probes, h.links))
    {
        return maud_errorCapacity;
    }
    if (h.layers != 0 && !maudCreateProbeBake(allocator, h.probes, (uint32_t)FieldFloats(&h), bake))
    {
        maudReleaseProbeGraph(allocator, graph);
        return maud_errorCapacity;
    }
    Fill(data, &h, &l, graph, bake);
    return maud_success;
}
