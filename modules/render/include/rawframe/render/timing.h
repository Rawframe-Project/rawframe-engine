#pragma once

// A pass of a frame timed on the GPU: the one way a recorder,
// the scene's or the canvas's, names a pass it adds to the frame's
// timestamps.

#include "rawframe/render/frame.h"

#include <string_view>

// Maul RHI's own; only the rendering cluster includes its headers and so
// can use one.
struct mrhiPassDef;

namespace rawframe::render {

/// `def`'s pass, of the graphics class, timed in `frame` as `pass`: its
/// start and end written to the frame's timestamps where the frame times
/// its passes; nothing where it does not, or has timed its most.
void timePass(Frame& frame, mrhiPassDef& def, std::string_view pass);

} // namespace rawframe::render
