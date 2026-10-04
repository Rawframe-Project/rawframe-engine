// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bake volumes (mnav-0003). A span lies in a volume when the volume's ring
// holds its cell's center, by the same scan as 2D outlines, and its floor
// lies within the volume's heights.

#include "volume.h"

#include "allocator.h"
#include "compact.h"
#include "outline.h"
#include "raster.h"

#include "maul-nav/bake.h"

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static mnavInputResult Refuse(mnavResult result, mnavInputElement element, int32_t index)
{
    return (mnavInputResult){result, element, index};
}

mnavInputResult mnavCheckVolume(const mnavBakeDef* def, const mnavBakeVolume* volume)
{
    bool area = volume->kind == mnav_volumeArea;
    if (volume->pointCount < 3 || volume->points == nullptr || volume->kind > mnav_volumeArea ||
        (area && (volume->area == mnav_areaNone || volume->area >= MNAV_AREA_TYPES)) ||
        !isfinite(volume->minY) || !isfinite(volume->maxY) || volume->minY > volume->maxY)
    {
        return Refuse(mnav_errorInvalid, mnav_elementNone, -1);
    }
    if (volume->pointCount > def->limits.inputTriangles)
    {
        return Refuse(mnav_errorLimit, mnav_elementNone, -1);
    }
    float ground = def->cellSize * (float)MNAV_MAX_EXTENT_CELLS;
    for (int32_t i = 0; i < volume->pointCount; ++i)
    {
        mnavVec2 p = volume->points[i];
        if (!isfinite(p.x) || !isfinite(p.y))
        {
            return Refuse(mnav_errorInvalid, mnav_elementPoint, i);
        }
        if (fabsf(p.x) > ground || fabsf(p.y) > ground)
        {
            return Refuse(mnav_errorRange, mnav_elementPoint, i);
        }
    }
    return Refuse(mnav_success, mnav_elementNone, -1);
}

// One volume's visit over the field.
typedef struct Visit
{
    mnavCompactField* field;
    const mnavBakeVolume* volume;
    uint8_t* marks;
} Visit;

static mnavResult VisitCell(void* context, int32_t x, int32_t z)
{
    Visit* v = context;
    mnavCompactField* field = v->field;
    const mnavBakeVolume* volume = v->volume;
    size_t column = (size_t)z * (size_t)field->frame.width + (size_t)x;
    for (uint32_t i = field->columns[column]; i < field->columns[column + 1]; ++i)
    {
        double y = ((double)field->spans[i].floor - (double)MNAV_HEIGHT_OFFSET) *
                   (double)field->frame.cellHeight;
        if (field->areas[i] == mnav_areaNone || y < (double)volume->minY ||
            y > (double)volume->maxY)
        {
            continue;
        }
        if (volume->kind == mnav_volumeInclude)
        {
            v->marks[i] = 1;
        }
        else
        {
            field->areas[i] = volume->kind == mnav_volumeExclude ? mnav_areaNone : volume->area;
        }
    }
    return mnav_success;
}

// The most points of a volume of a kind touching the tile, or 0.
static int32_t MostPoints(const mnavTileFrame* frame, const mnavBakeVolume* volumes, int32_t count,
                          mnavVolumeKind kind)
{
    int32_t most = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        const mnavBakeVolume* v = &volumes[i];
        if (v->kind == kind && v->pointCount > most &&
            mnavRingTouchesTile(frame, v->points, v->pointCount))
        {
            most = v->pointCount;
        }
    }
    return most;
}

// Visits the volumes of a kind touching the tile, in order.
static void VisitAll(mnavCompactField* field, const mnavBakeVolume* volumes, int32_t count,
                     mnavVolumeKind kind, double* crossings, uint8_t* marks)
{
    for (int32_t i = 0; i < count; ++i)
    {
        const mnavBakeVolume* volume = &volumes[i];
        if (volume->kind != kind ||
            !mnavRingTouchesTile(&field->frame, volume->points, volume->pointCount))
        {
            continue;
        }
        Visit v = {field, volume, marks};
        // The visit itself never fails.
        (void)mnavVisitRing(&field->frame, volume->points, volume->pointCount, crossings, VisitCell,
                            &v);
    }
}

// Drops the walkable spans no include volume holds.
static mnavResult Include(mnavMemory* memory, mnavCompactField* field,
                          const mnavBakeVolume* volumes, int32_t count, double* crossings)
{
    size_t spans = (size_t)field->spanCount;
    if (spans == 0)
    {
        return mnav_success;
    }
    uint8_t* marks = nullptr;
    mnavResult result =
        mnavAllocate(memory, spans, sizeof(uint8_t), alignof(uint8_t), (void**)&marks);
    if (result != mnav_success)
    {
        return result;
    }
    memset(marks, 0, spans);
    VisitAll(field, volumes, count, mnav_volumeInclude, crossings, marks);
    for (size_t i = 0; i < spans; ++i)
    {
        field->areas[i] = marks[i] != 0 ? field->areas[i] : mnav_areaNone;
    }
    mnavRelease(memory, marks, spans, sizeof(uint8_t), alignof(uint8_t));
    return mnav_success;
}

mnavResult mnavCarveVolumes(mnavMemory* memory, mnavCompactField* field,
                            const mnavBakeVolume* volumes, int32_t count)
{
    bool includes = false;
    for (int32_t i = 0; i < count; ++i)
    {
        includes = includes || volumes[i].kind == mnav_volumeInclude;
    }
    int32_t a = MostPoints(&field->frame, volumes, count, mnav_volumeInclude);
    int32_t b = MostPoints(&field->frame, volumes, count, mnav_volumeExclude);
    size_t points = (size_t)(a > b ? a : b);
    if (!includes && points == 0)
    {
        return mnav_success;
    }
    double* crossings = nullptr;
    mnavResult result =
        mnavAllocate(memory, points, sizeof(double), alignof(double), (void**)&crossings);
    if (result == mnav_success && includes)
    {
        result = Include(memory, field, volumes, count, crossings);
    }
    if (result == mnav_success)
    {
        VisitAll(field, volumes, count, mnav_volumeExclude, crossings, nullptr);
    }
    mnavRelease(memory, crossings, points, sizeof(double), alignof(double));
    return result;
}

mnavResult mnavMarkVolumes(mnavMemory* memory, mnavCompactField* field,
                           const mnavBakeVolume* volumes, int32_t count)
{
    size_t points = (size_t)MostPoints(&field->frame, volumes, count, mnav_volumeArea);
    if (points == 0)
    {
        return mnav_success;
    }
    double* crossings = nullptr;
    mnavResult result =
        mnavAllocate(memory, points, sizeof(double), alignof(double), (void**)&crossings);
    if (result == mnav_success)
    {
        VisitAll(field, volumes, count, mnav_volumeArea, crossings, nullptr);
    }
    mnavRelease(memory, crossings, points, sizeof(double), alignof(double));
    return result;
}
