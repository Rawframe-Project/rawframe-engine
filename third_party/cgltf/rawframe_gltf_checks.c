#include "rawframe_gltf_checks.h"

#include <stdint.h>

/* The bytes of one component, or nought for a type cgltf_validate refuses. */
static cgltf_size component_size(cgltf_component_type type) {
    switch (type) {
    case cgltf_component_type_r_8:
    case cgltf_component_type_r_8u:
        return 1;
    case cgltf_component_type_r_16:
    case cgltf_component_type_r_16u:
        return 2;
    case cgltf_component_type_r_32u:
    case cgltf_component_type_r_32f:
        return 4;
    default:
        return 0;
    }
}

/* Whether data in `view` at `offset` is aligned to `size`. No view, no
 * loaded data, or a type of no size reads nothing through such a pointer. */
static int aligned(const cgltf_buffer_view* view, cgltf_size offset, cgltf_size size) {
    if (view == NULL || size == 0) {
        return 1;
    }
    const uint8_t* const base = cgltf_buffer_view_data(view);
    if (base == NULL) {
        return 1;
    }
    return ((uintptr_t)base + offset) % size == 0 && view->stride % size == 0;
}

int rawframe_gltf_accessors_aligned(const cgltf_data* data) {
    for (cgltf_size i = 0; i < data->accessors_count; ++i) {
        const cgltf_accessor* const accessor = &data->accessors[i];
        const cgltf_size size = component_size(accessor->component_type);
        if (!aligned(accessor->buffer_view, accessor->offset, size)) {
            return 0;
        }
        if (accessor->is_sparse) {
            const cgltf_accessor_sparse* const sparse = &accessor->sparse;
            if (!aligned(sparse->indices_buffer_view, sparse->indices_byte_offset,
                         component_size(sparse->indices_component_type)) ||
                !aligned(sparse->values_buffer_view, sparse->values_byte_offset, size)) {
                return 0;
            }
        }
    }
    return 1;
}
