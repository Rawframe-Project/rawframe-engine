// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The baker: one call runs every stage of the bake for a tile (mnav-0003).

#include "allocator.h"
#include "bake_def.h"
#include "border_vertices.h"
#include "compact.h"
#include "contour.h"
#include "detail.h"
#include "erode.h"
#include "filter.h"
#include "heightfield.h"
#include "holes.h"
#include "input.h"
#include "outline.h"
#include "polymesh.h"
#include "raster.h"
#include "region.h"
#include "terrain.h"
#include "tile.h"
#include "volume.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct mnavBaker
{
    mnavBakeDef def;
    mnavBakeCells cells;
    mnavMemory memory;
    uint8_t* tile;
    size_t tileSize;
};

// The stages' results for one tile, released together.
// A bake's input: triangle meshes and terrains, or for a flat (2D) bake,
// outlines.
typedef struct Input
{
    mnavBakeInput solid;
    const mnavOutline* outlines;
    int32_t outlineCount;
    bool flat;
} Input;

typedef struct Stages
{
    mnavHeightfield heightfield;
    mnavCompactField compact;
    mnavRegionMap regions;
    mnavContourSet set;
    mnavPolyMesh mesh;
    mnavDetailMesh detail;
} Stages;

mnavBakeDefResult mnavCreateBaker(const mnavBakeDef* def, mnavBaker** bakerOut)
{
    if (bakerOut == nullptr)
    {
        return (mnavBakeDefResult){mnav_errorInvalid, mnav_settingNone};
    }
    *bakerOut = nullptr;
    mnavBakeCells cells = {0};
    mnavBakeDefResult checked = mnavCheckBakeDef(def, &cells);
    if (checked.result != mnav_success)
    {
        return checked;
    }
    mnavMemory memory = mnavMakeMemory(def->allocator, def->limits.memoryBytes);
    mnavBaker* baker = nullptr;
    mnavResult result =
        mnavAllocate(&memory, 1, sizeof(mnavBaker), alignof(mnavBaker), (void**)&baker);
    if (result != mnav_success)
    {
        return (mnavBakeDefResult){result, mnav_settingNone};
    }
    *baker = (mnavBaker){*def, cells, memory, nullptr, 0};
    *bakerOut = baker;
    return (mnavBakeDefResult){mnav_success, mnav_settingNone};
}

static void DropTile(mnavBaker* baker)
{
    mnavReleaseTileBytes(&baker->memory, baker->tile, baker->tileSize);
    baker->tile = nullptr;
    baker->tileSize = 0;
}

void mnavDestroyBaker(mnavBaker* baker)
{
    if (baker == nullptr)
    {
        return;
    }
    DropTile(baker);
    mnavMemory memory = baker->memory;
    mnavRelease(&memory, baker, 1, sizeof(mnavBaker), alignof(mnavBaker));
}

// Checks every mesh as hostile input and their total against the input
// limit; names the first refused mesh in the report.
static mnavResult CheckOutlines(const mnavBaker* baker, const mnavOutline* outlines,
                                int32_t outlineCount, mnavBakeReport* report)
{
    if (outlineCount < 0 || (outlineCount > 0 && outlines == nullptr))
    {
        return mnav_errorInvalid;
    }
    int64_t total = 0;
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        mnavInputResult input = mnavCheckOutline(&baker->def, &outlines[o]);
        if (input.result != mnav_success)
        {
            report->mesh = o;
            report->input = input;
            return input.result;
        }
        total += outlines[o].pointCount;
    }
    return total > baker->def.limits.inputTriangles ? mnav_errorLimit : mnav_success;
}

static mnavResult CheckInput(const mnavBaker* baker, const Input* in, mnavBakeReport* report)
{
    if (in->flat)
    {
        return CheckOutlines(baker, in->outlines, in->outlineCount, report);
    }
    const mnavTriangleMesh* meshes = in->solid.meshes;
    int32_t meshCount = in->solid.meshCount;
    int32_t terrainCount = in->solid.terrainCount;
    int32_t volumeCount = in->solid.volumeCount;
    if (meshCount < 0 || (meshCount > 0 && meshes == nullptr) || terrainCount < 0 ||
        (terrainCount > 0 && in->solid.terrains == nullptr) || volumeCount < 0 ||
        (volumeCount > 0 && in->solid.volumes == nullptr))
    {
        return mnav_errorInvalid;
    }
    int64_t total = 0;
    for (int32_t m = 0; m < meshCount; ++m)
    {
        mnavInputResult input = mnavCheckTriangleMesh(&baker->def, &meshes[m]);
        if (input.result != mnav_success)
        {
            report->mesh = m;
            report->input = input;
            return input.result;
        }
        total += meshes[m].triangleCount;
    }
    for (int32_t i = 0; i < terrainCount; ++i)
    {
        mnavInputResult input = mnavCheckTerrain(&baker->def, &in->solid.terrains[i]);
        if (input.result != mnav_success)
        {
            report->mesh = meshCount + i;
            report->input = input;
            return input.result;
        }
        total += mnavTerrainTriangles(&in->solid.terrains[i]);
    }
    for (int32_t i = 0; i < volumeCount; ++i)
    {
        mnavInputResult input = mnavCheckVolume(&baker->def, &in->solid.volumes[i]);
        if (input.result != mnav_success)
        {
            report->mesh = meshCount + terrainCount + i;
            report->input = input;
            return input.result;
        }
        total += in->solid.volumes[i].pointCount;
    }
    return total > baker->def.limits.inputTriangles ? mnav_errorLimit : mnav_success;
}

static uint64_t HashWords(uint64_t hash, const uint32_t* words, int32_t count)
{
    return mnavHash64(hash, words, count * (int32_t)sizeof(uint32_t));
}

static uint32_t Bits32(float f)
{
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static uint64_t Bits64(double d)
{
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// The hash of the generator, the settings that shape the tile and its
// place.
static uint64_t HashSettings(const mnavBakeDef* def, int32_t tileX, int32_t tileZ)
{
    mnavVersion version = mnavGetVersion();
    uint64_t origin[3] = {Bits64(def->origin.x), Bits64(def->origin.y), Bits64(def->origin.z)};
    const uint32_t words[] = {
        version.major,
        version.minor,
        version.patch,
        (uint32_t)origin[0],
        (uint32_t)(origin[0] >> 32),
        (uint32_t)origin[1],
        (uint32_t)(origin[1] >> 32),
        (uint32_t)origin[2],
        (uint32_t)(origin[2] >> 32),
        Bits32(def->cellSize),
        Bits32(def->cellHeight),
        (uint32_t)def->tileCells,
        Bits32(def->agent.radius),
        Bits32(def->agent.height),
        Bits32(def->agent.stepHeight),
        Bits32(def->agent.maxSlopeDegrees),
        Bits32(def->minRegionArea),
        Bits32(def->maxEdgeError),
        Bits32(def->maxEdgeLength),
        Bits32(def->detailSampleDistance),
        Bits32(def->detailMaxError),
        (uint32_t)tileX,
        (uint32_t)tileZ,
    };
    return HashWords(MNAV_HASH_INIT, words, (int32_t)(sizeof(words) / sizeof(words[0])));
}

// Adds a triangle that reaches the tile, as rasterization picks it, to
// the fingerprint, and counts it.
static uint64_t HashTriangle(const mnavBaker* baker, const mnavTileFrame* frame,
                             const mnavVec3 corners[3], mnavAreaType given, uint64_t hash,
                             int32_t* count)
{
    int32_t area = mnavTriangleArea(corners, given, baker->cells.cosMaxSlope);
    if (area < 0 || !mnavTriangleTouchesTile(frame, corners))
    {
        return hash;
    }
    uint32_t words[10];
    for (int32_t c = 0; c < 3; ++c)
    {
        words[c * 3 + 0] = Bits32(corners[c].x);
        words[c * 3 + 1] = Bits32(corners[c].y);
        words[c * 3 + 2] = Bits32(corners[c].z);
    }
    words[9] = (uint32_t)area;
    *count += 1;
    return HashWords(hash, words, 10);
}

static uint64_t HashTerrain(const mnavBaker* baker, const mnavTileFrame* frame,
                            const mnavTerrain* terrain, uint64_t hash, int32_t* count)
{
    int32_t c0 = 0;
    int32_t c1 = -1;
    int32_t r0 = 0;
    int32_t r1 = -1;
    if (!mnavTerrainCells(terrain, frame, &c0, &c1, &r0, &r1))
    {
        return hash;
    }
    for (int32_t r = r0; r <= r1; ++r)
    {
        for (int32_t c = c0; c <= c1; ++c)
        {
            for (int32_t k = 0; k < 2; ++k)
            {
                mnavVec3 corners[3];
                mnavAreaType given = 0;
                if (mnavTerrainTriangle(terrain, c, r, k, corners, &given))
                {
                    hash = HashTriangle(baker, frame, corners, given, hash, count);
                }
            }
        }
    }
    return hash;
}

// Adds the volumes that reach the tile to the fingerprint, after a word
// for include volumes, which reach every tile, when there are any; a bake
// whose volumes all miss the tile, none of them include volumes, has the
// fingerprint of one without.
static uint64_t HashVolumes(const mnavTileFrame* frame, const mnavBakeVolume* volumes,
                            int32_t count, uint64_t hash)
{
    if (count == 0)
    {
        return hash;
    }
    uint32_t includes = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        includes |= volumes[i].kind == mnav_volumeInclude ? 1u : 0u;
    }
    hash = includes != 0 ? HashWords(hash, &includes, 1) : hash;
    for (int32_t i = 0; i < count; ++i)
    {
        const mnavBakeVolume* v = &volumes[i];
        if (!mnavRingTouchesTile(frame, v->points, v->pointCount))
        {
            continue;
        }
        const uint32_t head[5] = {(uint32_t)v->kind, (uint32_t)v->area, Bits32(v->minY),
                                  Bits32(v->maxY), (uint32_t)v->pointCount};
        hash = HashWords(hash, head, 5);
        for (int32_t p = 0; p < v->pointCount; ++p)
        {
            const uint32_t point[2] = {Bits32(v->points[p].x), Bits32(v->points[p].y)};
            hash = HashWords(hash, point, 2);
        }
    }
    return hash;
}

// Adds every input triangle that reaches the tile, as rasterization picks
// them, meshes' then terrains', and the volumes to the fingerprint, and
// counts the triangles.
static uint64_t HashInput(const mnavBaker* baker, const mnavTileFrame* frame,
                          const mnavBakeInput* input, uint64_t hash, int32_t* count)
{
    *count = 0;
    for (int32_t m = 0; m < input->meshCount; ++m)
    {
        const mnavTriangleMesh* mesh = &input->meshes[m];
        for (int32_t t = 0; t < mesh->triangleCount; ++t)
        {
            const int32_t* index = mesh->indices + (size_t)t * 3;
            const mnavVec3 corners[3] = {mesh->vertices[index[0]], mesh->vertices[index[1]],
                                         mesh->vertices[index[2]]};
            mnavAreaType given = mesh->areas != nullptr ? mesh->areas[t] : mnav_areaWalkable;
            hash = HashTriangle(baker, frame, corners, given, hash, count);
        }
    }
    for (int32_t i = 0; i < input->terrainCount; ++i)
    {
        hash = HashTerrain(baker, frame, &input->terrains[i], hash, count);
    }
    return HashVolumes(frame, input->volumes, input->volumeCount, hash);
}

// The detail lookup's search radius: the wall error rounded up, at least
// one cell.
// Adds every outline that reaches the tile to the fingerprint, after a
// word that keeps 2D input apart from 3D, and counts them.
static uint64_t HashOutlines(const mnavTileFrame* frame, const mnavOutline* outlines,
                             int32_t outlineCount, uint64_t hash, int32_t* count)
{
    const uint32_t tag = 0x32444E41u;
    hash = HashWords(hash, &tag, 1);
    *count = 0;
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        const mnavOutline* outline = &outlines[o];
        if (!mnavOutlineTouchesTile(frame, outline))
        {
            continue;
        }
        *count += 1;
        uint32_t head[2] = {(uint32_t)outline->pointCount, (uint32_t)outline->area};
        hash = HashWords(hash, head, 2);
        for (int32_t i = 0; i < outline->pointCount; ++i)
        {
            uint32_t words[2] = {Bits32(outline->points[i].x), Bits32(outline->points[i].y)};
            hash = HashWords(hash, words, 2);
        }
    }
    return hash;
}

static int32_t SearchRadius(float edgeError)
{
    int32_t radius = (int32_t)ceilf(edgeError);
    return radius < 1 ? 1 : radius;
}

// Runs the stages in order, recording each in the report before it runs.
static mnavResult RunStages(mnavBaker* baker, const Input* in, int32_t tileX, int32_t tileZ,
                            Stages* s, mnavBakeReport* report)
{
    mnavMemory* memory = &baker->memory;
    const mnavBakeDef* def = &baker->def;
    const mnavBakeCells* cells = &baker->cells;
    report->stage = mnav_stageRasterize;
    bool flat = in->flat;
    mnavResult result =
        flat ? mnavBuildHeightfield2D(memory, def, cells, in->outlines, in->outlineCount, tileX,
                                      tileZ, &s->heightfield)
             : mnavBuildHeightfieldInput(memory, def, cells, &in->solid, tileX, tileZ,
                                         &s->heightfield);
    if (result != mnav_success)
    {
        return result;
    }
    // The walkable filters judge steps, ledges and clearance from heights;
    // a 2D bake has none, and its edges are walls, not ledges.
    if (!flat)
    {
        mnavFilterWalkable(&s->heightfield, cells->agentHeight, cells->agentStep);
    }
    report->stage = mnav_stageCompact;
    result = mnavBuildCompactField(memory, &s->heightfield, cells->agentHeight, cells->agentStep,
                                   &s->compact);
    if (result == mnav_success)
    {
        result = mnavCarveVolumes(memory, &s->compact, in->solid.volumes, in->solid.volumeCount);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageErode;
        result = mnavErode(memory, &s->compact, cells->agentRadius);
    }
    if (result == mnav_success)
    {
        result = mnavMarkVolumes(memory, &s->compact, in->solid.volumes, in->solid.volumeCount);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageRegions;
        result =
            mnavBuildRegions(memory, &s->compact, cells->border, cells->minRegion, &s->regions);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageContours;
        result = mnavBuildContours(memory, &s->compact, &s->regions, cells->border,
                                   cells->edgeError, cells->edgeLength, &s->set);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageHoles;
        result = mnavMergeHoles(memory, &s->set, s->regions.count);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stagePolygons;
        result = mnavBuildPolyMesh(memory, &s->set, def->tileCells, def->limits.tileVertices,
                                   def->limits.tilePolygons, &s->mesh);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageBorderVertices;
        result = mnavRemoveBorderVertices(memory, &s->mesh, def->limits.tilePolygons);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageLinks;
        result = mnavLinkPolyMesh(memory, &s->mesh);
    }
    if (result == mnav_success)
    {
        report->stage = mnav_stageDetail;
        mnavDetailSettings settings = {cells->detailSample, cells->detailError,
                                       SearchRadius(cells->edgeError), cells->border};
        result =
            mnavBuildDetailMesh(memory, &s->compact, &s->regions, &s->mesh, settings, &s->detail);
    }
    return result;
}

static void ReleaseStages(mnavMemory* memory, Stages* s)
{
    mnavReleaseDetailMesh(memory, &s->detail);
    mnavReleasePolyMesh(memory, &s->mesh);
    mnavReleaseContours(memory, &s->set);
    mnavReleaseRegions(memory, &s->regions);
    mnavReleaseCompactField(memory, &s->compact);
    mnavReleaseHeightfield(memory, &s->heightfield);
}

// Writes the stages' counts into the report.
static void Count(const Stages* s, mnavBakeReport* report)
{
    report->spans = s->compact.spanCount;
    report->regions = (int32_t)s->regions.count;
    report->droppedRegions = (int32_t)s->regions.dropped;
    report->polygons = s->mesh.polygonCount;
    report->vertices = s->mesh.vertexCount;
    report->detailVertices = s->detail.vertexCount;
    report->detailTriangles = s->detail.triangleCount;
    report->droppedHoles = s->set.droppedHoles;
    report->partialRings = s->mesh.failedRings + s->detail.failedPolygons;
    report->fallbackHeights = s->detail.fallbackHeights;
    report->cappedDetail = s->detail.cappedPolygons;
}

// Encodes the tile from the stages.
static mnavResult Encode(mnavBaker* baker, int32_t tileX, int32_t tileZ, const Stages* s,
                         uint64_t fingerprint)
{
    const mnavBakeDef* def = &baker->def;
    const mnavBakeCells* cells = &baker->cells;
    mnavTileInfo info = {mnavGetVersion(),   fingerprint,      tileX,           tileZ,
                         def->tileCells,     def->cellSize,    def->cellHeight, cells->agentHeight,
                         cells->agentRadius, cells->agentStep, def->origin};
    return mnavEncodeTile(&baker->memory, &info, &s->mesh, &s->detail, &baker->tile,
                          &baker->tileSize);
}

static mnavResult Bake(mnavBaker* baker, const Input* in, int32_t tileX, int32_t tileZ,
                       mnavBakeReport* reportOut)
{
    mnavBakeReport report = {0};
    report.mesh = -1;
    report.input = (mnavInputResult){mnav_success, mnav_elementNone, -1};
    if (baker == nullptr)
    {
        report.result = mnav_errorInvalid;
        if (reportOut != nullptr)
        {
            *reportOut = report;
        }
        return mnav_errorInvalid;
    }
    DropTile(baker);
    baker->memory.peak = baker->memory.used;
    report.stage = mnav_stageInput;
    mnavResult result = CheckInput(baker, in, &report);
    mnavTileFrame frame = {0};
    if (result == mnav_success &&
        !mnavMakeTileFrame(&baker->def, &baker->cells, tileX, tileZ, &frame))
    {
        result = mnav_errorRange;
    }
    Stages stages = {0};
    if (result == mnav_success)
    {
        uint64_t settings = HashSettings(&baker->def, tileX, tileZ);
        report.fingerprint =
            in->flat
                ? HashOutlines(&frame, in->outlines, in->outlineCount, settings, &report.triangles)
                : HashInput(baker, &frame, &in->solid, settings, &report.triangles);
        result = RunStages(baker, in, tileX, tileZ, &stages, &report);
    }
    if (result == mnav_success)
    {
        Count(&stages, &report);
        report.stage = mnav_stageEncode;
        result = Encode(baker, tileX, tileZ, &stages, report.fingerprint);
    }
    ReleaseStages(&baker->memory, &stages);
    if (result == mnav_success)
    {
        report.stage = mnav_stageDone;
        report.tileBytes = baker->tileSize;
    }
    report.result = result;
    report.memoryPeak = baker->memory.peak;
    if (reportOut != nullptr)
    {
        *reportOut = report;
    }
    return result;
}

mnavResult mnavBakeTile(mnavBaker* baker, const mnavTriangleMesh* meshes, int32_t meshCount,
                        int32_t tileX, int32_t tileZ, mnavBakeReport* reportOut)
{
    Input in = {{meshes, meshCount, nullptr, 0, nullptr, 0}, nullptr, 0, false};
    return Bake(baker, &in, tileX, tileZ, reportOut);
}

mnavResult mnavBakeTileInput(mnavBaker* baker, const mnavBakeInput* input, int32_t tileX,
                             int32_t tileZ, mnavBakeReport* reportOut)
{
    if (input == nullptr)
    {
        Input none = {{nullptr, -1, nullptr, 0, nullptr, 0}, nullptr, 0, false};
        return Bake(baker, &none, tileX, tileZ, reportOut);
    }
    Input in = {*input, nullptr, 0, false};
    return Bake(baker, &in, tileX, tileZ, reportOut);
}

mnavResult mnavBakeTile2D(mnavBaker* baker, const mnavOutline* outlines, int32_t outlineCount,
                          int32_t tileX, int32_t tileZ, mnavBakeReport* reportOut)
{
    Input in = {{nullptr, 0, nullptr, 0, nullptr, 0}, outlines, outlineCount, true};
    return Bake(baker, &in, tileX, tileZ, reportOut);
}

mnavResult mnavCopyBakedTile(const mnavBaker* baker, uint8_t* buffer, size_t capacity,
                             size_t* sizeOut)
{
    if (baker == nullptr || (buffer == nullptr && capacity > 0) || baker->tile == nullptr)
    {
        return mnav_errorInvalid;
    }
    if (sizeOut != nullptr)
    {
        *sizeOut = baker->tileSize;
    }
    if (capacity < baker->tileSize)
    {
        return mnav_errorCapacity;
    }
    if (buffer != nullptr)
    {
        memcpy(buffer, baker->tile, baker->tileSize);
    }
    return mnav_success;
}
