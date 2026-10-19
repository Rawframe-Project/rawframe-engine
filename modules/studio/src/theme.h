#pragma once

// Studio's look (D915): the screen design's palette (artifact
// "Rawframe Studio Screens", its Language board), 0xRRGGBBAA, and the
// sizes its frame is drawn at, as `look::`. Every panel takes its colors
// from here.

#include "icons.h"

#include <cstdint>

namespace rawframe::studio::look {

/// The title and menu row, the deepest; the window behind the docks; the
/// toolbar and the status bar; a dock's panel; a raised surface, as a
/// button or a card's picture; a field's well.
inline constexpr std::uint32_t kTitleFill = 0x0E0F12FFU;
inline constexpr std::uint32_t kWindowFill = 0x121317FFU;
inline constexpr std::uint32_t kBarFill = 0x17181CFFU;
inline constexpr std::uint32_t kPanelFill = 0x1A1B20FFU;
inline constexpr std::uint32_t kRaisedFill = 0x26282FFFU;
inline constexpr std::uint32_t kWellFill = 0x111216FFU;
/// Lines between parts, and a raised surface's edge.
inline constexpr std::uint32_t kLine = 0x22242AFFU;
inline constexpr std::uint32_t kEdge = 0x2B2D34FFU;
inline constexpr std::uint32_t kRaisedEdge = 0x33353DFFU;
/// The accent, what is chosen, and its edge.
inline constexpr std::uint32_t kAccent = 0x5B9BFFFFU;
inline constexpr std::uint32_t kOnAccent = 0x0B1020FFU;
inline constexpr std::uint32_t kSelected = 0x24375CFFU;
inline constexpr std::uint32_t kSelectedEdge = 0x35507FFFU;
inline constexpr std::uint32_t kHovered = 0x23252BFFU;
/// Words: the main, a row's, a quiet label's, a faint hint's, an
/// unavailable control's.
inline constexpr std::uint32_t kInk = 0xE3E4E8FFU;
inline constexpr std::uint32_t kRowInk = 0xD3D5DAFFU;
inline constexpr std::uint32_t kSoftInk = 0xC3C5CCFFU;
inline constexpr std::uint32_t kLabelInk = 0xA3A6AEFFU;
inline constexpr std::uint32_t kFaintInk = 0x8A8D96FFU;
inline constexpr std::uint32_t kOffInk = 0x5D6068FFU;
/// States: ready, a warning, an error; the axes X, Y, Z; an extension's.
inline constexpr std::uint32_t kReady = 0x3FB950FFU;
inline constexpr std::uint32_t kWarn = 0xF0A33AFFU;
inline constexpr std::uint32_t kError = 0xFF6B6BFFU;
inline constexpr std::uint32_t kAxisX = 0xFF6262FFU;
inline constexpr std::uint32_t kAxisY = 0x62D26FFFU;
inline constexpr std::uint32_t kAxisZ = 0x5B8CFFFFU;
inline constexpr std::uint32_t kExtension = 0x2DD4BFFFU;
/// An entity's icon by what it is: a plain one, a prefab's instance.
inline constexpr std::uint32_t kEntityInk = 0x7AA9FFFFU;
inline constexpr std::uint32_t kInstanceInk = 0xB48CFFFFU;

/// Sizes in pixels: the title row, the toolbar, a dock's tab row, a row,
/// a field, the status bar; the docks' widths.
inline constexpr float kTitleHeight = 34;
inline constexpr float kToolbarHeight = 40;
inline constexpr float kTabHeight = 32;
inline constexpr float kRowHeight = 24;
inline constexpr float kFieldHeight = 24;
inline constexpr float kStatusHeight = 24;
inline constexpr float kLeftDockWidth = 272;
inline constexpr float kRightDockWidth = 340;
/// Type sizes: body, a label's, a heading's.
inline constexpr float kBodySize = 13;
inline constexpr float kLabelSize = 12;
inline constexpr float kSmallSize = 11;

/// The image key an icon is drawn under: Studio hands each from its
/// embedded set (D915), tinted to the color it is shown in.
inline constexpr std::uint64_t kIconImageBase = 0x49434F4E00000000ULL;
[[nodiscard]] constexpr std::uint64_t iconImage(studio::Icon icon) noexcept {
    return kIconImageBase + static_cast<std::uint64_t>(icon);
}

} // namespace rawframe::studio::look
