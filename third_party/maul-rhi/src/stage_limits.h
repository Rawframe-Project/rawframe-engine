// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The resources one shader stage binds, fitted under a platform's bound
// on all of them together (Vulkan's maxPerStageResources), which the
// contract has no limit for: its per-kind limits (sampled textures,
// samplers, storage buffers, storage textures, uniform buffers) are
// what a pipeline may use at once, so their sum must fit.

#ifndef MAUL_RHI_SRC_STAGE_LIMITS_H
#define MAUL_RHI_SRC_STAGE_LIMITS_H

#include "maul-rhi/capabilities.h"

// Brings the per-kind limits above a common cap down to it, the cap the
// highest whose limits sum to at most total, never below the contract's
// floor (mrhiDefaultLimits); limits that already fit are kept. The
// floor's own sum (56) fits any total of at least that.
void mrhiFitStageLimits(mrhiLimits* limits, uint32_t total);

#endif // MAUL_RHI_SRC_STAGE_LIMITS_H
