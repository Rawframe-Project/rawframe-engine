// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The .maudbake file (docs/bake-format.md): a probe set's graph and
// bake written in a fixed layout, and read back from hostile bytes,
// every part checked before anything is allocated.

#ifndef MAUL_AUDIO_SRC_BAKE_FILE_H
#define MAUL_AUDIO_SRC_BAKE_FILE_H

#include "probe_bake.h"
#include "probe_graph.h"

#include <stddef.h>
#include <stdint.h>

#define MAUD_BAKE_HEADER 40u

// What a loading spatializer allows: its limits and its reflections'
// field layout (order 0 for none).
typedef struct maudBakeLimits
{
    uint32_t maxProbes;
    uint32_t maxLinks;
    uint32_t fieldOrder;
    uint32_t fieldBins;
} maudBakeLimits;

// The size of a set's file; with a bake whose fields are of the given
// order and bins (order 0 for none). 0 if it would not fit in size_t.
size_t maudBakeFileBytes(const maudProbeGraph* graph, const maudProbeBake* bake,
                         uint32_t fieldOrder, uint32_t fieldBins);

// Writes the file into out, maudBakeFileBytes long; bake NULL or empty
// for probes and links alone.
void maudWriteBakeFile(const maudProbeGraph* graph, const maudProbeBake* bake, uint32_t fieldOrder,
                       uint32_t fieldBins, uint8_t* out);

// Reads a file into a graph and a bake (empty for probes and links
// alone): maud_errorInvalid for a file not well formed;
// maud_errorUnsupported for another version or fields that do not match
// the limits' layout; maud_errorCapacity past the limits or when memory
// runs out. Nothing is allocated on failure.
maudResult maudReadBakeFile(const uint8_t* data, size_t size, const maudBakeLimits* limits,
                            const maudAllocator* allocator, maudProbeGraph* graph,
                            maudProbeBake* bake);

#endif // MAUL_AUDIO_SRC_BAKE_FILE_H
