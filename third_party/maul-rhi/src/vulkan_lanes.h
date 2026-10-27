// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's lanes and batches on Vulkan (vulkan_lanes.c). Each pass class
// records in a lane (vulkan_frame.h). A lane's passes go in the frame's
// order into batches, a new one where a pass follows another lane's pass
// and after a pass another lane's follows, and each batch waits for the
// lanes' batches its first pass follows. With more than one lane, a
// frame's first batch resets its query sets and every lane's first batch
// waits for it, and its last batch waits for every lane before the
// frame's end barriers. With one lane, a frame is one batch.

#ifndef MAUL_RHI_SRC_VULKAN_LANES_H
#define MAUL_RHI_SRC_VULKAN_LANES_H

#include "vulkan_pass.h"

// Begins a frame: marks the passes another lane follows, and opens its
// first batch with the query sets' resets.
void mrhiVulkanBeginLanes(mrhiVulkanRecording* recording);

// Before a pass records: makes its lane's batch the one recording, a new
// one where it follows another lane's pass.
void mrhiVulkanEnterPass(mrhiVulkanRecording* recording);

// After a pass recorded: ends its batch when another lane's pass follows
// it.
void mrhiVulkanLeavePass(mrhiVulkanRecording* recording);

// Ends the passes' batches and makes the frame's last batch the one
// recording, for its end barriers: with more than one lane, a new batch
// on the first lane after every lane's last.
void mrhiVulkanEndLanes(mrhiVulkanRecording* recording);

// Ends the frame's last batch.
void mrhiVulkanCloseLanes(mrhiVulkanRecording* recording);

// Places a native pass's command buffer (NULL for none) in its batch
// after the part recorded so far, and begins the batch's next part:
// false when the device cannot.
bool mrhiVulkanNextPart(mrhiVulkanRecording* recording, void* native);

#endif // MAUL_RHI_SRC_VULKAN_LANES_H
