#pragma once

// How a present policy becomes the present mode a surface is configured
// with: the first mode of the policy's chain the surface offers.

#include "rawframe/render/device.h"

#include <maul-rhi/surface.h>

namespace rawframe::render {

/// The mode `policy` presents by on a surface offering `offered`, a set of
/// mode bits: the first of its chain offered, fifo when none is, since
/// every surface offers fifo.
[[nodiscard]] mrhiPresentMode presentModeFor(PresentPolicy policy, mrhiPresentModes offered) noexcept;

/// The policy a surface configured with `mode` presents by.
[[nodiscard]] PresentPolicy policyOf(mrhiPresentMode mode) noexcept;

} // namespace rawframe::render
