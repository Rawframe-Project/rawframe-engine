// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flight volumes (mnav-0015): where agents that fly or swim may go, baked
// per tile from the navmesh bake's input into a compact sparse voxel
// octree, loaded from bytes, staged and committed together as navmesh
// tiles are.

#ifndef MAUL_NAV_FLIGHT_H
#define MAUL_NAV_FLIGHT_H

#include "maul-nav/bake.h"
#include "maul-nav/base.h"
#include "maul-nav/draw.h"
#include "maul-nav/navmesh.h"
#include "maul-nav/query.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The flight tile format's version, written into every flight tile.
#define MNAV_FLIGHT_FORMAT 1

// The range of a flight tile's side, in voxels: 4 times a power of 2.
#define MNAV_MIN_FLIGHT_TILE_VOXELS 16
#define MNAV_MAX_FLIGHT_TILE_VOXELS 512

// The largest values a flight tile's node and leaf limits may take.
#define MNAV_MAX_FLIGHT_TILE_NODES  16777216
#define MNAV_MAX_FLIGHT_TILE_LEAVES 16777216
// The most voxels the box mnavDebugFlight draws may span.
#define MNAV_MAX_FLIGHT_DEBUG_VOXELS 16777216

    // The named limits that bound a flight bake's work and a flight
    // volume's size. Each is at least 1 and at most its MNAV_MAX_ value.
    // Reaching one is mnav_errorLimit.
    typedef struct mnavFlightLimits
    {
        // Input triangles a bake reads, as mnavBakeLimits counts them.
        int32_t inputTriangles;
        // Solid spans in a tile's columns, at the voxel size.
        int32_t tileSpans;
        // Octree nodes and leaves in a tile.
        int32_t tileNodes;
        int32_t tileLeaves;
        // Tiles committed in a flight volume at once.
        int32_t tiles;
        // The most memory a baker or a flight volume holds at once; more
        // than 0.
        uint64_t memoryBytes;
    } mnavFlightLimits;

    // How flight tiles are baked and what a flight volume accepts. Made
    // by mnavDefaultFlightDef and then changed.
    typedef struct mnavFlightDef
    {
        // Set by mnavDefaultFlightDef; a def not made by it is refused.
        uint32_t cookie;
        // MNAV_ABI_VERSION, set by mnavDefaultFlightDef.
        uint32_t version;
        // Where memory comes from; zeroed for the C library's functions.
        mnavAllocator allocator;
        // The world position of the input's (0, 0, 0), as in mnavBakeDef.
        mnavPos3 origin;
        // A voxel's side, in meters, from MNAV_MIN_CELL_SIZE to
        // MNAV_MAX_CELL_SIZE.
        float voxelSize;
        // A tile's side in voxels: 4 times a power of 2, from
        // MNAV_MIN_FLIGHT_TILE_VOXELS to MNAV_MAX_FLIGHT_TILE_VOXELS.
        int32_t tileVoxels;
        // The volume's bottom and top, in meters from the origin. The
        // volume runs from the floor rounded down to a voxel up in whole
        // cubes of the tile's side until it holds the ceiling, within
        // MNAV_MAX_HEIGHT_CELLS voxels of the origin.
        float floor;
        float ceiling;
        // The flier's radius, at least 0: the space it flies through
        // keeps this far from anything solid, rounded up to voxels, at
        // most the tile's side less 3 voxels.
        float radius;
        // Whether each column is solid from the floor up to its lowest
        // solid voxel, so that space beneath the ground is not open. Off
        // for open space such as a field of floating debris.
        bool groundBelow;
        mnavFlightLimits limits;
    } mnavFlightDef;

    // The setting a flight def check refused.
    typedef uint8_t mnavFlightSetting;
    enum
    {
        mnav_flightSettingNone = 0,
        mnav_flightSettingCookie = 1,
        mnav_flightSettingAllocator = 2,
        mnav_flightSettingOrigin = 3,
        mnav_flightSettingVoxelSize = 4,
        mnav_flightSettingTileVoxels = 5,
        mnav_flightSettingFloor = 6,
        mnav_flightSettingCeiling = 7,
        mnav_flightSettingRadius = 8,
        mnav_flightSettingInputTriangles = 9,
        mnav_flightSettingTileSpans = 10,
        mnav_flightSettingTileNodes = 11,
        mnav_flightSettingTileLeaves = 12,
        mnav_flightSettingTiles = 13,
        mnav_flightSettingMemoryBytes = 14,
    };

    // A flight def check's outcome: the status, and the first setting it
    // refused.
    typedef struct mnavFlightDefResult
    {
        mnavResult result;
        mnavFlightSetting setting;
    } mnavFlightDefResult;

    // What a flight bake did.
    typedef struct mnavFlightBakeReport
    {
        mnavResult result;
        // The first mesh refused, or -1, and what its check found, as in
        // mnavBakeReport.
        int32_t mesh;
        mnavInputResult input;
        // The hash of everything that shaped the tile: the generator's
        // version, the def's settings, the tile's place and the input that
        // reaches it. Written into the tile.
        uint64_t fingerprint;
        // Input triangles that reach the tile, solid spans, the tile's
        // cubes, nodes and leaves, its bytes, and the most memory the bake
        // held at once.
        int32_t triangles;
        int32_t spans;
        int32_t cubes;
        int32_t nodes;
        int32_t leaves;
        uint64_t tileBytes;
        uint64_t memoryPeak;
    } mnavFlightBakeReport;

    // A flight path search's result (mnav-0015). Short of the end point,
    // the path runs to the point searched nearest it.
    typedef struct mnavFlightPath
    {
        mnavPathEnd end;
        // The path's length in meters.
        double length;
        // The points from the start point on, each in sight of the next,
        // in the query context's memory until its next search.
        const mnavPos3* points;
        int32_t pointCount;
    } mnavFlightPath;

    // What stopped a flight raycast.
    typedef uint8_t mnavFlightStop;
    enum
    {
        // Nothing: the ray reached its end.
        mnav_flightClear = 0,
        // A voxel the flier may not enter, or the volume's floor or
        // ceiling.
        mnav_flightBlocked = 1,
        // A place with no tile loaded.
        mnav_flightNotLoaded = 2,
    };

    // A flight raycast's result: what stopped it, the fraction of the way
    // from its start where it stopped (1 when clear), and the point there.
    typedef struct mnavFlightHit
    {
        mnavFlightStop stop;
        double t;
        mnavPos3 point;
    } mnavFlightHit;

    // A flight tile baker. Made by mnavCreateFlightBaker.
    typedef struct mnavFlightBaker mnavFlightBaker;

    // A flight volume: committed flight tiles queries read. Made by
    // mnavCreateFlightVolume.
    typedef struct mnavFlightVolume mnavFlightVolume;

// The cookie mnavDefaultFlightDef sets and every function that takes a
// flight def checks.
#define MNAV_FLIGHT_DEF_COOKIE 0x464E564Du

    /// Makes a flight def with every setting at its default: 1 m voxels,
    /// tiles of 32 voxels, a volume from 0 to 64 m, a flier of 0.5 m, the
    /// ground below solid, and limits for a medium world.
    /// Built in the program, so that it carries the version of the
    /// headers the program includes.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    static inline mnavFlightDef mnavDefaultFlightDef(void)
    {
#ifdef __cplusplus
        mnavFlightDef def = {};
#else
    mnavFlightDef def = {0};
#endif
        def.cookie = MNAV_FLIGHT_DEF_COOKIE;
        def.version = MNAV_ABI_VERSION;
        def.voxelSize = 1.0f;
        def.tileVoxels = 32;
        def.floor = 0.0f;
        def.ceiling = 64.0f;
        def.radius = 0.5f;
        def.groundBelow = true;
        def.limits.inputTriangles = 4194304;
        def.limits.tileSpans = 4194304;
        def.limits.tileNodes = 1048576;
        def.limits.tileLeaves = 1048576;
        def.limits.tiles = 65536;
        def.limits.memoryBytes = 268435456;
        return def;
    }

    /// Checks a flight def.
    ///
    /// @param def  The def.
    /// @return `mnav_success`; `mnav_errorInvalid` with the first setting
    /// refused, or for a NULL def; `mnav_errorVersion` with no setting for
    /// a def built against another major or minor version.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavFlightDefResult mnavValidateFlightDef(const mnavFlightDef* def);

    /// Makes a flight baker: checks a def and keeps a copy, its allocator
    /// and its memory limit.
    ///
    /// @param def       The def.
    /// @param bakerOut  Receives the baker, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` with the setting for an
    /// invalid def or a NULL argument; `mnav_errorVersion` for a def built
    /// against another major or minor version; `mnav_errorLimit` when the baker
    /// does not fit the def's memory limit; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavFlightDefResult mnavCreateFlightBaker(const mnavFlightDef* def,
                                                                      mnavFlightBaker** bakerOut);

    /// Destroys a flight baker and the tile it holds.
    ///
    /// @param baker  The baker, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_API void mnavDestroyFlightBaker(mnavFlightBaker* baker);

    /// Bakes one flight tile from the input a navmesh bake reads
    /// (mnav-0015): every triangle is rasterized at the voxel size, and a
    /// voxel any triangle touches is solid; with groundBelow, so is each
    /// column below its lowest solid voxel; then every voxel within the
    /// flier's radius of a solid one, by whole voxels, is blocked too. A
    /// tile index in the input is not used: it lists triangles for a
    /// navmesh's tiles. The tile's bytes are the same on every platform.
    ///
    /// @param baker     The baker; the tile it held before is dropped.
    /// @param input     The meshes, terrains and volumes, as
    ///                  mnavBakeTileInput reads them.
    /// @param tileX     The tile's column.
    /// @param tileZ     The tile's row.
    /// @param reportOut Receives the report. May be NULL.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL baker or
    /// input or input mnavBakeTileInput refuses, the mesh named in the
    /// report; `mnav_errorRange` for input or a tile past the extent;
    /// `mnav_errorLimit` past a limit; `mnav_errorCapacity` when the
    /// allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBakeFlightTile(mnavFlightBaker* baker,
                                                          const mnavBakeInput* input, int32_t tileX,
                                                          int32_t tileZ,
                                                          mnavFlightBakeReport* reportOut);

    /// Copies the last baked flight tile's bytes into caller memory.
    ///
    /// @param baker     The baker.
    /// @param bufferOut Receives the bytes: at least capacity bytes; may be
    ///                  NULL when capacity is 0.
    /// @param capacity  The buffer's size in bytes.
    /// @param sizeOut   Receives the tile's size in bytes. May be NULL.
    /// @return `mnav_success`; `mnav_errorCapacity` when the buffer is
    /// smaller than the tile, with the size still written;
    /// `mnav_errorInvalid` for a NULL baker, a NULL buffer with a
    /// positive capacity, or a baker holding no tile.
    /// @par Thread safety
    /// Safe from any thread; the baker is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavCopyFlightTile(const mnavFlightBaker* baker,
                                                          uint8_t* bufferOut, size_t capacity,
                                                          size_t* sizeOut);

    /// Makes an empty flight volume for tiles baked with a def: checks the
    /// def and keeps a copy, its allocator and its limits.
    ///
    /// @param def        The def the tiles are baked with.
    /// @param volumeOut  Receives the volume, or NULL on failure.
    /// @return `mnav_success`; `mnav_errorInvalid` with the setting for an
    /// invalid def or a NULL argument; `mnav_errorVersion` for a def built
    /// against another major or minor version; `mnav_errorLimit` when the
    /// volume does not fit the def's memory limit; `mnav_errorCapacity` when
    /// the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MNAV_NODISCARD MNAV_API mnavFlightDefResult
    mnavCreateFlightVolume(const mnavFlightDef* def, mnavFlightVolume** volumeOut);

    /// Destroys a flight volume, its tiles and everything staged.
    ///
    /// @param volume  The volume, or NULL.
    /// @par Thread safety
    /// Safe from any thread; the volume is used by one thread at a time.
    MNAV_API void mnavDestroyFlightVolume(mnavFlightVolume* volume);

    /// Loads flight tile bytes, checking every field as hostile input and
    /// the tile's settings against the volume's def, and stages the tile
    /// for the next commit, in place of anything staged at its place
    /// before. Queries do not see it until the commit.
    ///
    /// @param volume  The volume.
    /// @param bytes   The tile's bytes, as mnavCopyFlightTile gives them.
    /// @param size    Their size in bytes.
    /// @return `mnav_success`; `mnav_errorInvalid` naming the section and
    /// element for malformed bytes, a tile baked with other settings (the
    /// header) or a NULL argument; `mnav_errorVersion` for another flight
    /// tile format version; `mnav_errorLimit` for a tile past the def's
    /// node or leaf limit (the header) or past the memory limit;
    /// `mnav_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the volume is used by one thread at a time.
    /// Staging changes nothing queries read, but it may not run beside
    /// another call that stages or commits.
    MNAV_NODISCARD MNAV_API mnavTileResult mnavStageFlightTile(mnavFlightVolume* volume,
                                                               const uint8_t* bytes, size_t size);

    /// Stages the removal of the flight tile at a place for the next
    /// commit, in place of anything staged there before.
    ///
    /// @param volume  The volume.
    /// @param tileX   The tile's column.
    /// @param tileZ   The tile's row.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL volume;
    /// `mnav_errorLimit` past the memory limit; `mnav_errorCapacity` when
    /// the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the volume is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavStageFlightTileRemoval(mnavFlightVolume* volume,
                                                                  int32_t tileX, int32_t tileZ);

    /// Applies everything staged at once: installs and removes the tiles.
    /// Either all of it applies or, on failure, nothing does and the
    /// staged changes stay.
    ///
    /// @param volume  The volume.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL volume;
    /// `mnav_errorLimit` when the tiles would pass the tiles limit or
    /// memory its limit; `mnav_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread; the volume is used by one thread at a time,
    /// so no query may run during a commit.
    MNAV_NODISCARD MNAV_API mnavResult mnavCommitFlight(mnavFlightVolume* volume);

    /// Reads the fingerprint of the flight tile committed at a place, as
    /// its bake reported it, so that a host can tell whether its input
    /// has changed since.
    ///
    /// @param volume          The volume.
    /// @param tileX           The tile's column.
    /// @param tileZ           The tile's row.
    /// @param fingerprintOut  Receives the fingerprint, or 0.
    /// @return `mnav_success`; `mnav_errorNotLoaded` when no tile is
    /// committed there; `mnav_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; none may while a stage or commit call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavGetFlightTile(const mnavFlightVolume* volume,
                                                         int32_t tileX, int32_t tileZ,
                                                         uint64_t* fingerprintOut);

    /// Tells whether the flier's center may be at a point: whether the
    /// voxel holding it is neither solid nor within the flier's radius of
    /// a solid voxel.
    ///
    /// @param volume   The volume.
    /// @param point    The point, in world coordinates.
    /// @param openOut  Receives whether the point is open; false on
    ///                 failure.
    /// @return `mnav_success`; `mnav_errorNotLoaded` when no tile is
    /// committed under the point; `mnav_errorRange` for a point not
    /// finite or below or above the volume; `mnav_errorInvalid` for a
    /// NULL argument.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; none may while a stage or commit call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavIsFlightOpen(const mnavFlightVolume* volume,
                                                        mnavPos3 point, bool* openOut);

    /// Finds a flier's path between two points (mnav-0015): Lazy Theta*
    /// (Nash, Koenig and Tovey, 2010) over the open blocks of the
    /// committed tiles, linked through shared faces, each block entered at
    /// the point of its face nearest the point the path comes from. A
    /// point is in sight of another when the segment between them crosses
    /// no blocked voxel, nor an edge or corner of one. The search keeps to
    /// the query context's node budget and path length limit; short of the
    /// end, the path runs to the point searched nearest it.
    ///
    /// @param query    The query context; its last path is dropped.
    /// @param volume   The flight volume.
    /// @param start    The start, in world coordinates.
    /// @param end      The end, in world coordinates.
    /// @param pathOut  Receives the path. A start or end not open ends the
    ///                 search at once: `mnav_pathNone` for a blocked voxel,
    ///                 `mnav_pathNotLoaded` for a place with no tile, with
    ///                 the start alone as its path.
    /// @return `mnav_success`; `mnav_errorRange` for a point not finite,
    /// below or above the volume or past the extent; `mnav_errorInvalid`
    /// for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and the volume is not changed during the search.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindFlightPath(mnavQuery* query,
                                                          const mnavFlightVolume* volume,
                                                          mnavPos3 start, mnavPos3 end,
                                                          mnavFlightPath* pathOut);

    /// Begins a flier's path search to run in slices: the same search as
    /// mnavFindFlightPath, continued by mnavContinueFlightPath and ended by
    /// mnavFinishFlightPath. Whatever the slices, the path is the one
    /// mnavFindFlightPath gives.
    ///
    /// @param query   The query context; it holds the search until the next
    ///                search of any kind begins.
    /// @param volume  The flight volume.
    /// @param start   The start, in world coordinates.
    /// @param end     The end, in world coordinates.
    /// @return `mnav_success`; `mnav_errorRange` for a point not finite,
    /// below or above the volume or past the extent; `mnav_errorInvalid`
    /// for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavBeginFlightPath(mnavQuery* query,
                                                           const mnavFlightVolume* volume,
                                                           mnavPos3 start, mnavPos3 end);

    /// Continues a search begun by mnavBeginFlightPath: closes up to a
    /// number of nodes, fewer when the search ends first.
    ///
    /// @param query    The query context.
    /// @param volume   The volume the search began on.
    /// @param nodes    The most nodes to close, at least 1.
    /// @param endedOut Receives whether the search has ended.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, no
    /// flight search begun, or fewer than 1 node; `mnav_errorStale` for
    /// another volume, or one committed to since the search began: begin
    /// again.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and the volume is not changed during the call.
    MNAV_NODISCARD MNAV_API mnavResult mnavContinueFlightPath(mnavQuery* query,
                                                              const mnavFlightVolume* volume,
                                                              int32_t nodes, bool* endedOut);

    /// Ends a search begun by mnavBeginFlightPath and writes its path: as
    /// mnavFindFlightPath's when it has ended; otherwise to the point
    /// searched nearest the end, its end mnav_pathUnfinished.
    ///
    /// @param query    The query context; its memory holds the path.
    /// @param volume   The volume the search began on.
    /// @param pathOut  Receives the path.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument or
    /// no flight search begun; `mnav_errorStale` for another volume, or one
    /// committed to since the search began.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time,
    /// and the volume is not changed during the call.
    MNAV_NODISCARD MNAV_API mnavResult mnavFinishFlightPath(mnavQuery* query,
                                                            const mnavFlightVolume* volume,
                                                            mnavFlightPath* pathOut);

    /// Casts a flier's ray: walks the voxels the segment crosses, and
    /// where it passes an edge or a corner every voxel it touches, and
    /// stops at the first one the flier may not enter or with no tile.
    ///
    /// @param volume  The flight volume.
    /// @param from    The ray's start, in world coordinates.
    /// @param to      Its end.
    /// @param hitOut  Receives what stopped it, where and at what fraction.
    /// @return `mnav_success`; `mnav_errorRange` for a point not finite or
    /// past the extent; `mnav_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; none may while a stage or commit call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavFlightRaycast(const mnavFlightVolume* volume,
                                                         mnavPos3 from, mnavPos3 to,
                                                         mnavFlightHit* hitOut);

    /// Checks a flier's path against the volume as it is now: casts each
    /// step's ray in order, as mnavFlightRaycast does, and stops at the
    /// first step that meets a voxel the flier may not enter or a place
    /// with no tile. A host that follows a path checks what is left of it
    /// after a commit, and searches again from where it is when a step
    /// fails.
    ///
    /// @param volume      The flight volume.
    /// @param points      The path's points, in world coordinates; may be
    ///                    NULL when pointCount is 0.
    /// @param pointCount  How many, at least 0.
    /// @param stepOut     Receives the first step not clear, i for the
    ///                    one from point i to point i + 1, or -1 when every
    ///                    step is clear.
    /// @param hitOut      Receives what stopped that step, as
    ///                    mnavFlightRaycast gives it; may be NULL, and is
    ///                    left alone when every step is clear.
    /// @return `mnav_success`; `mnav_errorRange` for a point not finite or
    /// past the extent, before any step is cast; `mnav_errorInvalid` for a
    /// NULL argument or a negative count.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; none may while a stage or commit call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavCheckFlightPath(const mnavFlightVolume* volume,
                                                           const mnavPos3* points,
                                                           int32_t pointCount, int32_t* stepOut,
                                                           mnavFlightHit* hitOut);

    /// Finds the nearest point open to the flier's center within a radius
    /// of a point: the point itself when it is open, or else a point just
    /// inside the nearest open voxel, at most a thousandth of a voxel in
    /// from its face. Tiles not loaded are not searched.
    ///
    /// @param volume      The flight volume.
    /// @param point       The point, in world coordinates.
    /// @param radius      How far to look, in meters, at least 0.
    /// @param nearestOut  Receives the nearest open point.
    /// @param foundOut    Receives whether one lies within the radius.
    /// @return `mnav_success`; `mnav_errorRange` for a point not finite or
    /// past the extent, or a radius below 0 or not finite;
    /// `mnav_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; none may while a stage or commit call runs.
    MNAV_NODISCARD MNAV_API mnavResult mnavFindNearestFlightPoint(const mnavFlightVolume* volume,
                                                                  mnavPos3 point, float radius,
                                                                  mnavPos3* nearestOut,
                                                                  bool* foundOut);

    /// Appends the open blocks of a flight volume's octrees inside a box,
    /// each as a wire box of twelve lines (mnav_debugFlightBlock, its value
    /// the block's side in voxels), drawn once however much of it the box
    /// holds; solid space shows as the gaps. A block shows where a flier's
    /// center may go and how the octree merged open space.
    ///
    /// @param volume  The flight volume.
    /// @param low     The box's lowest corner, in world coordinates.
    /// @param high    Its highest corner, at least low on every axis.
    /// @param buffer  The buffer appended to.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// corner not finite, a box backward, or a buffer with a count out of
    /// range, an array missing or an origin not finite; `mnav_errorRange`
    /// for a corner past the extent or a box of more than
    /// MNAV_MAX_FLIGHT_DEBUG_VOXELS voxels within the floor and ceiling;
    /// `mnav_errorCapacity` when the buffer filled, its counts saying what
    /// the whole needs.
    /// @par Thread safety
    /// Safe from any thread. Any number of threads may read the volume at
    /// once between commits; the buffer is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugFlight(const mnavFlightVolume* volume, mnavPos3 low,
                                                       mnavPos3 high, mnavDebugBuffer* buffer);

    /// Appends a flight path's steps, point to point (mnav_debugPath).
    ///
    /// @param path    The path.
    /// @param buffer  The buffer appended to.
    /// @return `mnav_success`; `mnav_errorInvalid` for a NULL argument, a
    /// path with a negative count or no points, or a buffer as
    /// mnavDebugFlight refuses it; `mnav_errorCapacity` when the buffer
    /// filled.
    /// @par Thread safety
    /// Safe from any thread; the buffer is used by one thread at a time.
    MNAV_NODISCARD MNAV_API mnavResult mnavDebugFlightPath(const mnavFlightPath* path,
                                                           mnavDebugBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // MAUL_NAV_FLIGHT_H
