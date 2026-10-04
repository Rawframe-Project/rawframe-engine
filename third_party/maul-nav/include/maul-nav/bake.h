// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake's settings and its input surface: the def a bake runs with,
// the agent profile it bakes for, the named limits that bound its work,
// and the triangle meshes a host hands it, each checked before any work
// (mnav-0002).

#ifndef MAUL_NAV_BAKE_H
#define MAUL_NAV_BAKE_H

#include "maul-nav/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The number of area types: 0 is unwalkable, 1 to 63 are walkable kinds a
// query filter costs and includes or excludes.
#define MNAV_AREA_TYPES 64

// The range of a cell's size and height, in meters.
#define MNAV_MIN_CELL_SIZE 0.001f
#define MNAV_MAX_CELL_SIZE 10.0f

// The range of a tile's side, in cells.
#define MNAV_MIN_TILE_CELLS 16
#define MNAV_MAX_TILE_CELLS 1024

// How far input may lie from the bake's origin: horizontally, in cells of
// the bake's cell size; vertically, in cells of its cell height, up or down.
#define MNAV_MAX_EXTENT_CELLS 4194304
#define MNAV_MAX_HEIGHT_CELLS 32767

// The largest values the named limits may take.
#define MNAV_MAX_INPUT_TRIANGLES 268435456
#define MNAV_MAX_TILE_SPANS      67108864
#define MNAV_MAX_TILE_POLYGONS   65535
#define MNAV_MAX_TILE_VERTICES   65535
#define MNAV_MAX_TILE_LINKS      1048576
#define MNAV_MAX_TILES           1048576
#define MNAV_MAX_LINKS           1048576

    // An area type, 0 to MNAV_AREA_TYPES - 1.
    typedef uint8_t mnavAreaType;

    enum
    {
        // Not walkable: triangles of this area contribute nothing walkable
        // and block what lies under them.
        mnav_areaNone = 0,
        // The walkable area a triangle has when its mesh gives no areas.
        mnav_areaWalkable = 1,
    };

    // The agent a navmesh is baked for, in meters and degrees (one
    // profile per navmesh).
    typedef struct mnavAgentProfile
    {
        // The agent's radius: walkable area keeps this far from walls. At
        // least 0.
        float radius;
        // The agent's height: walkable area has this much free space above
        // it. More than 0.
        float height;
        // The highest ledge the agent steps up or down. At least 0.
        float stepHeight;
        // The steepest walkable slope, in degrees, from 0 up to but not
        // including 90.
        float maxSlopeDegrees;
    } mnavAgentProfile;

    // The named limits that bound a bake's work. Each is at least 1 and at
    // most its MNAV_MAX_ value. Reaching one is mnav_errorLimit, never a
    // silent truncation. None is in seconds, so a bake's outcome does not
    // depend on the machine.
    typedef struct mnavBakeLimits
    {
        // Triangles in one bake's input.
        int32_t inputTriangles;
        // Input triangles that touch one tile.
        int32_t tileTriangles;
        // Span fragments rasterized into one tile: one per cell a triangle
        // covers, before they merge into spans.
        int32_t tileSpans;
        // Polygons in one tile.
        int32_t tilePolygons;
        // Vertices in one tile.
        int32_t tileVertices;
        // Links in one tile.
        int32_t tileLinks;
        // Tiles in one navmesh.
        int32_t tiles;
        // Off-mesh links in one navmesh, staged or committed (mnav-0004).
        int32_t links;
        // Bytes the bake may hold allocated at once.
        uint64_t memoryBytes;
    } mnavBakeLimits;

    // How a bake runs. Build it with mnavDefaultBakeDef.
    // A navmesh's runtime tier (mnav-0004): what may change once loaded.
    // Tiles and off-mesh links load and unload in every tier.
    typedef uint8_t mnavTier;

    enum
    {
        // Nothing loaded changes.
        mnav_tierStatic = 0,
        // Polygons' areas change, and off-mesh links are enabled and
        // disabled.
        mnav_tierModifiers = 1,
        // Also, a loaded tile is replaced in one commit.
        mnav_tierDynamic = 2,
    };

    typedef struct mnavBakeDef
    {
        uint32_t cookie;
        // The allocator the bake uses; zeroed for the C library's.
        mnavAllocator allocator;
        // The world position the tile grid starts at. Input vertices are
        // relative to it.
        mnavPos3 origin;
        // A cell's side on the ground, in meters, MNAV_MIN_CELL_SIZE to
        // MNAV_MAX_CELL_SIZE.
        float cellSize;
        // A cell's height, in meters, in the same range.
        float cellHeight;
        // A tile's side, in cells, MNAV_MIN_TILE_CELLS to
        // MNAV_MAX_TILE_CELLS.
        int32_t tileCells;
        // The agent the navmesh is for.
        mnavAgentProfile agent;
        // The smallest walkable region kept, in square meters, at least 0:
        // smaller islands away from a tile's edge are dropped.
        float minRegionArea;
        // How far a simplified wall may stray from the cells it follows, in
        // meters, at least 0.
        float maxEdgeError;
        // The longest wall edge, in meters, at least 0; 0 for no limit.
        float maxEdgeLength;
        // How far apart the detail mesh samples the floor's height along
        // polygon edges and inside polygons, in meters, at least 0; 0 for
        // none, and at least one cell otherwise.
        float detailSampleDistance;
        // How far the detail surface may stray from a sampled height, in
        // meters, at least 0.
        float detailMaxError;
        // The limits on the work.
        mnavBakeLimits limits;
        // The runtime tier of a navmesh made with this def,
        // mnav_tierStatic by default; the bake ignores it.
        mnavTier tier;
    } mnavBakeDef;

    // The setting a def check refused.
    typedef uint8_t mnavBakeSetting;

    enum
    {
        mnav_settingNone = 0,
        mnav_settingCookie = 1,
        mnav_settingAllocator = 2,
        mnav_settingOrigin = 3,
        mnav_settingCellSize = 4,
        mnav_settingCellHeight = 5,
        mnav_settingTileCells = 6,
        mnav_settingAgentRadius = 7,
        mnav_settingAgentHeight = 8,
        mnav_settingAgentStepHeight = 9,
        mnav_settingAgentMaxSlope = 10,
        mnav_settingInputTriangles = 11,
        mnav_settingTileTriangles = 12,
        mnav_settingTileSpans = 13,
        mnav_settingTilePolygons = 14,
        mnav_settingTileVertices = 15,
        mnav_settingTileLinks = 16,
        mnav_settingTiles = 17,
        mnav_settingMemoryBytes = 18,
        mnav_settingMinRegionArea = 19,
        mnav_settingMaxEdgeError = 20,
        mnav_settingMaxEdgeLength = 21,
        mnav_settingDetailSampleDistance = 22,
        mnav_settingDetailMaxError = 23,
        mnav_settingLinks = 24,
        mnav_settingTier = 25,
    };

    // A def check's outcome: the status, and the first setting it refused.
    typedef struct mnavBakeDefResult
    {
        mnavResult result;
        mnavBakeSetting setting;
    } mnavBakeDefResult;

    // What a def's meters become in cells (mnav-0002). A quotient within 2^-10 of
    // an integer counts as that integer; heights and the radius then round
    // up and the step height rounds down.
    typedef struct mnavBakeCells
    {
        // The agent's height, in cell heights.
        int32_t agentHeight;
        // The agent's step height, in cell heights.
        int32_t agentStep;
        // The agent's radius, in cells.
        int32_t agentRadius;
        // The border rasterized around each tile beyond its side, in
        // cells: the radius and 3 more.
        int32_t border;
        // The cosine of the maximum slope: a triangle is walkable when its
        // normal's Y is at least this times the normal's length.
        float cosMaxSlope;
        // A tile's side, in meters.
        float tileSize;
        // The minimum region area, in cells.
        int32_t minRegion;
        // The maximum edge error, in cells, unrounded.
        float edgeError;
        // The maximum wall edge length, in cells, rounded down; 0 for no
        // limit.
        int32_t edgeLength;
        // The detail sample distance, in sixteenths of a cell, rounded
        // down; 0 for none, at least 16 otherwise.
        int32_t detailSample;
        // The detail maximum error, in sixteenths of a cell height,
        // rounded down.
        int32_t detailError;
    } mnavBakeCells;

    // A triangle mesh in a bake's input.
    typedef struct mnavTriangleMesh
    {
        // The vertices, in meters relative to the bake's origin. Only read
        // during the call.
        const mnavVec3* vertices;
        // The number of vertices, at least 0.
        int32_t vertexCount;
        // Three vertex indices per triangle.
        const int32_t* indices;
        // The number of triangles, at least 0.
        int32_t triangleCount;
        // One area type per triangle, or NULL for mnav_areaWalkable on
        // every triangle.
        const mnavAreaType* areas;
    } mnavTriangleMesh;

// The most samples a terrain may have along either side.
#define MNAV_MAX_TERRAIN_SIDE 65536

    // A terrain (mnav-0003): heights sampled on a regular grid, the compact
    // form of the largest input. Each cell between four samples is two
    // triangles split along the diagonal from its lowest X and Z corner.
    typedef struct mnavTerrain
    {
        // The first sample's place, in meters relative to the bake's origin.
        mnavVec3 origin;
        // The distance between samples along X and along Z, in meters,
        // more than 0.
        float spacingX;
        float spacingZ;
        // The samples along X and along Z, 2 to MNAV_MAX_TERRAIN_SIDE.
        int32_t columns;
        int32_t rows;
        // columns times rows heights added to the origin's Y, X fastest.
        // Only read during the call.
        const float* heights;
        // One area type per cell, (columns - 1) times (rows - 1), X
        // fastest, or NULL for mnav_areaWalkable everywhere; a cell of
        // mnav_areaNone is a hole, with no ground at all.
        const mnavAreaType* areas;
    } mnavTerrain;

    // A point of a 2D outline, in meters relative to the bake's origin:
    // (x, y) is the navmesh point (x, 0, y) (mnav-0002).
    typedef struct mnavVec2
    {
        float x;
        float y;
    } mnavVec2;

    // A closed ring of points in a 2D bake's input. The cells whose
    // centers it holds, by the even-odd rule, take its area; one of area
    // mnav_areaNone cuts them out of every other outline.
    typedef struct mnavOutline
    {
        // The points, the last joined back to the first. Only read during
        // the call.
        const mnavVec2* points;
        // The number of points, at least 3.
        int32_t pointCount;
        mnavAreaType area;
    } mnavOutline;

    // What a bake volume does (mnav-0003).
    typedef uint8_t mnavVolumeKind;

    enum
    {
        // When a bake has include volumes, the ground inside none of them
        // is dropped, before the agent's radius is kept from walls.
        mnav_volumeInclude = 0,
        // The ground inside is dropped before the agent's radius is kept
        // from walls, so agents keep clear of it as of a wall.
        mnav_volumeExclude = 1,
        // The walkable ground inside takes the volume's area after the
        // radius is kept, the last such volume holding it winning.
        mnav_volumeArea = 2,
    };

    // A prism standing on a ring of points: the cells whose centers the
    // ring holds, by the even-odd rule, with their ground from minY to
    // maxY.
    typedef struct mnavBakeVolume
    {
        // The ring, (x, y) being the place (x, 0, y) relative to the bake's
        // origin, the last point joined back to the first. Only read during
        // the call.
        const mnavVec2* points;
        // The number of points, at least 3.
        int32_t pointCount;
        // The heights the ground must lie within, minY at most maxY.
        float minY;
        float maxY;
        mnavVolumeKind kind;
        // For mnav_volumeArea, a walkable area type; otherwise unused.
        mnavAreaType area;
    } mnavBakeVolume;

    // Everything a 3D bake reads: triangle meshes, terrains and volumes.
    typedef struct mnavBakeInput
    {
        const mnavTriangleMesh* meshes;
        int32_t meshCount;
        const mnavTerrain* terrains;
        int32_t terrainCount;
        const mnavBakeVolume* volumes;
        int32_t volumeCount;
    } mnavBakeInput;

    // The kind of input element a check refused.
    typedef uint8_t mnavInputElement;

    enum
    {
        // The input as a whole, or no element.
        mnav_elementNone = 0,
        mnav_elementVertex = 1,
        mnav_elementTriangle = 2,
        mnav_elementPoint = 3,
        // A terrain's height sample, and its cell.
        mnav_elementSample = 4,
        mnav_elementCell = 5,
    };

    // An input check's outcome: the status, and the first element it
    // refused, vertices before triangles, each in ascending index order.
    typedef struct mnavInputResult
    {
        mnavResult result;
        mnavInputElement element;
        // The element's index, or -1 for mnav_elementNone.
        int32_t index;
    } mnavInputResult;

    /// Checks a 2D outline as hostile input to a bake with a def.
    ///
    /// @param def     The bake def the outline is for.
    /// @param outline The outline.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, an
    /// invalid def, fewer than 3 points, an area of MNAV_AREA_TYPES or more,
    /// or a point that is not finite (the point); `mnav_errorRange` for a
    /// point past the extent input may have (the point); `mnav_errorLimit`
    /// for more points than the inputTriangles limit.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavInputResult mnavValidateOutline(const mnavBakeDef* def,
                                                                const mnavOutline* outline);

    /// Returns the default bake def: cells of 0.25 m by 0.125 m, tiles of
    /// 128 cells, an agent 0.5 m in radius and 2 m tall that steps 0.75 m
    /// and walks slopes up to 45 degrees, regions of at least 2 square
    /// meters, walls within 0.3 m of the cells and at most 12 m long,
    /// detail samples every 1.5 m within 0.125 m, and limits sized for a
    /// large level.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_API mnavBakeDef mnavDefaultBakeDef(void);

    /// Checks a bake def and converts its meters to cells.
    ///
    /// @param def       The def to check.
    /// @param cellsOut  Receives the settings in cells when the def is
    ///                  valid. May be NULL.
    /// @return `mnav_success`; `mnav_errorInvalid` with the setting for a
    /// NULL def, a def without its cookie, an allocator with one function,
    /// a value that is not finite or lies outside its range, an agent
    /// radius whose border is wider than a tile, or an agent height or
    /// step height past MNAV_MAX_HEIGHT_CELLS.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavBakeDefResult mnavValidateBakeDef(const mnavBakeDef* def,
                                                                  mnavBakeCells* cellsOut);

    /// Checks a triangle mesh as hostile input to a bake with a def.
    ///
    /// @param def   The bake def the mesh is for.
    /// @param mesh  The mesh.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, an
    /// invalid def, a negative count, a NULL array its count needs, a
    /// vertex coordinate that is not finite, a vertex index outside the
    /// vertices or an area type of MNAV_AREA_TYPES or more;
    /// `mnav_errorLimit` for more triangles than the def's inputTriangles
    /// limit; `mnav_errorRange` for a vertex more than MNAV_MAX_EXTENT_CELLS
    /// cells from the origin on the ground or MNAV_MAX_HEIGHT_CELLS cell
    /// heights above or below it. The element and index name the first
    /// offending vertex or triangle.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavInputResult mnavValidateTriangleMesh(const mnavBakeDef* def,
                                                                     const mnavTriangleMesh* mesh);

// The tile format version this library writes and reads (mnav-0003).
#define MNAV_TILE_FORMAT 1

    // A baker: the def it was made with, its memory, and the last tile it
    // baked. Made by mnavCreateBaker.
    typedef struct mnavBaker mnavBaker;

    // The stage a bake ended in.
    typedef uint8_t mnavBakeStage;

    enum
    {
        mnav_stageInput = 0,
        mnav_stageRasterize = 1,
        mnav_stageCompact = 2,
        mnav_stageErode = 3,
        mnav_stageRegions = 4,
        mnav_stageContours = 5,
        mnav_stageHoles = 6,
        mnav_stagePolygons = 7,
        mnav_stageBorderVertices = 8,
        mnav_stageLinks = 9,
        mnav_stageDetail = 10,
        mnav_stageEncode = 11,
        // Every stage ran.
        mnav_stageDone = 12,
    };

    // What a bake did: its result and the stage it ended in, the first
    // input refused, what the tile holds, and every piece of work it gave
    // up on rather than fail.
    typedef struct mnavBakeReport
    {
        mnavResult result;
        mnavBakeStage stage;
        // The first mesh refused, or -1, and what its check found.
        int32_t mesh;
        mnavInputResult input;
        // The hash of everything that shaped the tile: the generator's
        // version, the def's settings, the tile's place and the input
        // triangles that reach it, in input order. Written into the tile.
        uint64_t fingerprint;
        // Input triangles that reach the tile, walkable open spans,
        // regions, polygons, vertices, detail vertices and triangles.
        int32_t triangles;
        int32_t spans;
        int32_t regions;
        int32_t polygons;
        int32_t vertices;
        int32_t detailVertices;
        int32_t detailTriangles;
        // The tile's bytes, and the most memory the bake held at once.
        uint64_t tileBytes;
        uint64_t memoryPeak;
        // Holes that no bridge could join to their outline, polygon rings
        // and detail outlines whose triangulation stopped short, detail
        // samples that found no height and took the polygon's, and
        // polygons whose detail reached its vertex cap.
        int32_t droppedHoles;
        int32_t partialRings;
        int32_t fallbackHeights;
        int32_t cappedDetail;
        // Islands of walkable space smaller than the minimum region area
        // and away from the tile's border, dropped.
        int32_t droppedRegions;
    } mnavBakeReport;

    /// Makes a baker from a def: checks the def and keeps a copy, its
    /// allocator and its memory limit.
    ///
    /// @param def       The def.
    /// @param bakerOut  Receives the baker, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` with the setting for an
    /// invalid def or a NULL argument; `mnav_errorLimit` when the baker
    /// does not fit the def's memory limit; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavBakeDefResult mnavCreateBaker(const mnavBakeDef* def,
                                                              mnavBaker** bakerOut);

    /// Destroys a baker and the tile it holds.
    ///
    /// @param baker  The baker, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_API void mnavDestroyBaker(mnavBaker* baker);

    /// Bakes one tile from triangle meshes and terrains, shaped by
    /// volumes, as mnavBakeTile does from meshes alone; a terrain's
    /// triangles count as input triangles, after the meshes', toward the
    /// limits and the fingerprint, and a volume's points count as input
    /// triangles too. A refused terrain is reported as the mesh at its
    /// index after the meshes, a refused volume as the one at its index
    /// after the terrains.
    ///
    /// @param baker     The baker; the tile it held before is dropped.
    /// @param input     The meshes and terrains.
    /// @param tileX     The tile's column.
    /// @param tileZ     The tile's row.
    /// @param reportOut Receives the report. May be NULL.
    /// @return As mnavBakeTile; a terrain with a side out of range, a
    /// spacing not more than 0 or not finite, a height not finite (the
    /// sample) or an area of MNAV_AREA_TYPES or more (the cell) is
    /// `mnav_errorInvalid`, a sample past the extent input may have
    /// `mnav_errorRange`; a volume with fewer than 3 points, a kind out of
    /// range, an area volume's area not walkable, heights not finite or
    /// backward, or a point not finite (the point) is `mnav_errorInvalid`,
    /// a point past the extent (the point) `mnav_errorRange`.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBakeTileInput(mnavBaker* baker,
                                                         const mnavBakeInput* input, int32_t tileX,
                                                         int32_t tileZ, mnavBakeReport* reportOut);

    /// Bakes tile (tileX, tileZ) of the meshes: checks every mesh, then
    /// rasterizes, filters, partitions, traces and triangulates the tile
    /// and lays its detail, and keeps the tile's bytes for
    /// mnavCopyBakedTile, in place of any tile baked before. The same
    /// meshes and def give the same bytes on every platform.
    ///
    /// @param baker      The baker.
    /// @param meshes     The input meshes, in the def's frame.
    /// @param meshCount  The number of meshes, at least 0.
    /// @param tileX      The tile's column in the grid from the origin.
    /// @param tileZ      The tile's row.
    /// @param reportOut  Receives what the bake did. May be NULL.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL baker, a
    /// negative count, NULL meshes with a positive count, or a mesh its
    /// check refuses (the report names it); `mnav_errorRange` for a mesh
    /// past the extent or a tile past it; `mnav_errorLimit` when a named
    /// limit or the memory limit is reached (the report's stage says
    /// where); `mnav_errorCapacity` when the allocator fails. On failure
    /// the baker holds no tile.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    /// Bakers share nothing, so one per thread bakes tiles in parallel.
    MNAV_NODISCARD MNAV_API mnavResult mnavBakeTile(mnavBaker* baker,
                                                    const mnavTriangleMesh* meshes,
                                                    int32_t meshCount, int32_t tileX, int32_t tileZ,
                                                    mnavBakeReport* reportOut);

    /// Bakes tile (tileX, tileZ) of 2D outlines (mnav-0002): checks every
    /// outline, fills the cells whose centers a walkable outline holds and
    /// no obstruction does as a flat floor at height 0, with the highest
    /// area among the outlines holding them, then erodes, partitions,
    /// traces and triangulates as mnavBakeTile does; the walkable filters,
    /// which judge heights, do not run. The report's mesh names a refused
    /// outline and its triangles count the outlines reaching the tile.
    ///
    /// @param baker        The baker.
    /// @param outlines     The input outlines, in the def's frame.
    /// @param outlineCount The number of outlines, at least 0.
    /// @param tileX        The tile's column in the grid from the origin.
    /// @param tileZ        The tile's row.
    /// @param reportOut    Receives what the bake did. May be NULL.
    /// @return As mnavBakeTile, for outlines; also `mnav_errorLimit` for
    /// more points in all than the inputTriangles limit, or more outlines
    /// reaching the tile than the tileTriangles limit.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    /// Bakers share nothing, so one per thread bakes tiles in parallel.
    MNAV_NODISCARD MNAV_API mnavResult mnavBakeTile2D(mnavBaker* baker, const mnavOutline* outlines,
                                                      int32_t outlineCount, int32_t tileX,
                                                      int32_t tileZ, mnavBakeReport* reportOut);

    /// Copies the last baked tile's bytes into caller memory.
    ///
    /// @param baker     The baker.
    /// @param buffer    The memory, at least capacity bytes; may be NULL
    ///                  when capacity is 0.
    /// @param capacity  The buffer's size in bytes.
    /// @param sizeOut   Receives the tile's size in bytes. May be NULL.
    /// @return `mnav_success`; `mnav_errorCapacity` when the buffer is
    /// smaller than the tile, with the size still written;
    /// `mnav_errorInvalid` for a NULL baker, a NULL buffer with a
    /// positive capacity, or a baker holding no tile.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavCopyBakedTile(const mnavBaker* baker, uint8_t* buffer,
                                                         size_t capacity, size_t* sizeOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_BAKE_H
