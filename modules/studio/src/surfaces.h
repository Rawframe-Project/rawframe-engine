#pragma once

// Studio's surfaces in one table (SPEC-0051's conformance, D873): each one
// built, by its name, its kind (Part 1), its place (Part 2), and the
// headless path it stands on (Part 5), "none needed" where the contract
// says so. Studio names its docks' panels and its dialogs through
// `placed`, so one the table does not place there does not compile, and
// the repository check (tools/check_repo.py) refuses a kind not in Part 1,
// a budget passed, or a headless path left empty. A surface not yet where
// the contract puts it says where it goes in `until`.

#include <array>
#include <cstdint>
#include <string_view>

namespace rawframe::studio {

enum class SurfaceKind : std::uint8_t {
    Frame,
    Command,
    Panel,
    DocumentEditor,
    ViewportMode,
    Overlay,
    Play,
    Dialog,
    Service,
    ExternalTool
};

struct Surface {
    std::string_view name;
    SurfaceKind kind = SurfaceKind::Frame;
    std::string_view place;
    std::string_view headless;
    std::string_view until;
};

inline constexpr auto kSurfaces = std::to_array<Surface>({
    // The frame.
    {"Docks and saved layouts", SurfaceKind::Frame, "Window menu", "none needed: presentation", ""},
    {"Viewport", SurfaceKind::Frame, "Center", "the scene document; authoring.preview", ""},
    {"Multiple selection", SurfaceKind::Frame, "Everywhere", "an atomic batch", ""},
    // Commands, the toolbar's among them.
    {"Command palette", SurfaceKind::Command, "Menu bar", "the operations authoring.describe lists", ""},
    {"Cook", SurfaceKind::Command, "Toolbar", "authoring.cook; rawframe-cook", ""},
    {"Play", SurfaceKind::Command, "Toolbar", "rawframe-play, or the server and client commands", ""},
    {"Undo", SurfaceKind::Command, "Toolbar", "authoring.undo", ""},
    {"Redo", SurfaceKind::Command, "Toolbar", "authoring.redo", ""},
    // Panels: the left dock, the right, and the bottom.
    {"Hierarchy", SurfaceKind::Panel, "Left", "scene.list_entities; scene.find_entities", ""},
    {"Layers", SurfaceKind::Panel, "Left", "tag fields in the scene", ""},
    {"World", SurfaceKind::Panel, "Left", "scene.stream_instance", ""},
    {"Inspector", SurfaceKind::Panel, "Right", "scene.set_field and the schema's hints", ""},
    {"Assets", SurfaceKind::Panel, "Bottom", "content listing; authoring.import; the cook", ""},
    {"History", SurfaceKind::Panel, "Bottom", "authoring.history, undo, redo", ""},
    {"Console", SurfaceKind::Panel, "Bottom", "NDJSON logs", ""},
    {"Network", SurfaceKind::Panel, "Bottom", "server and client measurement records", ""},
    {"Environment", SurfaceKind::Panel, "Right", "environment components in the scene", "the right dock's Scene settings"},
    {"Profiler", SurfaceKind::Panel, "Bottom", "diagnostic span records; authoring.profile", ""},
    {"Frame", SurfaceKind::Panel, "Bottom", "authoring.preview's frame description; the capture file", ""},
    {"Changes", SurfaceKind::Panel, "Bottom", "git", ""},
    {"Extension panels", SurfaceKind::Panel, "Bottom", "authoring.extension_panel", ""},
    // Document editors in the center's tabs.
    {"Scene", SurfaceKind::DocumentEditor, "Center", "the scene document", ""},
    {"Project settings", SurfaceKind::DocumentEditor, "Center", "the .game text", ""},
    {"Data", SurfaceKind::DocumentEditor, "Center", "the data document", ""},
    {"Style sheets", SurfaceKind::DocumentEditor, "Center", "the style document", ""},
    {"Visual UI editing", SurfaceKind::DocumentEditor, "Center", "the UI document", ""},
    {"Material graphs", SurfaceKind::DocumentEditor, "Center", "the material document", ""},
    {"Graphs", SurfaceKind::DocumentEditor, "Center", "the graph document", ""},
    {"Frame capture", SurfaceKind::DocumentEditor, "Center", "the capture file", ""},
    {"Sequences", SurfaceKind::DocumentEditor, "Center", "the sequence document", ""},
    // Viewport modes, in the mode palette on the center's left edge (D910).
    {"Transform", SurfaceKind::ViewportMode, "Mode palette", "scene.set_field", ""},
    {"Blockout", SurfaceKind::ViewportMode, "Mode palette", "the blockout document", ""},
    {"Terrain", SurfaceKind::ViewportMode, "Mode palette", "terrain.brush; the terrain document", ""},
    {"Tiles", SurfaceKind::ViewportMode, "Mode palette", "tilemap.paint; the tilemap document", ""},
    {"Scatter", SurfaceKind::ViewportMode, "Mode palette", "the scatter document", ""},
    // Overlays over the viewport.
    {"View modes", SurfaceKind::Overlay, "View menu", "none needed: presentation", ""},
    {"2D editing", SurfaceKind::Overlay, "View menu", "the same scene document", ""},
    {"Per-kind visualizers", SurfaceKind::Overlay, "Show menu", "none needed: presentation", ""},
    {"Split views", SurfaceKind::Overlay, "Viewport menu", "none needed: presentation", ""},
    // Play.
    {"Play inside Studio", SurfaceKind::Play, "Center", "rawframe-play, or the server and client commands", ""},
    {"Server and clients", SurfaceKind::Play, "Toolbar", "the play settings", ""},
    {"Pause and step", SurfaceKind::Play, "Toolbar", "tooling endpoint commands", ""},
    // Dialogs.
    {"Start", SurfaceKind::Dialog, "Start", "template folders and a new-project command", ""},
    {"Preferences", SurfaceKind::Dialog, "Window menu", "a preferences file", ""},
    {"Export", SurfaceKind::Dialog, "File menu", "rawframe-export", ""},
    // Services and external tools.
    {"Reloading", SurfaceKind::Service, "Status bar", "the atomic reload", ""},
    {"MCP bridge", SurfaceKind::Service, "Title bar", "rawframe-author mcp", ""},
    {"Real-time collaboration",
     SurfaceKind::Service,
     "Title bar",
     "rawframe-author host, join, and mcp --join; authoring.present, claim, and presence",
     ""},
    {"Extensions", SurfaceKind::Service, "Preferences", "authoring.extensions and authoring.extension", ""},
    {"Accessibility", SurfaceKind::Service, "Everywhere", "none needed", ""},
    {"Code editor", SurfaceKind::ExternalTool, "Open in", "the Kest language server; files", ""},
    {"Script debugger", SurfaceKind::ExternalTool, "VS Code", "rawframe-debug", ""},
    {"Version control", SurfaceKind::ExternalTool, "Status bar", "git", ""},
});

/// The surface named `name` of `kind` placed in `place`, or nothing.
[[nodiscard]] constexpr const Surface*
placedSurface(std::string_view name, SurfaceKind kind, std::string_view place) noexcept {
    for (const Surface& kSurface : kSurfaces) {
        if (kSurface.name == name && kSurface.kind == kind && kSurface.place == place) {
            return &kSurface;
        }
    }
    return nullptr;
}

/// Reached only where `placed` finds no surface, so the name does not
/// compile.
inline void surfaceNotInTheTable() noexcept {
}

/// `name`, where the table places a surface of `kind` by it in `place`;
/// anything else does not compile.
consteval std::string_view placed(std::string_view name, SurfaceKind kind, std::string_view place) {
    if (placedSurface(name, kind, place) == nullptr) {
        surfaceNotInTheTable();
    }
    return name;
}

} // namespace rawframe::studio
