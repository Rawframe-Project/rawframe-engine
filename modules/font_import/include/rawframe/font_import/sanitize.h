#pragma once

// Font import (ADR-0049, ADR-0078, D385): a TrueType or OpenType font or
// collection, from anyone, rebuilt by the OpenType Sanitizer before any of
// it may reach the runtime's FreeType and HarfBuzz: every table OTS knows is
// parsed, checked, and written afresh, and every table it does not know is
// dropped, never passed through. Import tooling only; no client or server
// closure may depend on it.

#include "rawframe/result/result.h"

#include <cstddef>
#include <span>
#include <vector>

namespace rawframe::font_import {

struct SanitizeLimits {
    /// The largest source taken, and the largest font written.
    std::size_t maximumBytes = std::size_t{32} << 20U;
};

/// The font rebuilt: a collection stays a collection, every face rebuilt.
/// Refuses (`Unsupported`) WOFF and WOFF 2.0, (`OverLimit`) a source or a
/// rebuilt font past the limit, and (`BadFont`) anything else that is not a
/// font OTS can rebuild, naming what it found first. The same bytes always
/// give the same font.
[[nodiscard]] result::Result<std::vector<std::byte>> sanitize(std::span<const std::byte> source,
                                                              const SanitizeLimits& limits = {});

} // namespace rawframe::font_import
