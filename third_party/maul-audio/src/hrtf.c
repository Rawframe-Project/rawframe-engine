// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Loading HRTF sets. The file is checked whole first (hrtf_file), then
// one block is laid out for what the set keeps, and the responses are
// copied at the file's rate or resampled to the def's.

#include "allocator.h"
#include "hrtf_core.h"
#include "hrtf_file.h"
#include "hrtf_resample.h"

#include <string.h>

#define MAX_RATE 384000u

static bool DefValid(const maudHrtfDef* def)
{
    return def->cookie == MAUD_HRTF_DEF_COOKIE && def->bytes != nullptr &&
           (def->sampleRate == 0 || (def->sampleRate >= 8000 && def->sampleRate <= MAX_RATE)) &&
           def->maxDirections >= 1 && def->maxTaps >= 1 && maudIsAllocatorValid(&def->allocator);
}

// Where each part of the set lies in its block.
typedef struct Parts
{
    size_t elevations;
    size_t azimuths;
    size_t firstDirection;
    size_t delays;
    size_t responses;
    size_t name;
    size_t license;
    size_t size;
    bool overflow;
    maudLayout layout;
} Parts;

static Parts LayOut(const maudHrtfFile* file, uint32_t taps)
{
    maudLayout layout = {.size = sizeof(maudHrtf)};
    Parts parts = {0};
    parts.elevations = maudLayoutAdd(&layout, file->ringCount, sizeof(float), alignof(float));
    parts.azimuths = maudLayoutAdd(&layout, file->ringCount, sizeof(uint32_t), alignof(uint32_t));
    parts.firstDirection =
        maudLayoutAdd(&layout, file->ringCount, sizeof(uint32_t), alignof(uint32_t));
    parts.delays =
        maudLayoutAdd(&layout, 2u * (size_t)file->directionCount, sizeof(float), alignof(float));
    parts.responses = maudLayoutAdd(&layout, 2u * (size_t)file->directionCount * taps,
                                    sizeof(float), alignof(float));
    parts.name = maudLayoutAdd(&layout, file->nameLength, 1, 1);
    parts.license = maudLayoutAdd(&layout, file->licenseLength, 1, 1);
    parts.size = layout.size;
    parts.overflow = layout.overflow;
    parts.layout = layout;
    return parts;
}

// Copies the file's rings, delays and responses into the set, resampled
// from the file's rate to the set's; scratch holds one response, and
// weights the resampling's weights when the rates differ.
static void Fill(maudHrtf* hrtf, const maudHrtfFile* file, float* scratch, double* weights)
{
    if (hrtf->sampleRate != file->sampleRate)
    {
        maudResampleWeights(file->taps, file->sampleRate, hrtf->taps, hrtf->sampleRate, weights);
    }
    uint32_t first = 0;
    for (uint32_t ring = 0; ring < file->ringCount; ++ring)
    {
        hrtf->elevations[ring] = maudHrtfFileElevation(file, ring);
        hrtf->azimuths[ring] = maudHrtfFileAzimuths(file, ring);
        hrtf->firstDirection[ring] = first;
        first += hrtf->azimuths[ring];
    }
    float ratio = (float)hrtf->sampleRate / (float)file->sampleRate;
    for (uint32_t direction = 0; direction < file->directionCount; ++direction)
    {
        for (uint32_t ear = 0; ear < 2; ++ear)
        {
            size_t at = 2u * (size_t)direction + ear;
            hrtf->delays[at] = maudHrtfFileDelay(file, direction, ear) * ratio;
            float* response = hrtf->responses + at * hrtf->taps;
            if (hrtf->sampleRate == file->sampleRate)
            {
                maudHrtfFileResponse(file, direction, ear, response);
                continue;
            }
            maudHrtfFileResponse(file, direction, ear, scratch);
            maudResampleResponse(scratch, file->taps, file->sampleRate, response, hrtf->taps,
                                 hrtf->sampleRate, weights);
        }
    }
}

maudResult maudLoadHrtf(const maudHrtfDef* def, maudHrtf** hrtfOut)
{
    if (hrtfOut != nullptr)
    {
        *hrtfOut = nullptr;
    }
    if (def != nullptr && def->cookie == MAUD_HRTF_DEF_COOKIE && def->version != MAUD_ABI_VERSION)
    {
        return maud_errorVersion;
    }
    if (def == nullptr || hrtfOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudHrtfFile file;
    maudResult result = maudReadHrtfFile(def->bytes, def->byteCount, &file);
    if (result != maud_success)
    {
        return result;
    }
    uint32_t rate = def->sampleRate != 0 ? def->sampleRate : file.sampleRate;
    uint32_t taps = maudResampledTaps(file.taps, file.sampleRate, rate);
    if (file.directionCount > def->maxDirections || taps > def->maxTaps)
    {
        return maud_errorCapacity;
    }
    Parts parts = LayOut(&file, taps);
    // One block for the resampling's weights, then one response.
    size_t weightBytes =
        rate == file.sampleRate
            ? 0
            : (size_t)taps * maudResampleWidth(file.sampleRate, rate) * sizeof(double);
    size_t scratchBytes = weightBytes + (size_t)file.taps * sizeof(float);
    double* weights =
        parts.overflow ? nullptr : maudAllocate(&def->allocator, scratchBytes, alignof(double));
    float* scratch = weights == nullptr ? nullptr : (float*)((unsigned char*)weights + weightBytes);
    unsigned char* block =
        scratch == nullptr ? nullptr : maudAllocate(&def->allocator, parts.size, alignof(maudHrtf));
    if (block == nullptr)
    {
        if (scratch != nullptr)
        {
            maudRelease(&def->allocator, weights, scratchBytes, alignof(double));
        }
        return maud_errorCapacity;
    }
    maudLayoutPoison(&parts.layout, block);
    maudHrtf* hrtf = (maudHrtf*)block;
    *hrtf = (maudHrtf){
        .allocator = def->allocator,
        .bytes = parts.size,
        .sampleRate = rate,
        .taps = taps,
        .ringCount = file.ringCount,
        .directionCount = file.directionCount,
        .distance = file.distance,
        .elevations = (float*)(block + parts.elevations),
        .azimuths = (uint32_t*)(block + parts.azimuths),
        .firstDirection = (uint32_t*)(block + parts.firstDirection),
        .delays = (float*)(block + parts.delays),
        .responses = (float*)(block + parts.responses),
        .name = (char*)(block + parts.name),
        .nameLength = file.nameLength,
        .license = (char*)(block + parts.license),
        .licenseLength = file.licenseLength,
    };
    memcpy(hrtf->name, file.name, file.nameLength);
    memcpy(hrtf->license, file.license, file.licenseLength);
    Fill(hrtf, &file, scratch, weights);
    maudRelease(&def->allocator, weights, scratchBytes, alignof(double));
    *hrtfOut = hrtf;
    return maud_success;
}

void maudDestroyHrtf(maudHrtf* hrtf)
{
    if (hrtf == nullptr)
    {
        return;
    }
    maudAllocator allocator = hrtf->allocator;
    maudRelease(&allocator, hrtf, hrtf->bytes, alignof(maudHrtf));
}

maudResult maudGetHrtfInfo(const maudHrtf* hrtf, maudHrtfInfo* infoOut)
{
    // Cleared first: a failure leaves zero (conventions section 8).
    if (infoOut != nullptr)
    {
        memset(infoOut, 0, sizeof(*infoOut));
    }
    if (hrtf == nullptr || infoOut == nullptr)
    {
        return maud_errorInvalid;
    }
    *infoOut = (maudHrtfInfo){
        .sampleRate = hrtf->sampleRate,
        .taps = hrtf->taps,
        .directionCount = hrtf->directionCount,
        .ringCount = hrtf->ringCount,
        .distance = hrtf->distance,
        .name = hrtf->name,
        .nameLength = hrtf->nameLength,
        .license = hrtf->license,
        .licenseLength = hrtf->licenseLength,
    };
    return maud_success;
}
