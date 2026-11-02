/* Checks Rawframe makes of a glTF that cgltf does not, before cgltf reads
 * its accessors. This file and rawframe_gltf_checks.c are ours, kept beside
 * cgltf's implementation so every importer that reads glTF shares them. */
#ifndef RAWFRAME_GLTF_CHECKS_H
#define RAWFRAME_GLTF_CHECKS_H

#include "cgltf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Whether every accessor's data, and every sparse accessor's indices and
 * values, starts at an address that is a multiple of its component's size,
 * with a buffer view stride that is one too, as glTF 2.0 requires of the
 * offsets. cgltf reads accessor data through pointers of the component's
 * type, so data that is not aligned would be read misaligned. Run after
 * cgltf_load_buffers and before cgltf_validate or any read. */
int rawframe_gltf_accessors_aligned(const cgltf_data* data);

#ifdef __cplusplus
}
#endif

#endif
