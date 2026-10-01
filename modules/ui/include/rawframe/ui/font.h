#pragma once

// A cooked font (ADR-0049, ADR-0078, D385): a TrueType or OpenType font or
// collection the cook's sanitizer rebuilt, never an author's raw bytes, and
// read by a tree's `addFont`.

#include "rawframe/base/bits128.h"

#include <string_view>

namespace rawframe::ui {

inline constexpr base::Bits128 kFontType = base::parseBits128Hex("51efbae405c153ba13e8af579ad96b5a").value;
inline constexpr std::string_view kFontRepresentation = "rawframe.font";

} // namespace rawframe::ui
