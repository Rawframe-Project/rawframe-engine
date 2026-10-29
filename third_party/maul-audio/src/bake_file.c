// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake file (bake_file.h). Every number is written and read byte by
// byte in little-endian order, so the bytes do not depend on the host;
// reading checks the header, the checksum, the fields (file_fields.h)
// and each one's exact length, then every probe, row, neighbour (in
// range, rising, listed from both ends) and value, and allocates only
// once all of it holds. Link lengths are the probes' distances, computed
// as the graph's builder computes them.

#include "bake_file.h"

#include "allocator.h"
#include "crc32.h"
#include "file_fields.h"

#include <math.h>
#include <string.h>

// The one version written and read, and the header before the fields:
// the magic, the version and the checksum of every byte after it.
#define VERSION      3u
#define HEADER_BYTES 16u
// The head field: probe count, link count, range.
#define HEAD_BYTES 12u
// The energy fields' layout field: order and bins.
#define LAYOUT_BYTES 8u
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

// The fields, in the order a writer puts them. The estimates and the
// energy fields (their layout, then their values) are the layers: a
// file has the estimates, or the estimates and the fields, or neither.
enum
{
    PART_HEAD,
    PART_PROBES,
    PART_ROWS,
    PART_NEIGHBOURS,
    PART_REVERB,
    PART_LAYOUT,
    PART_FIELDS,
    PART_COUNT
};

static const uint32_t s_tags[PART_COUNT] = {
    MAUD_FIELD_TAG('h', 'e', 'a', 'd'), MAUD_FIELD_TAG('p', 'r', 'o', 'b'),
    MAUD_FIELD_TAG('r', 'o', 'w', 's'), MAUD_FIELD_TAG('n', 'b', 'r', 's'),
    MAUD_FIELD_TAG('r', 'e', 'v', 'b'), MAUD_FIELD_TAG('f', 'h', 'd', 'r'),
    MAUD_FIELD_TAG('f', 'l', 'd', 's'),
};

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

// Where each part's values start, and the end.
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
               ? (uint64_t)(h->order + 1) * (h->order + 1) * MAUD_BANDS * h->bins
               : 0;
}

// Each part's length in bytes, 0 for a layer the file does not have.
static void PartBytes(const Header* h, uint64_t bytes[PART_COUNT])
{
    bool reverb = (h->layers & LAYER_REVERB) != 0;
    bool fields = (h->layers & LAYER_FIELDS) != 0;
    bytes[PART_HEAD] = HEAD_BYTES;
    bytes[PART_PROBES] = (uint64_t)h->probes * 12;
    bytes[PART_ROWS] = ((uint64_t)h->probes + 1) * 4;
    bytes[PART_NEIGHBOURS] = (uint64_t)h->links * 8;
    bytes[PART_REVERB] = reverb ? (uint64_t)h->probes * 12 * 4 : 0;
    bytes[PART_LAYOUT] = fields ? LAYOUT_BYTES : 0;
    bytes[PART_FIELDS] = (uint64_t)h->probes * FieldFloats(h) * 4;
}

// Whether the file has a part: the head, probes, rows and neighbours
// always, the layers as it has them.
static bool HasPart(const Header* h, int part)
{
    return part == PART_REVERB   ? (h->layers & LAYER_REVERB) != 0
           : part == PART_LAYOUT ? (h->layers & LAYER_FIELDS) != 0
           : part == PART_FIELDS ? (h->layers & LAYER_FIELDS) != 0
                                 : true;
}

// Points the layout at the parts' payloads, at their offsets in at.
static void Place(const Header* h, const uint64_t at[PART_COUNT], Layout* l)
{
    l->probes = at[PART_PROBES];
    l->rows = at[PART_ROWS];
    l->neighbours = at[PART_NEIGHBOURS];
    l->times = at[PART_REVERB];
    l->levels = l->times + (uint64_t)h->probes * 12;
    l->tailTimes = l->levels + (uint64_t)h->probes * 12;
    l->tailLevels = l->tailTimes + (uint64_t)h->probes * 12;
    l->fields = at[PART_FIELDS];
}

// Where the parts' values lie: each after the one before, framed and
// padded, from the header on; at gets each payload's offset.
static void Measure(const Header* h, Layout* l, uint64_t at[PART_COUNT])
{
    uint64_t bytes[PART_COUNT];
    PartBytes(h, bytes);
    uint64_t end = HEADER_BYTES;
    for (int part = 0; part < PART_COUNT; ++part)
    {
        at[part] = 0;
        if (HasPart(h, part))
        {
            at[part] = end + MAUD_FIELD_FRAME;
            end += maudFieldSize(bytes[part]);
        }
    }
    Place(h, at, l);
    l->end = end;
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
    uint64_t at[PART_COUNT];
    Measure(&h, &l, at);
    // A part's length is a uint32 in its frame.
    uint64_t bytes[PART_COUNT];
    PartBytes(&h, bytes);
    for (int part = 0; part < PART_COUNT; ++part)
    {
        if (bytes[part] > UINT32_MAX)
        {
            return 0;
        }
    }
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
    uint64_t at[PART_COUNT];
    Measure(&h, &l, at);
    uint64_t bytes[PART_COUNT];
    PartBytes(&h, bytes);
    memcpy(out, MAGIC, sizeof(MAGIC));
    PutU32(out + 8, VERSION);
    // A layer the bake does not have is left out, as its default.
    for (int part = 0; part < PART_COUNT; ++part)
    {
        if (at[part] != 0)
        {
            maudPutFieldFrame(out + at[part] - MAUD_FIELD_FRAME, s_tags[part],
                              (uint32_t)bytes[part]);
        }
    }
    PutU32(out + at[PART_HEAD], h.probes);
    PutU32(out + at[PART_HEAD] + 4, h.links);
    PutF32(out + at[PART_HEAD] + 8, h.range);
    if (h.layers & LAYER_FIELDS)
    {
        PutU32(out + at[PART_LAYOUT], h.order);
        PutU32(out + at[PART_LAYOUT] + 4, h.bins);
    }
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
        PutFloats(out + l.times, bake->times, (uint64_t)h.probes * MAUD_BANDS);
        PutFloats(out + l.levels, bake->levels, (uint64_t)h.probes * MAUD_BANDS);
        PutFloats(out + l.tailTimes, bake->tailTimes, (uint64_t)h.probes * MAUD_BANDS);
        PutFloats(out + l.tailLevels, bake->tailLevels, (uint64_t)h.probes * MAUD_BANDS);
    }
    if (h.layers & LAYER_FIELDS)
    {
        PutFloats(out + l.fields, bake->fields, (uint64_t)h.probes * FieldFloats(&h));
    }
    PutU32(out + 12, maudCrc32(out + HEADER_BYTES, (size_t)l.end - HEADER_BYTES));
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

// Reads the header and the fields into h and l: maud_errorVersion for
// another version or a field only a later one writes, maud_errorFormat
// for anything malformed. The layers are the parts present: the
// estimates alone, or with the energy fields' layout and values, or
// neither.
static maudResult HeaderValid(const uint8_t* data, size_t size, Header* h, Layout* l)
{
    if (size < HEADER_BYTES || memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    {
        return maud_errorFormat;
    }
    if (U32(data + 8) != VERSION)
    {
        return maud_errorVersion;
    }
    if (maudCrc32(data + HEADER_BYTES, size - HEADER_BYTES) != U32(data + 12))
    {
        return maud_errorFormat;
    }
    maudField parts[PART_COUNT];
    maudResult read = maudReadFields(data, size, HEADER_BYTES, s_tags, PART_COUNT, parts);
    if (read != maud_success)
    {
        return read;
    }
    bool fields = parts[PART_FIELDS].present;
    if (!parts[PART_HEAD].present || parts[PART_HEAD].length != HEAD_BYTES ||
        !parts[PART_PROBES].present || !parts[PART_ROWS].present ||
        !parts[PART_NEIGHBOURS].present || parts[PART_LAYOUT].present != fields ||
        (fields && (!parts[PART_REVERB].present || parts[PART_LAYOUT].length != LAYOUT_BYTES)))
    {
        return maud_errorFormat;
    }
    const uint8_t* head = parts[PART_HEAD].bytes;
    const uint8_t* layout = parts[PART_LAYOUT].bytes;
    *h = (Header){
        .version = VERSION,
        .probes = U32(head),
        .links = U32(head + 4),
        .range = F32(head + 8),
        .layers = (parts[PART_REVERB].present ? LAYER_REVERB : 0) | (fields ? LAYER_FIELDS : 0),
        .order = fields ? U32(layout) : 0,
        .bins = fields ? U32(layout + 4) : 0,
    };
    if (!(h->range >= MIN_RANGE && h->range <= MAX_RANGE) ||
        (fields && (h->order < 1 || h->order > MAX_ORDER || h->bins < 1 || h->bins > MAX_BINS)))
    {
        return maud_errorFormat;
    }
    // Every length the counts give fits in 64 bits: the counts are 32.
    uint64_t bytes[PART_COUNT];
    PartBytes(h, bytes);
    uint64_t at[PART_COUNT];
    for (int part = 0; part < PART_COUNT; ++part)
    {
        if (parts[part].present && parts[part].length != bytes[part])
        {
            return maud_errorFormat;
        }
        at[part] = parts[part].present ? (uint64_t)(parts[part].bytes - data) : 0;
    }
    Place(h, at, l);
    l->end = size;
    return maud_success;
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
    uint64_t values = (h->layers & LAYER_REVERB) ? (uint64_t)h->probes * MAUD_BANDS : 0;
    for (uint64_t i = 0; i < values; ++i)
    {
        float t = F32(data + l->times + 4 * i);
        float v = F32(data + l->levels + 4 * i);
        if (!(t >= MIN_TIME && t <= MAX_TIME) || !(v >= MIN_LEVEL && v <= MAX_LEVEL))
        {
            return false;
        }
    }
    for (uint64_t i = 0; i < values; ++i)
    {
        float t = F32(data + l->tailTimes + 4 * i);
        float v = F32(data + l->tailLevels + 4 * i);
        if (!(t == 0.0f || (t >= MIN_TIME && t <= MAX_TIME)) || !(v >= MIN_LEVEL && v <= MAX_LEVEL))
        {
            return false;
        }
    }
    uint64_t perProbe = FieldFloats(h);
    uint64_t w = (uint64_t)MAUD_BANDS * h->bins;
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
    uint64_t values = (h->layers & LAYER_REVERB) ? (uint64_t)h->probes * MAUD_BANDS : 0;
    for (uint64_t i = 0; i < values; ++i)
    {
        bake->times[i] = F32(data + l->times + 4 * i);
        bake->levels[i] = F32(data + l->levels + 4 * i);
        bake->tailTimes[i] = F32(data + l->tailTimes + 4 * i);
        bake->tailLevels[i] = F32(data + l->tailLevels + 4 * i);
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
    maudResult read = data != nullptr ? HeaderValid(data, size, &h, &l) : maud_errorInvalid;
    if (read != maud_success)
    {
        return read;
    }
    if (!LinksValid(data, &h, &l) || !ValuesValid(data, &h, &l))
    {
        return maud_errorFormat;
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
