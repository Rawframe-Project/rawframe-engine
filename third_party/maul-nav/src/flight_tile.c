// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight tile format and its loader.

#include "flight_tile.h"

#include "allocator.h"
#include "bytes.h"
#include "fields.h"
#include "flight.h"
#include "flight_def.h"
#include "invariant.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"
#include "maul-nav/navmesh.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint8_t MAGIC[4] = {'M', 'N', 'V', 'F'};

// FLHT's flags: the ground below is solid.
#define FLAG_GROUND_BELOW 1u

// The bytes of a cube's class, a node and a leaf, and of the FLHT field.
#define ROOT_BYTES   1
#define NODE_BYTES   2
#define LEAF_BYTES   8
#define FLIGHT_BYTES 56

// The fields, in order of their names, and the section each one's
// refusals name.
enum
{
    FIELD_FLIGHT,
    FIELD_LEAVES,
    FIELD_NODES,
    FIELD_ROOTS,
    FIELDS
};

static const uint32_t TAGS[FIELDS] = {
    MNAV_TAG('F', 'L', 'H', 'T'),
    MNAV_TAG('L', 'E', 'A', 'F'),
    MNAV_TAG('N', 'O', 'D', 'E'),
    MNAV_TAG('R', 'O', 'O', 'T'),
};

static const mnavTileSection SECTIONS[FIELDS] = {
    mnav_tileHeader,
    mnav_tileFlightLeaves,
    mnav_tileFlightNodes,
    mnav_tileFlightRoots,
};

static void WriteField(mnavByteWriter* w, int32_t field, const mnavFlightDef* def,
                       const mnavFlightTile* tile)
{
    switch (field)
    {
    case FIELD_FLIGHT:
        mnavPutU32(w, (uint32_t)tile->tileX);
        mnavPutU32(w, (uint32_t)tile->tileZ);
        mnavPutU16(w, (uint64_t)tile->side);
        mnavPutU16(w, def->groundBelow ? FLAG_GROUND_BELOW : 0u);
        mnavPutU32(w, mnavFloatBits(def->voxelSize));
        mnavPutU32(w, mnavFloatBits(def->radius));
        mnavPutU32(w, mnavFloatBits(def->floor));
        mnavPutU32(w, mnavFloatBits(def->ceiling));
        mnavPutU64(w, mnavDoubleBits(def->origin.x));
        mnavPutU64(w, mnavDoubleBits(def->origin.y));
        mnavPutU64(w, mnavDoubleBits(def->origin.z));
        mnavPutU32(w, (uint32_t)tile->floorVoxel);
        break;
    case FIELD_LEAVES:
        for (int32_t k = 0; k < tile->leafCount; ++k)
        {
            mnavPutU64(w, tile->leaves[k]);
        }
        break;
    case FIELD_NODES:
        for (int32_t n = 0; n < tile->nodeCount; ++n)
        {
            mnavPutU16(w, tile->nodes[n].mask);
        }
        break;
    default:
        for (int32_t c = 0; c < tile->cubeCount; ++c)
        {
            mnavPutU8(w, tile->roots[c]);
        }
        break;
    }
}

mnavResult mnavEncodeFlightTile(mnavMemory* memory, const mnavFlightDef* def, uint64_t fingerprint,
                                const mnavFlightTile* tile, uint8_t** bytes, size_t* size)
{
    *bytes = nullptr;
    *size = 0;
    const size_t sizes[FIELDS] = {FLIGHT_BYTES, (size_t)tile->leafCount * LEAF_BYTES,
                                  (size_t)tile->nodeCount * NODE_BYTES,
                                  (size_t)tile->cubeCount * ROOT_BYTES};
    size_t payload = 0;
    for (int32_t f = 0; f < FIELDS; ++f)
    {
        // An empty field holds its default and is left out.
        payload += sizes[f] > 0 ? mnavFieldBytes(sizes[f]) : 0;
    }
    size_t total = MNAV_FLIGHT_HEADER_BYTES + payload;
    uint8_t* out = nullptr;
    mnavResult result = mnavAllocate(memory, total, 1, 1, (void**)&out);
    if (result != mnav_success)
    {
        return result;
    }
    mnavByteWriter w = {out + MNAV_FLIGHT_HEADER_BYTES};
    uint64_t fields = 0;
    for (int32_t f = 0; f < FIELDS; ++f)
    {
        if (sizes[f] > 0)
        {
            mnavPutFieldHead(&w, TAGS[f], sizes[f]);
            WriteField(&w, f, def, tile);
            mnavPutFieldPad(&w, sizes[f]);
            fields += 1;
        }
    }
    MNAV_ASSERT(w.at == out + total);
    uint64_t hash = mnavHash64(MNAV_HASH_INIT, out + MNAV_FLIGHT_HEADER_BYTES, (int32_t)payload);
    w.at = out;
    for (int32_t k = 0; k < 4; ++k)
    {
        mnavPutU8(&w, MAGIC[k]);
    }
    mnavPutU16(&w, MNAV_FLIGHT_FORMAT);
    mnavPutU16(&w, MNAV_FLIGHT_GENERATOR);
    mnavPutU64(&w, fingerprint);
    mnavPutU64(&w, hash);
    mnavPutU32(&w, payload);
    mnavPutU32(&w, fields);
    *bytes = out;
    *size = total;
    return mnav_success;
}

void mnavReleaseFlightTileBytes(mnavMemory* memory, uint8_t* bytes, size_t size)
{
    mnavRelease(memory, bytes, size, 1, 1);
}

static mnavTileResult Refuse(mnavResult result, mnavTileSection section, int32_t index)
{
    return (mnavTileResult){result, section, index};
}

static mnavTileResult Accept(void)
{
    return (mnavTileResult){mnav_success, mnav_tileHeader, -1};
}

// Whether FLHT's settings are the def's and its shape the shape.
static bool SameSettings(mnavByteReader* r, const mnavFlightDef* def, const mnavFlightShape* shape)
{
    bool same = mnavGetU16(r) == (uint64_t)def->tileVoxels;
    same &= mnavGetU16(r) == (def->groundBelow ? FLAG_GROUND_BELOW : 0u);
    same &= mnavGetU32(r) == mnavFloatBits(def->voxelSize);
    same &= mnavGetU32(r) == mnavFloatBits(def->radius);
    same &= mnavGetU32(r) == mnavFloatBits(def->floor);
    same &= mnavGetU32(r) == mnavFloatBits(def->ceiling);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.x);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.y);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.z);
    same &= mnavGetU32(r) == (uint32_t)shape->floorVoxel;
    return same;
}

// Reads the header, finds the fields, which the payload's hash covers,
// and checks FLHT and the counts the lengths give.
static mnavTileResult ReadHeader(const uint8_t* bytes, size_t size, const mnavFlightDef* def,
                                 const mnavFlightShape* shape, mnavFlightTile* tile,
                                 mnavField fields[FIELDS], uint64_t* fingerprint)
{
    if (size < MNAV_FLIGHT_HEADER_BYTES || memcmp(bytes, MAGIC, sizeof(MAGIC)) != 0)
    {
        return Refuse(mnav_errorFormat, mnav_tileHeader, -1);
    }
    mnavByteReader r = {bytes + sizeof(MAGIC), MNAV_FLIGHT_HEADER_BYTES - sizeof(MAGIC), false};
    if (mnavGetU16(&r) != MNAV_FLIGHT_FORMAT)
    {
        return Refuse(mnav_errorVersion, mnav_tileHeader, -1);
    }
    (void)mnavGetU16(&r);
    *fingerprint = mnavGetU64(&r);
    uint64_t hash = mnavGetU64(&r);
    size_t payload = (size_t)mnavGetU32(&r);
    uint64_t count = mnavGetU32(&r);
    if (payload != size - MNAV_FLIGHT_HEADER_BYTES || payload > INT32_MAX ||
        mnavHash64(MNAV_HASH_INIT, bytes + MNAV_FLIGHT_HEADER_BYTES, (int32_t)payload) != hash)
    {
        return Refuse(mnav_errorFormat, mnav_tilePayload, -1);
    }
    mnavFieldsResult read =
        mnavReadFields(bytes + MNAV_FLIGHT_HEADER_BYTES, payload, count, TAGS, FIELDS, fields);
    if (read.result != mnav_success)
    {
        return read.known >= 0 ? Refuse(read.result, SECTIONS[read.known], -1)
                               : Refuse(read.result, mnav_tilePayload, read.at);
    }
    // An absent FLHT has no bytes.
    const mnavField* flight = &fields[FIELD_FLIGHT];
    if (flight->size != FLIGHT_BYTES)
    {
        return Refuse(mnav_errorFormat, mnav_tileHeader, -1);
    }
    r = (mnavByteReader){flight->bytes, flight->size, false};
    tile->tileX = (int32_t)(uint32_t)mnavGetU32(&r);
    tile->tileZ = (int32_t)(uint32_t)mnavGetU32(&r);
    if (!SameSettings(&r, def, shape))
    {
        return Refuse(mnav_errorFormat, mnav_tileHeader, -1);
    }
    if (fields[FIELD_ROOTS].size != (size_t)shape->cubeCount * ROOT_BYTES)
    {
        return Refuse(mnav_errorFormat, mnav_tileFlightRoots, -1);
    }
    if (fields[FIELD_NODES].size % NODE_BYTES != 0)
    {
        return Refuse(mnav_errorFormat, mnav_tileFlightNodes, -1);
    }
    if (fields[FIELD_LEAVES].size % LEAF_BYTES != 0)
    {
        return Refuse(mnav_errorFormat, mnav_tileFlightLeaves, -1);
    }
    size_t nodes = fields[FIELD_NODES].size / NODE_BYTES;
    size_t leaves = fields[FIELD_LEAVES].size / LEAF_BYTES;
    if (nodes > (size_t)def->limits.tileNodes || leaves > (size_t)def->limits.tileLeaves)
    {
        return Refuse(mnav_errorFormat, mnav_tileHeader, -1);
    }
    tile->side = def->tileVoxels;
    tile->floorVoxel = shape->floorVoxel;
    tile->cubeCount = shape->cubeCount;
    tile->nodeCount = (int32_t)nodes;
    tile->nodeCapacity = tile->nodeCount;
    tile->leafCount = (int32_t)leaves;
    tile->leafCapacity = tile->leafCount;
    return Accept();
}

// The bits set in a byte.
static int32_t Ones(uint32_t bits)
{
    int32_t count = 0;
    for (; bits != 0; bits &= bits - 1u)
    {
        ++count;
    }
    return count;
}

// Whether a node's mask is a mixed block's: no child both mixed and
// solid, and neither every child empty nor every child solid.
static bool MixedMask(uint32_t mask)
{
    uint32_t mixed = mask & 0xFFu;
    uint32_t solid = mask >> 8;
    return (mixed & solid) == 0 && (mixed != 0 || (solid != 0 && solid != 0xFFu));
}

// Where the node walk stands: the next node and leaf to give out.
typedef struct Walk
{
    int32_t node;
    int64_t leaf;
} Walk;

// Reads one cube's nodes, level by level from the root, giving each its
// first child: in the next level, or among the leaves below the last.
static mnavTileResult ReadCube(mnavByteReader* r, mnavFlightTile* tile, int32_t levels, Walk* w)
{
    int64_t count = 1;
    for (int32_t l = 0; l < levels; ++l)
    {
        int64_t base = w->node;
        int64_t next = 0;
        for (int64_t i = 0; i < count; ++i)
        {
            if (w->node >= tile->nodeCount)
            {
                return Refuse(mnav_errorFormat, mnav_tileFlightNodes, tile->nodeCount);
            }
            uint32_t mask = (uint32_t)mnavGetU16(r);
            if (!MixedMask(mask))
            {
                return Refuse(mnav_errorFormat, mnav_tileFlightNodes, w->node);
            }
            bool last = l == levels - 1;
            int64_t first = last ? w->leaf + next : base + count + next;
            tile->nodes[w->node] = (mnavFlightNode){(uint32_t)first, (uint16_t)mask};
            next += Ones(mask & 0xFFu);
            w->node += 1;
        }
        if (l == levels - 1)
        {
            w->leaf += next;
        }
        count = next;
    }
    return Accept();
}

static mnavTileResult ReadNodes(mnavByteReader* r, mnavFlightTile* tile)
{
    int32_t levels = 0;
    while ((4 << levels) < tile->side)
    {
        ++levels;
    }
    Walk w = {0, 0};
    for (int32_t c = 0; c < tile->cubeCount; ++c)
    {
        tile->rootNodes[c] = tile->roots[c] == MNAV_FLIGHT_MIXED ? w.node : -1;
        if (tile->roots[c] != MNAV_FLIGHT_MIXED)
        {
            continue;
        }
        mnavTileResult result = ReadCube(r, tile, levels, &w);
        if (result.result != mnav_success)
        {
            return result;
        }
    }
    if (w.node != tile->nodeCount)
    {
        return Refuse(mnav_errorFormat, mnav_tileFlightNodes, w.node);
    }
    if (w.leaf != tile->leafCount)
    {
        return Refuse(mnav_errorFormat, mnav_tileFlightLeaves, -1);
    }
    return Accept();
}

static mnavByteReader Over(const mnavField* field)
{
    return (mnavByteReader){field->bytes, field->size, false};
}

static mnavTileResult ReadPayload(const mnavField fields[FIELDS], mnavFlightTile* tile)
{
    mnavByteReader r = Over(&fields[FIELD_ROOTS]);
    for (int32_t c = 0; c < tile->cubeCount; ++c)
    {
        tile->roots[c] = (uint8_t)mnavGetU8(&r);
        if (tile->roots[c] > MNAV_FLIGHT_MIXED)
        {
            return Refuse(mnav_errorFormat, mnav_tileFlightRoots, c);
        }
    }
    r = Over(&fields[FIELD_NODES]);
    mnavTileResult result = ReadNodes(&r, tile);
    r = Over(&fields[FIELD_LEAVES]);
    for (int32_t k = 0; k < tile->leafCount && result.result == mnav_success; ++k)
    {
        tile->leaves[k] = mnavGetU64(&r);
        if (tile->leaves[k] == 0 || tile->leaves[k] == UINT64_MAX)
        {
            result = Refuse(mnav_errorFormat, mnav_tileFlightLeaves, k);
        }
    }
    return result;
}

static mnavResult Allocate(mnavMemory* memory, mnavFlightTile* tile)
{
    mnavResult r = mnavAllocate(memory, (size_t)tile->cubeCount, 1, 1, (void**)&tile->roots);
    r = r == mnav_success ? mnavAllocate(memory, (size_t)tile->cubeCount, sizeof(int32_t),
                                         alignof(int32_t), (void**)&tile->rootNodes)
                          : r;
    r = r == mnav_success && tile->nodeCapacity > 0
            ? mnavAllocate(memory, (size_t)tile->nodeCapacity, sizeof(mnavFlightNode),
                           alignof(mnavFlightNode), (void**)&tile->nodes)
            : r;
    return r == mnav_success && tile->leafCapacity > 0
               ? mnavAllocate(memory, (size_t)tile->leafCapacity, sizeof(uint64_t),
                              alignof(uint64_t), (void**)&tile->leaves)
               : r;
}

mnavTileResult mnavDecodeFlightTile(mnavMemory* memory, const mnavFlightDef* def,
                                    const mnavFlightShape* shape, const uint8_t* bytes, size_t size,
                                    mnavFlightTile* tile, uint64_t* fingerprintOut)
{
    *tile = (mnavFlightTile){0};
    mnavField fields[FIELDS];
    uint64_t fingerprint = 0;
    mnavTileResult result = ReadHeader(bytes, size, def, shape, tile, fields, &fingerprint);
    if (result.result != mnav_success)
    {
        *tile = (mnavFlightTile){0};
        return result;
    }
    mnavResult allocated = Allocate(memory, tile);
    result = allocated == mnav_success ? ReadPayload(fields, tile)
                                       : Refuse(allocated, mnav_tileHeader, -1);
    if (result.result != mnav_success)
    {
        mnavReleaseFlightTile(memory, tile);
        return result;
    }
    *fingerprintOut = fingerprint;
    return Accept();
}
