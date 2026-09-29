// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The container's parse: the header and digest, the section table's
// bounds and overlaps, then every record against the rules of
// docs/contract/container.md. Every read is bounded by the sizes checked
// before it, and bytes are read one at a time, so nothing depends on the
// caller's alignment.

#include "container.h"

#include "bytes.h"
#include "capabilities_core.h"
#include "label.h"
#include "sha256.h"

#include <stdckdint.h>
#include <string.h>

#define HEADER_BYTES   64
#define SECTION_BYTES  24
#define ENTRY_BYTES    48
#define BINDING_BYTES  24
#define VARIABLE_BYTES 8
#define CONSTANT_BYTES 16
#define SECTION_TYPES  10
#define MAX_SECTIONS   64
#define MAX_ROOT_BLOCK 256
#define MAX_NAME       256
// The records an array section holds at most, which also bounds the
// uniqueness checks' work.
#define MAX_RECORDS 4096
#define SPIRV_MAGIC 0x07230203u

enum
{
    SECTION_META = 1,
    SECTION_STRINGS,
    SECTION_ENTRIES,
    SECTION_BINDINGS,
    SECTION_INPUTS,
    SECTION_OUTPUTS,
    SECTION_CONSTANTS,
    SECTION_SPIRV,
    SECTION_WGSL,
    SECTION_VARIABLES,
};

static bool IsZero(const uint8_t* at, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (at[i] != 0)
        {
            return false;
        }
    }
    return true;
}

// A section as the table gives it.
typedef struct Section
{
    uint32_t type;
    uint64_t offset;
    uint64_t size;
} Section;

// Checks the header: success with the section count, version or invalid.
static mrhiResult CheckHeader(const uint8_t* bytes, size_t size, uint32_t* countOut)
{
    if (size < HEADER_BYTES || bytes[0] != 'M' || bytes[1] != 'R' || bytes[2] != 'S' ||
        bytes[3] != 'C')
    {
        return mrhi_errorInvalid;
    }
    if (mrhiRead32(bytes + 4) != 1)
    {
        return mrhi_errorVersion;
    }
    uint32_t count = mrhiRead32(bytes + 48);
    if (mrhiRead64(bytes + 8) != size || !IsZero(bytes + 52, 12) || count > MAX_SECTIONS ||
        count * (uint64_t)SECTION_BYTES > size - HEADER_BYTES)
    {
        return mrhi_errorInvalid;
    }
    uint8_t digest[MRHI_DIGEST_BYTES];
    mrhiSha256(bytes + 48, size - 48, digest);
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        if (digest[i] != bytes[16 + i])
        {
            return mrhi_errorInvalid;
        }
    }
    *countOut = count;
    return mrhi_success;
}

// Reads and checks the section table: every section after the table,
// inside the container, 8-byte aligned, apart from the others, and each
// known type at most once.
static bool ReadSections(const uint8_t* bytes, size_t size, uint32_t count, Section* sections)
{
    uint64_t tableEnd = HEADER_BYTES + (uint64_t)count * SECTION_BYTES;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* record = bytes + HEADER_BYTES + (size_t)i * SECTION_BYTES;
        Section section = {mrhiRead32(record), mrhiRead64(record + 8), mrhiRead64(record + 16)};
        uint64_t end = 0;
        if (mrhiRead32(record + 4) != 0 || section.offset % 8 != 0 || section.offset < tableEnd ||
            ckd_add(&end, section.offset, section.size) || end > size)
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            bool apart = sections[j].offset + sections[j].size <= section.offset ||
                         end <= sections[j].offset;
            bool repeated = sections[j].type == section.type && section.type <= SECTION_TYPES;
            if (!apart || repeated)
            {
                return false;
            }
        }
        sections[i] = section;
    }
    return true;
}

// Finds a section by type: its bytes and size, or NULL when absent.
static const uint8_t* FindSection(const uint8_t* bytes, const Section* sections, uint32_t count,
                                  uint32_t type, uint64_t* sizeOut)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (sections[i].type == type)
        {
            *sizeOut = sections[i].size;
            return bytes + sections[i].offset;
        }
    }
    *sizeOut = 0;
    return nullptr;
}

// Takes an array section: false when its size is not a whole number of
// records, or holds more than MAX_RECORDS.
static bool TakeArray(const uint8_t* bytes, const Section* sections, uint32_t count, uint32_t type,
                      uint32_t recordBytes, const uint8_t** arrayOut, uint32_t* countOut)
{
    uint64_t size = 0;
    *arrayOut = FindSection(bytes, sections, count, type, &size);
    *countOut = (uint32_t)(size / recordBytes);
    return size % recordBytes == 0 && size / recordBytes <= MAX_RECORDS;
}

mrhiShaderEntry mrhiContainerEntry(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->entries + (size_t)index * ENTRY_BYTES;
    return (mrhiShaderEntry){
        .stage = mrhiRead32(at),
        .nameOffset = mrhiRead32(at + 4),
        .nameLength = mrhiRead32(at + 8),
        .workgroup = {mrhiRead32(at + 12), mrhiRead32(at + 16), mrhiRead32(at + 20)},
        .firstInput = mrhiRead16(at + 24),
        .inputCount = mrhiRead16(at + 26),
        .firstOutput = mrhiRead16(at + 28),
        .outputCount = mrhiRead16(at + 30),
        .firstVariable = mrhiRead16(at + 32),
        .variableCount = mrhiRead16(at + 34),
        .builtins = mrhiRead32(at + 36),
        .workgroupStorageBytes = mrhiRead32(at + 40),
        .heapUses = mrhiRead32(at + 44),
    };
}

mrhiShaderBinding mrhiContainerBinding(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->bindings + (size_t)index * BINDING_BYTES;
    return (mrhiShaderBinding){
        .table = at[0],
        .kind = at[1],
        .slot = mrhiRead16(at + 2),
        .stages = mrhiRead32(at + 4),
        .sampler = at[8],
        .sampleType = at[9],
        .viewDimension = at[10],
        .access = at[11],
        .format = mrhiRead16(at + 12),
        .multisampled = at[14] != 0,
        .minSize = mrhiRead64(at + 16),
    };
}

static mrhiShaderVariable ReadVariable(const uint8_t* records, uint32_t index)
{
    const uint8_t* at = records + (size_t)index * VARIABLE_BYTES;
    return (mrhiShaderVariable){
        .location = mrhiRead32(at),
        .type = at[4],
        .components = at[5],
        .interpolation = at[6],
        .sampling = at[7],
    };
}

mrhiShaderVariable mrhiContainerInput(const mrhiContainer* container, uint32_t index)
{
    return ReadVariable(container->inputs, index);
}

mrhiShaderVariable mrhiContainerOutput(const mrhiContainer* container, uint32_t index)
{
    return ReadVariable(container->outputs, index);
}

mrhiShaderVariable mrhiContainerVariable(const mrhiContainer* container, uint32_t index)
{
    return ReadVariable(container->variables, index);
}

mrhiShaderConstant mrhiContainerConstant(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->constants + (size_t)index * CONSTANT_BYTES;
    return (mrhiShaderConstant){
        .id = mrhiRead32(at),
        .type = at[4],
        .bits = mrhiRead32(at + 8),
        .required = at[12] != 0,
    };
}

// What an interface record is.
typedef enum Role
{
    ROLE_INPUT,
    ROLE_OUTPUT,
    ROLE_VARIABLE,
} Role;

// Whether an inter-stage variable is interpolated as WGSL allows:
// integers flat, flat from the first or either vertex, and the others
// sampled at the center, the centroid or per sample.
static bool IsInterpolationValid(mrhiShaderVariable variable)
{
    bool integer = variable.type == mrhi_scalarSint32 || variable.type == mrhi_scalarUint32;
    switch (variable.interpolation)
    {
    case mrhi_interpolationPerspective:
    case mrhi_interpolationLinear:
        return !integer && variable.sampling >= mrhi_samplingCenter &&
               variable.sampling <= mrhi_samplingSample;
    case mrhi_interpolationFlat:
        return variable.sampling == mrhi_samplingFirst || variable.sampling == mrhi_samplingEither;
    default:
        return false;
    }
}

// Whether every record of an interface section is well formed for its
// role, whether or not an entry names it; notes 16-bit floats.
static bool AreVariablesValid(const uint8_t* records, uint32_t count, Role role, bool* float16Out)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiShaderVariable variable = ReadVariable(records, i);
        bool type = variable.type >= mrhi_scalarFloat32 && variable.type <= mrhi_scalarUint32 &&
                    !(role == ROLE_INPUT && variable.type == mrhi_scalarFloat16);
        bool interpolation = role == ROLE_VARIABLE
                                 ? IsInterpolationValid(variable)
                                 : variable.interpolation == 0 && variable.sampling == 0;
        bool location = role != ROLE_OUTPUT || variable.location < MRHI_COLOR_TARGETS;
        if (!type || variable.components < 1 || variable.components > 4 || !interpolation ||
            !location)
        {
            return false;
        }
        *float16Out = *float16Out || variable.type == mrhi_scalarFloat16;
    }
    return true;
}

// Whether a range of records lies in an array of count.
static bool InArray(uint32_t first, uint32_t length, uint32_t count)
{
    return first + length <= count;
}

// Whether an entry is well formed, its name unique among those before.
static bool IsEntryValid(const mrhiContainer* container, uint32_t index)
{
    mrhiShaderEntry entry = mrhiContainerEntry(container, index);
    bool vertex = entry.stage == mrhi_stageVertex;
    bool fragment = entry.stage == mrhi_stageFragment;
    bool compute = entry.stage == mrhi_stageCompute;
    bool workgroup =
        compute ? entry.workgroup[0] > 0 && entry.workgroup[1] > 0 && entry.workgroup[2] > 0
                : entry.workgroup[0] == 0 && entry.workgroup[1] == 0 && entry.workgroup[2] == 0 &&
                      entry.workgroupStorageBytes == 0;
    bool name =
        entry.nameLength > 0 && entry.nameLength <= MAX_NAME &&
        (uint64_t)entry.nameOffset + entry.nameLength <= container->stringBytes &&
        mrhiIsTextValid((const char*)container->strings + entry.nameOffset, entry.nameLength);
    bool inputs = InArray(entry.firstInput, entry.inputCount, container->inputCount) &&
                  (entry.inputCount == 0 || vertex);
    bool outputs = InArray(entry.firstOutput, entry.outputCount, container->outputCount) &&
                   (entry.outputCount == 0 || fragment);
    bool variables = InArray(entry.firstVariable, entry.variableCount, container->variableCount) &&
                     (entry.variableCount == 0 || !compute);
    bool builtins =
        (entry.builtins & ~mrhiShaderBuiltinsKnown) == 0 && (entry.builtins == 0 || fragment);
    // Writes go with the storage kinds, never in a vertex entry, as
    // WebGPU's rule for bound storage.
    uint32_t storage = mrhi_heapUseStorageTextures | mrhi_heapUseStorageBuffers;
    bool writes =
        (entry.heapUses & mrhi_heapUseWrites) == 0 || ((entry.heapUses & storage) != 0 && !vertex);
    bool heap = (entry.heapUses & ~mrhiShaderHeapUsesKnown) == 0 && writes;
    if (!(vertex || fragment || compute) || !workgroup || !name || !inputs || !outputs ||
        !variables || !builtins || !heap)
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderEntry other = mrhiContainerEntry(container, i);
        if (other.nameLength == entry.nameLength &&
            memcmp(container->strings + other.nameOffset, container->strings + entry.nameOffset,
                   entry.nameLength) == 0)
        {
            return false;
        }
    }
    return true;
}

// Whether the locations of a range of interface records are unique.
static bool AreLocationsUnique(const uint8_t* records, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t location = ReadVariable(records, first + i).location;
        for (uint32_t j = 0; j < i; ++j)
        {
            if (ReadVariable(records, first + j).location == location)
            {
                return false;
            }
        }
    }
    return true;
}

// Whether an entry's inputs, outputs and variables each have unique
// locations.
static bool AreEntryLocationsUnique(const mrhiContainer* container, mrhiShaderEntry entry)
{
    return AreLocationsUnique(container->inputs, entry.firstInput, entry.inputCount) &&
           AreLocationsUnique(container->outputs, entry.firstOutput, entry.outputCount) &&
           AreLocationsUnique(container->variables, entry.firstVariable, entry.variableCount);
}

// Whether a binding's details suit its kind.
static bool AreDetailsValid(const mrhiShaderBinding* binding)
{
    bool buffer = binding->kind == mrhi_bindingUniformBuffer ||
                  binding->kind == mrhi_bindingStorageBuffer ||
                  binding->kind == mrhi_bindingReadOnlyStorageBuffer;
    bool sampler = binding->kind == mrhi_bindingSampler;
    bool sampled = binding->kind == mrhi_bindingSampledTexture;
    bool storage = binding->kind == mrhi_bindingStorageTexture;
    bool samplerOk = sampler ? binding->sampler >= mrhi_samplerFiltering &&
                                   binding->sampler <= mrhi_samplerComparison
                             : binding->sampler == mrhi_samplerNone;
    bool sampleOk =
        sampled ? binding->sampleType >= mrhi_sampleFloat && binding->sampleType <= mrhi_sampleUint
                : binding->sampleType == mrhi_sampleNone;
    bool accessOk = storage ? binding->access >= mrhi_storageReadOnly &&
                                  binding->access <= mrhi_storageReadWrite &&
                                  mrhiIsFormatKnown(binding->format)
                            : binding->access == mrhi_storageNone && binding->format == 0;
    bool texture = sampled || storage;
    bool dimension = !texture  ? binding->viewDimension == 0
                     : storage ? binding->viewDimension == mrhi_texture2d ||
                                     binding->viewDimension == mrhi_texture2dArray ||
                                     binding->viewDimension == mrhi_texture3d
                               : binding->viewDimension <= mrhi_texture3d;
    bool multisampled =
        !binding->multisampled || (sampled && binding->viewDimension == mrhi_texture2d &&
                                   binding->sampleType != mrhi_sampleFloat);
    return samplerOk && sampleOk && accessOk && dimension && multisampled &&
           (buffer || binding->minSize == 0);
}

// Whether a binding is well formed, its table and slot unique among those
// before.
static bool IsBindingValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->bindings + (size_t)index * BINDING_BYTES;
    mrhiShaderBinding binding = mrhiContainerBinding(container, index);
    bool writable =
        binding.kind == mrhi_bindingStorageBuffer ||
        (binding.kind == mrhi_bindingStorageTexture && binding.access != mrhi_storageReadOnly);
    bool stages = binding.stages != 0 && (binding.stages & ~mrhiShaderStagesKnown) == 0 &&
                  !(writable && (binding.stages & mrhi_stageVertex) != 0);
    if (raw[14] > 1 || raw[15] != 0 || binding.table >= 4 || binding.kind == mrhi_bindingNone ||
        binding.kind > mrhi_bindingStorageTexture || !stages || !AreDetailsValid(&binding))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderBinding other = mrhiContainerBinding(container, i);
        if (other.table == binding.table && other.slot == binding.slot)
        {
            return false;
        }
    }
    return true;
}

// Whether a constant is well formed, its id unique among those before.
static bool IsConstantValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->constants + (size_t)index * CONSTANT_BYTES;
    mrhiShaderConstant constant = mrhiContainerConstant(container, index);
    if (!IsZero(raw + 5, 3) || raw[12] > 1 || !IsZero(raw + 13, 3) ||
        constant.type == mrhi_constantNone || constant.type > mrhi_constantFloat32 ||
        (constant.type == mrhi_constantBool && constant.bits > 1) ||
        (constant.required && constant.bits != 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        if (mrhiContainerConstant(container, i).id == constant.id)
        {
            return false;
        }
    }
    return true;
}

// Whether the records agree with the rules and with each other; notes
// 16-bit floats and the builtins and heap uses of the entries.
static bool AreRecordsValid(mrhiContainer* container)
{
    if (container->entryCount == 0 ||
        !AreVariablesValid(container->inputs, container->inputCount, ROLE_INPUT,
                           &container->float16) ||
        !AreVariablesValid(container->outputs, container->outputCount, ROLE_OUTPUT,
                           &container->float16) ||
        !AreVariablesValid(container->variables, container->variableCount, ROLE_VARIABLE,
                           &container->float16))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        if (!IsEntryValid(container, i) || !AreEntryLocationsUnique(container, entry))
        {
            return false;
        }
        container->builtins |= entry.builtins;
        container->heapUses |= entry.heapUses;
    }
    // WGSL reads no heaps yet: a container using one has no WGSL, and
    // one using none needs it.
    if ((container->heapUses != 0) == (container->wgslBytes > 0))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        if (!IsBindingValid(container, i))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        if (!IsConstantValid(container, i))
        {
            return false;
        }
    }
    return true;
}

// Takes the meta, strings and code sections. An absent section has size
// 0, so the size checks require the meta and SPIR-V; the strings are
// required by the entries' names, and the WGSL by entries using no heap.
static bool TakeParts(const uint8_t* bytes, const Section* sections, uint32_t count,
                      mrhiContainer* container)
{
    uint64_t size = 0;
    const uint8_t* meta = FindSection(bytes, sections, count, SECTION_META, &size);
    if (size != 16 || !IsZero(meta + 4, 12))
    {
        return false;
    }
    container->rootBlockBytes = mrhiRead32(meta);
    container->strings = FindSection(bytes, sections, count, SECTION_STRINGS, &size);
    container->stringBytes = (uint32_t)size;
    bool strings = size <= UINT32_MAX;
    container->spirv = FindSection(bytes, sections, count, SECTION_SPIRV, &size);
    container->spirvBytes = size;
    bool spirv = size >= 20 && size % 4 == 0 && mrhiRead32(container->spirv) == SPIRV_MAGIC;
    container->wgsl = FindSection(bytes, sections, count, SECTION_WGSL, &size);
    container->wgslBytes = size;
    bool wgsl = size == 0 || mrhiIsTextValid((const char*)container->wgsl, size);
    container->wgsl = size > 0 ? container->wgsl : nullptr;
    return container->rootBlockBytes % 4 == 0 && container->rootBlockBytes <= MAX_ROOT_BLOCK &&
           strings && spirv && wgsl;
}

mrhiResult mrhiParseContainer(const void* bytes, size_t size, mrhiContainer* containerOut)
{
    const uint8_t* data = bytes;
    uint32_t count = 0;
    mrhiResult status = CheckHeader(data, size, &count);
    if (status != mrhi_success)
    {
        return status;
    }
    Section sections[MAX_SECTIONS];
    mrhiContainer container = {0};
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        container.digest[i] = data[16 + i];
    }
    bool valid = ReadSections(data, size, count, sections) &&
                 TakeParts(data, sections, count, &container) &&
                 TakeArray(data, sections, count, SECTION_ENTRIES, ENTRY_BYTES, &container.entries,
                           &container.entryCount) &&
                 TakeArray(data, sections, count, SECTION_BINDINGS, BINDING_BYTES,
                           &container.bindings, &container.bindingCount) &&
                 TakeArray(data, sections, count, SECTION_INPUTS, VARIABLE_BYTES, &container.inputs,
                           &container.inputCount) &&
                 TakeArray(data, sections, count, SECTION_OUTPUTS, VARIABLE_BYTES,
                           &container.outputs, &container.outputCount) &&
                 TakeArray(data, sections, count, SECTION_CONSTANTS, CONSTANT_BYTES,
                           &container.constants, &container.constantCount) &&
                 TakeArray(data, sections, count, SECTION_VARIABLES, VARIABLE_BYTES,
                           &container.variables, &container.variableCount) &&
                 AreRecordsValid(&container);
    if (!valid)
    {
        return mrhi_errorInvalid;
    }
    *containerOut = container;
    return mrhi_success;
}
