// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The baker: one call runs every stage of the bake for a tile (mnav-0003).

#include "allocator.h"
#include "bake_def.h"
#include "bake_input.h"
#include "border_vertices.h"
#include "bytes.h"
#include "compact.h"
#include "contour.h"
#include "detail.h"
#include "erode.h"
#include "field_pack.h"
#include "filter.h"
#include "fingerprint.h"
#include "heightfield.h"
#include "holes.h"
#include "input.h"
#include "outline.h"
#include "polymesh.h"
#include "raster.h"
#include "region.h"
#include "terrain.h"
#include "tile.h"
#include "tile_cache.h"
#include "tile_index.h"
#include "volume.h"

#include "maul-nav/bake.h"
#include "maul-nav/base.h"

#include <math.h>
#include <stdckdint.h>
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
    // The clock's ticks when the stage being run began.
    uint64_t stageStart;
};

// The stages' results for one tile, released together.
// A bake's input: triangle meshes and terrains, or for a flat (2D) bake,
// outlines.
typedef struct Input
{
    mnavBakeInput solid;
    const mnavOutline* outlines;
    int32_t outlineCount;
    const mnavTileIndex* outlineIndex;
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
    *baker = (mnavBaker){*def, cells, memory, nullptr, 0, 0};
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
// With a tile index, checks that it fits and only the outlines it lists
// for the tile; the total counts every outline's points.
static mnavResult CheckOutlines(const mnavBaker* baker, const Input* in, int32_t tileX,
                                int32_t tileZ, mnavBakeReport* report)
{
    const mnavOutline* outlines = in->outlines;
    int32_t outlineCount = in->outlineCount;
    if (outlineCount < 0 || (outlineCount > 0 && outlines == nullptr) ||
        (in->outlineIndex != nullptr &&
         !mnavTileIndexFits2D(in->outlineIndex, &baker->def, &baker->cells, outlines,
                              outlineCount)))
    {
        return mnav_errorInvalid;
    }
    const mnavOutlineSet set =
        mnavOutlinesFor(outlines, outlineCount, in->outlineIndex, tileX, tileZ);
    for (int32_t k = 0; k < set.count; ++k)
    {
        const mnavOutline* outline = mnavOutlineAt(&set, k);
        mnavInputResult input = mnavCheckOutline(&baker->def, outline);
        if (input.result != mnav_success)
        {
            report->mesh = (int32_t)(outline - outlines);
            report->input = input;
            return input.result;
        }
    }
    int64_t total = 0;
    for (int32_t o = 0; o < outlineCount; ++o)
    {
        total += outlines[o].pointCount;
    }
    return total > baker->def.limits.inputTriangles ? mnav_errorLimit : mnav_success;
}

// Checks, with a tile index, that it fits, each mesh's counts and
// pointers, and the triangles it lists for the tile.
static mnavResult CheckIndexed(const mnavBaker* baker, const mnavBakeInput* solid, int32_t tileX,
                               int32_t tileZ, mnavBakeReport* report)
{
    if (!mnavTileIndexFits(solid->index, &baker->def, &baker->cells, solid->meshes,
                           solid->meshCount))
    {
        return mnav_errorInvalid;
    }
    for (int32_t m = 0; m < solid->meshCount; ++m)
    {
        mnavInputResult input = mnavCheckMeshShape(&baker->def, &solid->meshes[m]);
        if (input.result != mnav_success)
        {
            report->mesh = m;
            report->input = input;
            return input.result;
        }
    }
    const mnavIndexEntry* list = nullptr;
    int32_t count = 0;
    mnavTileIndexList(solid->index, tileX, tileZ, &list, &count);
    for (int32_t k = 0; k < count; ++k)
    {
        mnavInputResult input =
            mnavCheckMeshTriangle(&baker->def, &solid->meshes[list[k].mesh], list[k].triangle);
        if (input.result != mnav_success)
        {
            report->mesh = list[k].mesh;
            report->input = input;
            return input.result;
        }
    }
    return mnav_success;
}

static mnavResult CheckInput(const mnavBaker* baker, const Input* in, int32_t tileX, int32_t tileZ,
                             mnavBakeReport* report)
{
    if (in->flat)
    {
        return CheckOutlines(baker, in, tileX, tileZ, report);
    }
    if (!mnavCheckInputArrays(&in->solid))
    {
        return mnav_errorInvalid;
    }
    if (in->solid.index != nullptr)
    {
        mnavResult result = CheckIndexed(baker, &in->solid, tileX, tileZ, report);
        if (result != mnav_success)
        {
            return result;
        }
    }
    return mnavCheckSolidInput(&baker->def, &in->solid, in->solid.index != nullptr, &report->mesh,
                               &report->input);
}

// The hash of the generator, the settings that shape the tile and its
// place.
static uint64_t HashSettings(const mnavBakeDef* def, int32_t tileX, int32_t tileZ)
{
    mnavVersion version = mnavGetVersion();
    uint64_t origin[3] = {mnavDoubleBits(def->origin.x), mnavDoubleBits(def->origin.y),
                          mnavDoubleBits(def->origin.z)};
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
        mnavFloatBits(def->cellSize),
        mnavFloatBits(def->cellHeight),
        (uint32_t)def->tileCells,
        mnavFloatBits(def->agent.radius),
        mnavFloatBits(def->agent.height),
        mnavFloatBits(def->agent.stepHeight),
        mnavFloatBits(def->agent.maxSlopeDegrees),
        mnavFloatBits(def->minRegionArea),
        mnavFloatBits(def->maxEdgeError),
        mnavFloatBits(def->maxEdgeLength),
        mnavFloatBits(def->detailSampleDistance),
        mnavFloatBits(def->detailMaxError),
        (uint32_t)tileX,
        (uint32_t)tileZ,
    };
    return mnavHashWords(MNAV_HASH_INIT, words, (int32_t)(sizeof(words) / sizeof(words[0])));
}

// Adds every outline that reaches the tile to the fingerprint, after a
// word that keeps 2D input apart from 3D, and counts them.
static uint64_t HashOutlines(const mnavTileFrame* frame, const Input* in, int32_t tileX,
                             int32_t tileZ, uint64_t hash, int32_t* count)
{
    const uint32_t tag = 0x32444E41u;
    hash = mnavHashWords(hash, &tag, 1);
    *count = 0;
    const mnavOutlineSet set =
        mnavOutlinesFor(in->outlines, in->outlineCount, in->outlineIndex, tileX, tileZ);
    for (int32_t k = 0; k < set.count; ++k)
    {
        const mnavOutline* outline = mnavOutlineAt(&set, k);
        if (!mnavOutlineTouchesTile(frame, outline))
        {
            continue;
        }
        *count += 1;
        uint32_t head[2] = {(uint32_t)outline->pointCount, (uint32_t)outline->area};
        hash = mnavHashWords(hash, head, 2);
        for (int32_t i = 0; i < outline->pointCount; ++i)
        {
            uint32_t words[2] = {mnavFloatBits(outline->points[i].x),
                                 mnavFloatBits(outline->points[i].y)};
            hash = mnavHashWords(hash, words, 2);
        }
    }
    return hash;
}

// The detail lookup's search radius: the wall error rounded up, at least
// one cell.
static int32_t SearchRadius(float edgeError)
{
    int32_t radius = (int32_t)ceilf(edgeError);
    return radius < 1 ? 1 : radius;
}

// The clock's ticks now, or 0 without a clock.
static uint64_t Now(const mnavBaker* baker)
{
    const mnavClock* clock = &baker->def.clock;
    return clock->now != nullptr ? clock->now(clock->context) : 0;
}

// Ends the stage being run: its ticks and the most memory held while it
// ran go into the report; then the bake enters stage next.
static void Enter(mnavBaker* baker, mnavBakeReport* report, mnavBakeStage next)
{
    uint64_t now = Now(baker);
    uint64_t peak = baker->memory.peak;
    mnavBakeStage stage = report->stage;
    if (stage < MNAV_BAKE_STAGES)
    {
        report->stageTicks[stage] += now - baker->stageStart;
        report->stageMemory[stage] =
            peak > report->stageMemory[stage] ? peak : report->stageMemory[stage];
    }
    report->memoryPeak = peak > report->memoryPeak ? peak : report->memoryPeak;
    baker->memory.peak = baker->memory.used;
    baker->stageStart = now;
    report->stage = next;
}

// Runs the stages up to the open-space field, entering each in the report
// before it runs.
static mnavResult RunToCompact(mnavBaker* baker, const Input* in, int32_t tileX, int32_t tileZ,
                               Stages* s, mnavBakeReport* report)
{
    mnavMemory* memory = &baker->memory;
    const mnavBakeDef* def = &baker->def;
    const mnavBakeCells* cells = &baker->cells;
    Enter(baker, report, mnav_stageRasterize);
    bool flat = in->flat;
    mnavResult result =
        flat ? mnavBuildHeightfield2D(memory, def, cells, in->outlines, in->outlineCount,
                                      in->outlineIndex, tileX, tileZ, &s->heightfield)
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
    Enter(baker, report, mnav_stageCompact);
    return mnavBuildCompactField(memory, &s->heightfield, cells->agentHeight, cells->agentStep,
                                 &s->compact);
}

// Runs the stages from the volumes on, over the open-space field, entering
// each in the report before it runs.
static mnavResult RunFromCompact(mnavBaker* baker, const mnavBakeVolume* volumes, int32_t count,
                                 Stages* s, mnavBakeReport* report)
{
    mnavMemory* memory = &baker->memory;
    const mnavBakeDef* def = &baker->def;
    const mnavBakeCells* cells = &baker->cells;
    mnavResult result = mnavCarveVolumes(memory, &s->compact, volumes, count);
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageErode);
        result = mnavErode(memory, &s->compact, cells->agentRadius);
    }
    if (result == mnav_success)
    {
        result = mnavMarkVolumes(memory, &s->compact, volumes, count);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageRegions);
        result =
            mnavBuildRegions(memory, &s->compact, cells->border, cells->minRegion, &s->regions);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageContours);
        result = mnavBuildContours(memory, &s->compact, &s->regions, cells->border,
                                   cells->edgeError, cells->edgeLength, &s->set);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageHoles);
        result = mnavMergeHoles(memory, &s->set, s->regions.count);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stagePolygons);
        result = mnavBuildPolyMesh(memory, &s->set, def->tileCells, def->limits.tileVertices,
                                   def->limits.tilePolygons, &s->mesh);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageBorderVertices);
        result = mnavRemoveBorderVertices(memory, &s->mesh, def->limits.tilePolygons);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageLinks);
        result = mnavLinkPolyMesh(memory, &s->mesh);
    }
    if (result == mnav_success)
    {
        Enter(baker, report, mnav_stageDetail);
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

// Starts a bake or a rebuild: a fresh report, the last tile dropped and
// the input stage begun. False, with the report given out, for a NULL
// baker.
static bool Start(mnavBaker* baker, mnavBakeReport* report, mnavBakeReport* reportOut)
{
    *report = (mnavBakeReport){0};
    report->mesh = -1;
    report->input = (mnavInputResult){mnav_success, mnav_elementNone, -1};
    if (baker == nullptr)
    {
        report->result = mnav_errorInvalid;
        if (reportOut != nullptr)
        {
            *reportOut = *report;
        }
        return false;
    }
    DropTile(baker);
    baker->memory.peak = baker->memory.used;
    baker->stageStart = Now(baker);
    report->stage = mnav_stageInput;
    return true;
}

// Ends a bake or a rebuild: on success the counts and the encoded tile;
// then the stages released and the report given out.
static mnavResult Finish(mnavBaker* baker, int32_t tileX, int32_t tileZ, Stages* s,
                         mnavResult result, mnavBakeReport* report, mnavBakeReport* reportOut)
{
    if (result == mnav_success)
    {
        Count(s, report);
        Enter(baker, report, mnav_stageEncode);
        result = Encode(baker, tileX, tileZ, s, report->fingerprint);
    }
    // The last stage run ends here, where the bake ended or failed.
    Enter(baker, report, result == mnav_success ? mnav_stageDone : report->stage);
    ReleaseStages(&baker->memory, s);
    if (result == mnav_success)
    {
        report->tileBytes = baker->tileSize;
    }
    report->result = result;
    if (reportOut != nullptr)
    {
        *reportOut = *report;
    }
    return result;
}

// Bakes a tile; with a cache, keeps its field before the volumes apply.
static mnavResult Bake(mnavBaker* baker, const Input* in, int32_t tileX, int32_t tileZ,
                       mnavTileCache* cache, mnavBakeReport* reportOut)
{
    mnavBakeReport report;
    if (!Start(baker, &report, reportOut))
    {
        return mnav_errorInvalid;
    }
    mnavResult result = CheckInput(baker, in, tileX, tileZ, &report);
    mnavTileFrame frame = {0};
    if (result == mnav_success &&
        !mnavMakeTileFrame(&baker->def, &baker->cells, tileX, tileZ, &frame))
    {
        result = mnav_errorRange;
    }
    Stages stages = {0};
    uint64_t settings = HashSettings(&baker->def, tileX, tileZ);
    uint64_t geometry = 0;
    if (result == mnav_success)
    {
        if (in->flat)
        {
            report.fingerprint =
                HashOutlines(&frame, in, tileX, tileZ, settings, &report.triangles);
        }
        else
        {
            geometry = mnavFingerprintGeometry(&frame, baker->cells.cosMaxSlope, &in->solid, tileX,
                                               tileZ, settings, &report.triangles);
            report.fingerprint =
                mnavFingerprintVolumes(&frame, in->solid.volumes, in->solid.volumeCount, geometry);
        }
        result = RunToCompact(baker, in, tileX, tileZ, &stages, &report);
    }
    if (result == mnav_success && cache != nullptr)
    {
        const mnavCachedTile tile = {.tileX = tileX,
                                     .tileZ = tileZ,
                                     .settings = settings,
                                     .geometry = geometry,
                                     .triangles = report.triangles};
        result = mnavCacheTile(cache, &baker->memory, &tile, &stages.compact, in->solid.volumes,
                               in->solid.volumeCount);
    }
    if (result == mnav_success)
    {
        result = RunFromCompact(baker, in->solid.volumes, in->solid.volumeCount, &stages, &report);
    }
    // A cache keeps tiles that baked, never what an earlier input left.
    result = Finish(baker, tileX, tileZ, &stages, result, &report, reportOut);
    if (result != mnav_success)
    {
        mnavDropCachedTile(cache, tileX, tileZ);
    }
    return result;
}

mnavResult mnavBakeTile(mnavBaker* baker, const mnavTriangleMesh* meshes, int32_t meshCount,
                        int32_t tileX, int32_t tileZ, mnavBakeReport* reportOut)
{
    Input in = {{meshes, meshCount, nullptr, 0, nullptr, 0, nullptr}, nullptr, 0, nullptr, false};
    return Bake(baker, &in, tileX, tileZ, nullptr, reportOut);
}

mnavResult mnavBakeTileInput(mnavBaker* baker, const mnavBakeInput* input, int32_t tileX,
                             int32_t tileZ, mnavBakeReport* reportOut)
{
    if (input == nullptr)
    {
        Input none = {{nullptr, -1, nullptr, 0, nullptr, 0, nullptr}, nullptr, 0, nullptr, false};
        return Bake(baker, &none, tileX, tileZ, nullptr, reportOut);
    }
    Input in = {*input, nullptr, 0, nullptr, false};
    return Bake(baker, &in, tileX, tileZ, nullptr, reportOut);
}

mnavResult mnavBakeTile2D(mnavBaker* baker, const mnavOutline* outlines, int32_t outlineCount,
                          int32_t tileX, int32_t tileZ, mnavBakeReport* reportOut)
{
    Input in = {
        {nullptr, 0, nullptr, 0, nullptr, 0, nullptr}, outlines, outlineCount, nullptr, true};
    return Bake(baker, &in, tileX, tileZ, nullptr, reportOut);
}

mnavResult mnavBakeTile2DInput(mnavBaker* baker, const mnavBake2DInput* input, int32_t tileX,
                               int32_t tileZ, mnavBakeReport* reportOut)
{
    const mnavBake2DInput none = {nullptr, -1, nullptr};
    const mnavBake2DInput* in2 = input != nullptr ? input : &none;
    Input in = {{nullptr, 0, nullptr, 0, nullptr, 0, nullptr},
                in2->outlines,
                in2->outlineCount,
                in2->index,
                true};
    return Bake(baker, &in, tileX, tileZ, nullptr, reportOut);
}

mnavResult mnavBakeTileCached(mnavBaker* baker, mnavTileCache* cache, const mnavBakeInput* input,
                              int32_t tileX, int32_t tileZ, mnavBakeReport* reportOut)
{
    if (cache == nullptr || input == nullptr)
    {
        Input none = {{nullptr, -1, nullptr, 0, nullptr, 0, nullptr}, nullptr, 0, nullptr, false};
        return Bake(baker, &none, tileX, tileZ, nullptr, reportOut);
    }
    Input in = {*input, nullptr, 0, nullptr, false};
    return Bake(baker, &in, tileX, tileZ, cache, reportOut);
}

// Checks the obstacles as a bake checks volumes, naming a refused one by
// its index; their points and the cached volumes' count against the input
// limit.
static mnavResult CheckObstacles(const mnavBaker* baker, const mnavCachedTile* tile,
                                 const mnavBakeVolume* obstacles, int32_t obstacleCount,
                                 mnavBakeReport* report)
{
    if (obstacleCount < 0 || (obstacleCount > 0 && obstacles == nullptr))
    {
        return mnav_errorInvalid;
    }
    int64_t total = (int64_t)tile->triangles + tile->pointCount;
    for (int32_t i = 0; i < obstacleCount; ++i)
    {
        mnavInputResult input = mnavCheckVolume(&baker->def, &obstacles[i]);
        if (input.result != mnav_success)
        {
            report->mesh = i;
            report->input = input;
            return input.result;
        }
        total += obstacles[i].pointCount;
    }
    return total > baker->def.limits.inputTriangles ? mnav_errorLimit : mnav_success;
}

mnavResult mnavRebuildTile(mnavBaker* baker, mnavTileCache* cache, int32_t tileX, int32_t tileZ,
                           const mnavBakeVolume* obstacles, int32_t obstacleCount,
                           mnavBakeReport* reportOut)
{
    mnavBakeReport report;
    if (!Start(baker, &report, reportOut))
    {
        return mnav_errorInvalid;
    }
    Stages stages = {0};
    const mnavCachedTile* tile =
        cache != nullptr ? mnavFindCachedTile(cache, tileX, tileZ) : nullptr;
    mnavResult result = cache == nullptr  ? mnav_errorInvalid
                        : tile == nullptr ? mnav_errorNotLoaded
                        : tile->settings != HashSettings(&baker->def, tileX, tileZ)
                            ? mnav_errorInvalid
                            : CheckObstacles(baker, tile, obstacles, obstacleCount, &report);
    // The cached volumes, then the obstacles, in the baker's memory.
    int32_t count = 0;
    mnavBakeVolume* volumes = nullptr;
    if (result == mnav_success)
    {
        result = ckd_add(&count, tile->volumeCount, obstacleCount)
                     ? mnav_errorLimit
                     : mnavAllocate(&baker->memory, (size_t)count, sizeof(mnavBakeVolume),
                                    alignof(mnavBakeVolume), (void**)&volumes);
    }
    if (result == mnav_success)
    {
        if (tile->volumeCount > 0)
        {
            memcpy(volumes, tile->volumes, (size_t)tile->volumeCount * sizeof(mnavBakeVolume));
        }
        if (obstacleCount > 0)
        {
            memcpy(&volumes[tile->volumeCount], obstacles,
                   (size_t)obstacleCount * sizeof(mnavBakeVolume));
        }
        report.triangles = tile->triangles;
        report.fingerprint =
            mnavFingerprintVolumes(&tile->field.frame, volumes, count, tile->geometry);
        Enter(baker, &report, mnav_stageCompact);
        result = mnavUnpackField(&baker->memory, &tile->field, baker->cells.agentHeight,
                                 baker->cells.agentStep, &stages.compact);
    }
    if (result == mnav_success)
    {
        result = RunFromCompact(baker, volumes, count, &stages, &report);
    }
    mnavRelease(&baker->memory, volumes, (size_t)count, sizeof(mnavBakeVolume),
                alignof(mnavBakeVolume));
    return Finish(baker, tileX, tileZ, &stages, result, &report, reportOut);
}

mnavResult mnavCopyBakedTile(const mnavBaker* baker, uint8_t* bufferOut, size_t capacity,
                             size_t* sizeOut)
{
    if (baker == nullptr || (bufferOut == nullptr && capacity > 0) || baker->tile == nullptr)
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
    if (bufferOut != nullptr)
    {
        memcpy(bufferOut, baker->tile, baker->tileSize);
    }
    return mnav_success;
}
