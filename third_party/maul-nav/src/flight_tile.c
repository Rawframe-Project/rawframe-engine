// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flight tile format and its loader (mnav-0015).

#include "flight_tile.h"

#include "allocator.h"
#include "bytes.h"
#include "flight.h"
#include "flight_def.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"
#include "maul-nav/navmesh.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t MAGIC[4] = {'M', 'N', 'V', 'F'};

// The header's flags: the ground below is solid.
#define FLAG_GROUND_BELOW 1u

// The bytes of a node and of a leaf.
#define NODE_BYTES 2
#define LEAF_BYTES 8

static size_t PayloadBytes(int64_t cubes, int64_t nodes, int64_t leaves)
{
    return (size_t)(cubes + nodes * NODE_BYTES + leaves * LEAF_BYTES);
}

static void WriteHeader(mnavByteWriter* w, const mnavFlightDef* def, mnavVersion generator,
                        uint64_t fingerprint, const mnavFlightTile* tile, size_t payload,
                        uint64_t hash)
{
    for (int32_t k = 0; k < 4; ++k)
    {
        mnavPutU8(w, MAGIC[k]);
    }
    mnavPutU16(w, MNAV_FLIGHT_FORMAT);
    mnavPutU16(w, MNAV_FLIGHT_HEADER_BYTES);
    mnavPutU16(w, generator.major);
    mnavPutU16(w, generator.minor);
    mnavPutU16(w, generator.patch);
    mnavPutU16(w, def->groundBelow ? FLAG_GROUND_BELOW : 0u);
    mnavPutU64(w, fingerprint);
    mnavPutU64(w, hash);
    mnavPutU32(w, payload);
    mnavPutU32(w, (uint32_t)tile->tileX);
    mnavPutU32(w, (uint32_t)tile->tileZ);
    mnavPutU16(w, (uint64_t)tile->side);
    mnavPutU16(w, 0);
    mnavPutU32(w, mnavFloatBits(def->voxelSize));
    mnavPutU32(w, mnavFloatBits(def->radius));
    mnavPutU32(w, mnavFloatBits(def->floor));
    mnavPutU32(w, mnavFloatBits(def->ceiling));
    mnavPutU64(w, mnavDoubleBits(def->origin.x));
    mnavPutU64(w, mnavDoubleBits(def->origin.y));
    mnavPutU64(w, mnavDoubleBits(def->origin.z));
    mnavPutU32(w, (uint32_t)tile->floorVoxel);
    mnavPutU32(w, (uint64_t)tile->cubeCount);
    mnavPutU32(w, (uint64_t)tile->nodeCount);
    mnavPutU32(w, (uint64_t)tile->leafCount);
}

mnavResult mnavEncodeFlightTile(mnavMemory* memory, const mnavFlightDef* def, mnavVersion generator,
                                uint64_t fingerprint, const mnavFlightTile* tile, uint8_t** bytes,
                                size_t* size)
{
    *bytes = nullptr;
    *size = 0;
    size_t payload = PayloadBytes(tile->cubeCount, tile->nodeCount, tile->leafCount);
    size_t total = MNAV_FLIGHT_HEADER_BYTES + payload;
    uint8_t* out = nullptr;
    mnavResult result = mnavAllocate(memory, total, 1, 1, (void**)&out);
    if (result != mnav_success)
    {
        return result;
    }
    mnavByteWriter w = {out + MNAV_FLIGHT_HEADER_BYTES};
    for (int32_t c = 0; c < tile->cubeCount; ++c)
    {
        mnavPutU8(&w, tile->roots[c]);
    }
    for (int32_t n = 0; n < tile->nodeCount; ++n)
    {
        mnavPutU16(&w, tile->nodes[n].mask);
    }
    for (int32_t k = 0; k < tile->leafCount; ++k)
    {
        mnavPutU64(&w, tile->leaves[k]);
    }
    uint64_t hash = mnavHash64(MNAV_HASH_INIT, out + MNAV_FLIGHT_HEADER_BYTES, (int32_t)payload);
    w.at = out;
    WriteHeader(&w, def, generator, fingerprint, tile, payload, hash);
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

// The header's fields beside the settings, which are checked as read.
typedef struct Header
{
    uint64_t fingerprint;
    uint64_t hash;
    uint64_t payload;
    int64_t nodes;
    int64_t leaves;
} Header;

// Whether the header's settings are the def's and its shape the shape.
static bool SameSettings(mnavByteReader* r, const mnavFlightDef* def, const mnavFlightShape* shape)
{
    bool same = mnavGetU16(r) == (uint64_t)def->tileVoxels;
    same &= mnavGetU16(r) == 0;
    same &= mnavGetU32(r) == mnavFloatBits(def->voxelSize);
    same &= mnavGetU32(r) == mnavFloatBits(def->radius);
    same &= mnavGetU32(r) == mnavFloatBits(def->floor);
    same &= mnavGetU32(r) == mnavFloatBits(def->ceiling);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.x);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.y);
    same &= mnavGetU64(r) == mnavDoubleBits(def->origin.z);
    same &= mnavGetU32(r) == (uint32_t)shape->floorVoxel;
    same &= mnavGetU32(r) == (uint64_t)shape->cubeCount;
    return same;
}

static mnavTileResult ReadHeader(const uint8_t* bytes, size_t size, const mnavFlightDef* def,
                                 const mnavFlightShape* shape, mnavFlightTile* tile, Header* h)
{
    mnavByteReader r = {bytes, size, false};
    bool magic = true;
    for (int32_t k = 0; k < 4; ++k)
    {
        magic &= mnavGetU8(&r) == MAGIC[k];
    }
    if (!magic || size < MNAV_FLIGHT_HEADER_BYTES)
    {
        return Refuse(mnav_errorInvalid, mnav_tileHeader, -1);
    }
    if (mnavGetU16(&r) != MNAV_FLIGHT_FORMAT)
    {
        return Refuse(mnav_errorVersion, mnav_tileHeader, -1);
    }
    bool valid = mnavGetU16(&r) == MNAV_FLIGHT_HEADER_BYTES;
    for (int32_t k = 0; k < 3; ++k)
    {
        (void)mnavGetU16(&r);
    }
    valid &= mnavGetU16(&r) == (def->groundBelow ? FLAG_GROUND_BELOW : 0u);
    h->fingerprint = mnavGetU64(&r);
    h->hash = mnavGetU64(&r);
    h->payload = mnavGetU32(&r);
    tile->tileX = (int32_t)(uint32_t)mnavGetU32(&r);
    tile->tileZ = (int32_t)(uint32_t)mnavGetU32(&r);
    valid &= SameSettings(&r, def, shape);
    h->nodes = (int64_t)mnavGetU32(&r);
    h->leaves = (int64_t)mnavGetU32(&r);
    if (!valid)
    {
        return Refuse(mnav_errorInvalid, mnav_tileHeader, -1);
    }
    if (h->nodes > def->limits.tileNodes || h->leaves > def->limits.tileLeaves)
    {
        return Refuse(mnav_errorLimit, mnav_tileHeader, -1);
    }
    tile->side = def->tileVoxels;
    tile->floorVoxel = shape->floorVoxel;
    tile->cubeCount = shape->cubeCount;
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
                return Refuse(mnav_errorInvalid, mnav_tileFlightNodes, tile->nodeCount);
            }
            uint32_t mask = (uint32_t)mnavGetU16(r);
            if (!MixedMask(mask))
            {
                return Refuse(mnav_errorInvalid, mnav_tileFlightNodes, w->node);
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
        return Refuse(mnav_errorInvalid, mnav_tileFlightNodes, w.node);
    }
    if (w.leaf != tile->leafCount)
    {
        return Refuse(mnav_errorInvalid, mnav_tileFlightLeaves, -1);
    }
    return Accept();
}

static mnavTileResult ReadPayload(mnavByteReader* r, mnavFlightTile* tile)
{
    for (int32_t c = 0; c < tile->cubeCount; ++c)
    {
        tile->roots[c] = (uint8_t)mnavGetU8(r);
        if (tile->roots[c] > MNAV_FLIGHT_MIXED)
        {
            return Refuse(mnav_errorInvalid, mnav_tileFlightRoots, c);
        }
    }
    mnavTileResult result = ReadNodes(r, tile);
    for (int32_t k = 0; k < tile->leafCount && result.result == mnav_success; ++k)
    {
        tile->leaves[k] = mnavGetU64(r);
        if (tile->leaves[k] == 0 || tile->leaves[k] == UINT64_MAX)
        {
            result = Refuse(mnav_errorInvalid, mnav_tileFlightLeaves, k);
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
    Header h = {0};
    mnavTileResult result = ReadHeader(bytes, size, def, shape, tile, &h);
    if (result.result != mnav_success)
    {
        *tile = (mnavFlightTile){0};
        return result;
    }
    size_t payload = PayloadBytes(tile->cubeCount, h.nodes, h.leaves);
    if (h.payload != payload || size - MNAV_FLIGHT_HEADER_BYTES != payload ||
        mnavHash64(MNAV_HASH_INIT, bytes + MNAV_FLIGHT_HEADER_BYTES, (int32_t)payload) != h.hash)
    {
        *tile = (mnavFlightTile){0};
        return Refuse(mnav_errorInvalid, mnav_tilePayload, -1);
    }
    tile->nodeCount = (int32_t)h.nodes;
    tile->nodeCapacity = tile->nodeCount;
    tile->leafCount = (int32_t)h.leaves;
    tile->leafCapacity = tile->leafCount;
    mnavResult allocated = Allocate(memory, tile);
    mnavByteReader r = {bytes + MNAV_FLIGHT_HEADER_BYTES, payload, false};
    result =
        allocated == mnav_success ? ReadPayload(&r, tile) : Refuse(allocated, mnav_tileHeader, -1);
    if (result.result != mnav_success)
    {
        mnavReleaseFlightTile(memory, tile);
        return result;
    }
    *fingerprintOut = h.fingerprint;
    return Accept();
}
