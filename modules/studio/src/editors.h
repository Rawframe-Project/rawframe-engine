#pragma once

// What Studio's document editors keep between frames (D812, D818, D819,
// D827): the kinds of document listed with the scenes, and what the data,
// settings, graph, clip, and UI editors last read and laid out.

#include "rawframe/diagnostics/identity.h"
#include "rawframe/document/json.h"
#include "rawframe/ui/tree.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::studio {

/// A graph laid out on the canvas (D819), a clip on its timeline (D827).
inline constexpr diagnostics::EventIdentity kCanvasShown{"studio", "studio_canvas"};
inline constexpr diagnostics::EventIdentity kBlockoutShown{"studio", "studio_blockout"};
inline constexpr diagnostics::EventIdentity kPlanShown{"studio", "studio_plan"};
inline constexpr diagnostics::EventIdentity kTimelineShown{"studio", "studio_timeline"};
inline constexpr diagnostics::EventIdentity kCurveShown{"studio", "studio_curve"};
/// A style sheet's classes as each variant looks (D837).
inline constexpr diagnostics::EventIdentity kStylePreviewShown{"studio", "studio_style_preview"};
/// A UI document laid out as its game would show it (D832).
inline constexpr diagnostics::EventIdentity kUiPreviewShown{"studio", "studio_ui_preview"};
/// The played game's server's profile read (D838).
inline constexpr diagnostics::EventIdentity kProfileRead{"studio", "studio_profile"};
/// The Frame tab read the preview's last frame, or kept a capture (D842).
inline constexpr diagnostics::EventIdentity kFrameRead{"studio", "studio_frame"};
/// A capture kept (D842, D844), and one shown, read only (D843).
inline constexpr diagnostics::EventIdentity kCaptureKept{"studio", "studio_capture_kept"};
inline constexpr diagnostics::EventIdentity kCaptureShown{"studio", "studio_capture_shown"};
/// The environment panel's components and their entities (D834).
inline constexpr diagnostics::EventIdentity kEnvironmentShown{"studio", "studio_environment"};

/// What each document listed with the scenes is (D637, D643, D722, D730,
/// D812, D818).
enum class Listed : std::uint8_t {
    Scene,
    Sheet,
    Ui,
    Material,
    Graph,
    Data,
    Settings,
    Capture,
    Blockout,
};

struct EditorsHeld {
    /// The most rows a data document or the settings list.
    static constexpr std::size_t kMostRows = 512;
    /// The chosen blockout's lines as last read (D850).
    std::vector<std::string> blockoutLines;
    /// What each entity row of a data document holds, as last read (D812).
    std::vector<document::Value> data;
    /// The game's description's lines as last read, and each settings row's
    /// lines by their places (D818).
    std::vector<std::string> settingsLines;
    std::vector<std::vector<std::size_t>> settings;
    /// The canvas's or the timeline's parts and each one's row, and its
    /// nodes' or keys' boxes (D819, D827).
    std::vector<std::pair<ui::Node, std::size_t>> canvasParts;
    std::vector<std::pair<ui::Node, std::size_t>> canvasBoxes;
    /// A UI document's preview: each node's box and its row, and the frame
    /// laid over the chosen one (D832).
    std::vector<std::pair<ui::Node, std::size_t>> previewBoxes;
    std::optional<ui::Node> previewFrame;
    /// The game's classes made in Studio's tree for the preview.
    std::vector<ui::Style> previewStyles;
    /// The environment panel: its pane, its rows, each entity's button and
    /// row, and each component it offers to add, by its identity (D834).
    ui::Node environmentColumn{};
    std::vector<ui::Node> environmentRows;
    std::vector<std::pair<ui::Node, std::size_t>> environmentParts;
    std::vector<std::pair<ui::Node, std::string>> environmentAdds;
    /// The Profiler: its pane, its rows, when it next reads, and how many
    /// reads it has shown (D838).
    ui::Node profilerColumn{};
    std::vector<ui::Node> profilerRows;
    double profilerNextRead = 0;
    std::uint64_t profilerReads = 0;
    /// The Profiler's Save capture button, and the captures it has kept
    /// (D844).
    ui::Node profilerCapture{};
    std::uint64_t profilerCaptures = 0;
    /// The Frame tab: its tab and pane, its Capture button, its rows, when
    /// it next reads, how many reads it has shown, and the captures it has
    /// kept (D842).
    ui::Node frameTab{};
    ui::Node frameColumn{};
    ui::Node frameCapture{};
    std::vector<ui::Node> frameRows;
    double frameNextRead = 0;
    std::uint64_t frameReads = 0;
    std::uint64_t frameCaptures = 0;
    /// The capture shown, as its file holds it (D843).
    document::Value capture;
};

} // namespace rawframe::studio
