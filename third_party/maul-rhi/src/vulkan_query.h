// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Queries on Vulkan (mrhi-0003, mrhi-0012): each query set a frame
// names reset once at its start, occlusion queries around draws, a
// pass's timestamps at its start and end, and resolves that copy the
// queries the frame wrote and write 0 for the others.

#ifndef MAUL_RHI_SRC_VULKAN_QUERY_H
#define MAUL_RHI_SRC_VULKAN_QUERY_H

#include "vulkan_pass.h"

// Resets every query set the frame names, before its first pass.
void mrhiVulkanResetQueries(const mrhiVulkanRecording* recording);

// Writes the pass's timestamp at its start or its end, if it has one.
void mrhiVulkanPassTimestamp(const mrhiVulkanRecording* recording, bool end);

// Records an occlusion query's start or end, or a resolve.
void mrhiVulkanQuery(mrhiVulkanRecording* recording, const mrhiCommand* command);

#endif // MAUL_RHI_SRC_VULKAN_QUERY_H
