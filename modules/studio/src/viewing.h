#pragma once

// What Studio tells a scene's preview to show (D791 to D797): kept apart
// from the shell, as the preview is told it again whenever one is attached.

#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::studio {

struct PreviewShown {
    /// The layers the preview leaves out (D791), and how many times a
    /// preview was told.
    std::vector<std::uint32_t> hiddenLayers;
    std::uint64_t layersHidden = 0;
    /// How the preview draws, by the renderer's name (D794), and how many
    /// times a preview took it.
    std::string viewMode = "lit";
    std::uint64_t modesShown = 0;
    /// Bookmarks kept and gone to (D796).
    std::uint64_t bookmarksKept = 0;
    std::uint64_t bookmarksVisited = 0;
    /// How many views the preview shows, and how many times a preview
    /// split so (D797).
    std::uint8_t views = 1;
    std::uint64_t splitsShown = 0;
    /// The overlays the preview draws, by name, and how many times a
    /// preview took them (D798).
    std::vector<std::string> overlays;
    std::uint64_t overlaysShown = 0;
};

} // namespace rawframe::studio
