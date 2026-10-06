// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binding tables (mrhi-0011): a whole table set while recording, each
// binding checked against its slot in the pass's pipeline's reflection
// as WebGPU checks a bind group, and against an access the pass
// declares, then recorded after the table's command.

#include "capabilities_core.h"
#include "encoder_core.h"

#include <string.h>

static_assert(1 + MRHI_TABLE_BINDINGS <= MRHI_CHUNK_COMMANDS, "a whole table fits a chunk");

// The reflection of the pass's pipeline: or NULL with the refusal.
static const mrhiReflection* ReflectionOf(const mrhiDevice* device, const mrhiFramePass* pass,
                                          mrhiResult* statusOut)
{
    if (pass->pipeline == 0)
    {
        *statusOut = mrhi_errorState;
        return nullptr;
    }
    if (!mrhiPoolIsLive(&device->pipelines, pass->pipeline, pass->pipelineGeneration))
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    return device->pipelineSlots[pass->pipeline - 1].reflection;
}

// The bindings of a table in a reflection.
static uint32_t TableSize(const mrhiReflection* reflection, uint32_t table)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        count += reflection->bindings[i].table == table ? 1 : 0;
    }
    return count;
}

// The binding of a table's slot, with its position among the table's
// bindings: or NULL for a slot the table lacks.
static const mrhiShaderBinding* SlotOf(const mrhiReflection* reflection, uint32_t table,
                                       uint32_t slot, uint32_t* positionOut)
{
    uint32_t position = 0;
    for (uint32_t i = 0; i < reflection->bindingCount; ++i)
    {
        const mrhiShaderBinding* binding = &reflection->bindings[i];
        if (binding->table != table)
        {
            continue;
        }
        if (binding->slot == slot)
        {
            *positionOut = position;
            return binding;
        }
        ++position;
    }
    return nullptr;
}

// The access kinds that declare what a binding does: reading, writing,
// or both. WGSL has no write-only storage buffer, so a read-write one
// also takes a write access.
static uint32_t KindsOf(const mrhiShaderBinding* binding)
{
    uint32_t readWrite = MRHI_KIND(mrhi_accessStorageReadWrite);
    switch (binding->kind)
    {
    case mrhi_bindingUniformBuffer:
        return MRHI_KIND(mrhi_accessUniform);
    case mrhi_bindingStorageBuffer:
        return MRHI_KIND(mrhi_accessStorageWrite) | readWrite;
    case mrhi_bindingReadOnlyStorageBuffer:
        return MRHI_KIND(mrhi_accessStorageRead) | readWrite;
    case mrhi_bindingSampledTexture:
        return MRHI_KIND(mrhi_accessSampled);
    default:
        break;
    }
    switch (binding->access)
    {
    case mrhi_storageReadOnly:
        return MRHI_KIND(mrhi_accessStorageRead) | readWrite;
    case mrhi_storageWriteOnly:
        return MRHI_KIND(mrhi_accessStorageWrite) | readWrite;
    default:
        return readWrite;
    }
}

// Whether a view of a format and aspect samples as a sample type: a
// stencil aspect as uint, depth as depth or unfilterable float, and a
// color format as its scalar class, float being filterable only where
// the device filters the format. Every stencil format has depth, and a
// view of both aspects is refused before.
static bool IsSampledAs(const mrhiDevice* device, mrhiFormat format, mrhiTextureAspect aspect,
                        mrhiSampleType type)
{
    if (aspect == mrhi_aspectStencilOnly)
    {
        return type == mrhi_sampleUint;
    }
    if (mrhiFormatHasDepth(format))
    {
        return type == mrhi_sampleDepth || type == mrhi_sampleUnfilterableFloat;
    }
    switch (mrhiGetFormatTarget(format).scalar)
    {
    case mrhi_scalarSint32:
        return type == mrhi_sampleSint;
    case mrhi_scalarUint32:
        return type == mrhi_sampleUint;
    default:
        break;
    }
    uint32_t index = mrhiFormatIndex(format);
    bool filtering = index < MRHI_KNOWN_FORMATS && device->formatCaps[index].filtering;
    return type == mrhi_sampleUnfilterableFloat || (type == mrhi_sampleFloat && filtering);
}

// Checks a buffer binding and records its range: success, or
// mrhi_errorInvalid.
static mrhiResult CheckBuffer(const mrhiDevice* device, const mrhiShaderBinding* slot,
                              const mrhiBinding* binding, uint64_t total,
                              mrhiCommandBinding* recordedOut)
{
    bool uniform = slot->kind == mrhi_bindingUniformBuffer;
    const mrhiLimits* limits = &device->limits;
    uint64_t alignment = uniform ? limits->uniformOffsetAlignment : limits->storageOffsetAlignment;
    uint64_t most = uniform ? limits->uniformBindingBytes : limits->storageBindingBytes;
    uint64_t offset = binding->offset;
    if (offset > total)
    {
        return mrhi_errorInvalid;
    }
    uint64_t size = binding->size == MRHI_WHOLE_SIZE ? total - offset : binding->size;
    if (size == 0 || offset % alignment != 0 || size > total - offset || size > most ||
        size < slot->minSize || (!uniform && size % 4 != 0))
    {
        return mrhi_errorInvalid;
    }
    recordedOut->offset = offset;
    recordedOut->size = size;
    return mrhi_success;
}

// Checks a texture binding and records its view: success, or
// mrhi_errorInvalid.
static mrhiResult CheckTexture(const mrhiDevice* device, const mrhiShaderBinding* slot,
                               const mrhiBinding* binding, const mrhiTextureDef* texture,
                               mrhiCommandBinding* recordedOut)
{
    mrhiViewDef def = {
        .kind = binding->viewKind,
        .format = binding->viewFormat,
        .aspect = binding->range.aspect,
        .baseMip = binding->range.baseMip,
        .mipCount = binding->range.mipCount,
        .baseLayer = binding->range.baseLayer,
        .layerCount = binding->range.layerCount,
    };
    mrhiViewDef view;
    bool both = mrhiFormatHasDepth(texture->format) && mrhiFormatHasStencil(texture->format);
    if (def.kind != slot->viewDimension || def.aspect > mrhi_aspectStencilOnly ||
        (both && def.aspect == mrhi_aspectAll) || !mrhiResolveView(texture, &def, &view))
    {
        return mrhi_errorInvalid;
    }
    bool valid = slot->kind == mrhi_bindingSampledTexture
                     ? slot->multisampled == (texture->sampleCount > 1) &&
                           IsSampledAs(device, view.format, view.aspect, slot->sampleType)
                     : view.mipCount == 1 && view.format == slot->format;
    if (!valid)
    {
        return mrhi_errorInvalid;
    }
    recordedOut->offset = view.baseLayer;
    recordedOut->size = view.layerCount;
    recordedOut->viewFormat = view.format;
    recordedOut->viewKind = view.kind;
    recordedOut->aspect = view.aspect;
    recordedOut->baseMip = (uint8_t)view.baseMip;
    recordedOut->mipCount = (uint8_t)view.mipCount;
    return mrhi_success;
}

// Checks a sampler binding: success, or the refusal.
static mrhiResult CheckSampler(const mrhiDevice* device, const mrhiShaderBinding* slot,
                               const mrhiBinding* binding, mrhiCommandBinding* recordedOut)
{
    if (binding->resource.index1 != 0)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->samplers, binding->sampler.index1, binding->sampler.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiSamplerSlot* sampler = &device->samplerSlots[binding->sampler.index1 - 1];
    bool comparison = slot->sampler == mrhi_samplerComparison;
    if (sampler->comparison != comparison ||
        (slot->sampler == mrhi_samplerNonFiltering && sampler->filtering))
    {
        return mrhi_errorInvalid;
    }
    recordedOut->offset = sampler->handle;
    return mrhi_success;
}

// Checks one binding against its slot and the pass, recording it:
// success, or the refusal.
static mrhiResult CheckBinding(const mrhiDevice* device, const mrhiFramePass* pass,
                               const mrhiShaderBinding* slot, const mrhiBinding* binding,
                               mrhiCommandBinding* recordedOut)
{
    *recordedOut = (mrhiCommandBinding){.slot = binding->slot, .kind = slot->kind};
    if (slot->kind == mrhi_bindingSampler)
    {
        return CheckSampler(device, slot, binding, recordedOut);
    }
    uint32_t object = mrhiFindFrameResource(device, binding->resource);
    if (object == 0)
    {
        return mrhi_errorStale;
    }
    recordedOut->object = object;
    const mrhiFrameResource* resource = &device->frameResources[object - 1];
    bool buffer = resource->kind == mrhiFrameBuffer || resource->kind == mrhiImportedBuffer;
    bool bufferSlot = slot->kind <= mrhi_bindingReadOnlyStorageBuffer;
    if (buffer != bufferSlot)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    uint8_t planes = 1;
    if (buffer)
    {
        status = CheckBuffer(device, slot, binding, mrhiBufferBytesOf(resource), recordedOut);
    }
    else
    {
        const mrhiTextureDef* texture = mrhiFrameTextureOf(resource);
        status = CheckTexture(device, slot, binding, texture, recordedOut);
        planes = recordedOut->aspect == mrhi_aspectStencilOnly ? 2
                 : recordedOut->aspect == mrhi_aspectDepthOnly ? 1
                                                               : mrhiFormatPlanes(texture->format);
    }
    mrhiFrameUse part = {
        .planes = planes,
        .baseMip = recordedOut->baseMip,
        .mipCount = recordedOut->mipCount,
        .baseLayer = (uint32_t)recordedOut->offset,
        .layerCount = (uint32_t)recordedOut->size,
    };
    if (status == mrhi_success &&
        !mrhiPassDeclares(device, pass, object, KindsOf(slot), buffer ? nullptr : &part))
    {
        status = mrhi_errorInvalid;
    }
    return status;
}

mrhiResult mrhiSetBindings(mrhiDevice* device, mrhiPassId id, uint32_t table,
                           const mrhiBinding* bindings, uint32_t count)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (bindings == nullptr && count > 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiRecordingPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    if (mrhiWorkOf(pass) == mrhiWorkTransfer || table >= device->limits.bindingTables)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticBindingsTable);
    }
    const mrhiReflection* reflection = ReflectionOf(device, pass, &status);
    if (reflection == nullptr)
    {
        return status;
    }
    if (count != TableSize(reflection, table))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticBindingsCount);
    }
    // Checked first, so that a refused table takes no room.
    mrhiCommandBinding recorded[MRHI_TABLE_BINDINGS];
    bool seen[MRHI_TABLE_BINDINGS] = {0};
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t position = 0;
        const mrhiShaderBinding* slot = SlotOf(reflection, table, bindings[i].slot, &position);
        if (slot == nullptr || seen[position])
        {
            return mrhiDeviceMisuse(device, mrhi_diagnosticBindingsSlot);
        }
        seen[position] = true;
        status = CheckBinding(device, pass, slot, &bindings[i], &recorded[i]);
        if (status != mrhi_success)
        {
            return status == mrhi_errorInvalid
                       ? mrhiDeviceMisuse(device, mrhi_diagnosticBindingResource)
                       : status;
        }
    }
    mrhiCommand* records = mrhiTakeCommands(device, pass, 1 + count);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    memcpy(&records[1], recorded, count * sizeof(recorded[0]));
    pass->tablesSet |= (uint8_t)(1u << table);
    memcpy(pass->tableDigests[table], reflection->digest, MRHI_DIGEST_BYTES);
    records[0] = (mrhiCommand){
        .type = mrhiCommandBindings, .payload = (uint16_t)count, .a = table, .b = count};
    return mrhi_success;
}
