#pragma once

// What Studio's document editors keep between frames (D812, D818, D819,
// D827): the kinds of document listed with the scenes, and what the data,
// settings, graph, clip, and UI editors last read and laid out.

#include "icons.h"
#include "rawframe/authoring_session/link.h"
#include "rawframe/diagnostics/identity.h"
#include "rawframe/document/json.h"
#include "rawframe/process/child.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::studio {

/// A graph laid out on the canvas (D819), a clip on its timeline (D827).
inline constexpr diagnostics::EventIdentity kCanvasShown{"studio", "studio_canvas"};
inline constexpr diagnostics::EventIdentity kBlockoutShown{"studio", "studio_blockout"};
inline constexpr diagnostics::EventIdentity kPlanShown{"studio", "studio_plan"};
inline constexpr diagnostics::EventIdentity kTerrainShown{"studio", "studio_terrain"};
inline constexpr diagnostics::EventIdentity kTilemapShown{"studio", "studio_tilemap"};
inline constexpr diagnostics::EventIdentity kScatterShown{"studio", "studio_scatter"};
/// A viewport mode chosen in the mode palette (D910): which, and the
/// document it shows.
inline constexpr diagnostics::EventIdentity kModeChosen{"studio", "studio_mode"};
inline constexpr diagnostics::EventIdentity kSequenceShown{"studio", "studio_sequence"};
/// Studio joined to a shared session's host, and what it heard there
/// unasked (D882).
inline constexpr diagnostics::EventIdentity kJoined{"studio", "studio_joined"};
inline constexpr diagnostics::EventIdentity kHostHeard{"studio", "studio_host_heard"};
/// The collaborators shown: in the title bar, and their choices and claims
/// in the Hierarchy (D884).
inline constexpr diagnostics::EventIdentity kPresenceShown{"studio", "studio_presence_shown"};

/// Another author at the host, as presence says: the document they have
/// open, what they have chosen in it, and what they hold, each a document
/// and an entity or UI node in it, or the whole of it.
struct Collaborating {
    std::string author;
    std::string document;
    std::vector<std::string> chosen;
    std::vector<std::pair<std::string, std::string>> claims;
};
/// A scene's instances listed as districts (D870).
inline constexpr diagnostics::EventIdentity kWorldShown{"studio", "studio_world"};
/// Version control's changes read, and a commit made (D871).
inline constexpr diagnostics::EventIdentity kChangesShown{"studio", "studio_changes"};
inline constexpr diagnostics::EventIdentity kCommitted{"studio", "studio_committed"};
/// An extension's Inspector section shown (D875).
inline constexpr diagnostics::EventIdentity kSectionShown{"studio", "studio_section_shown"};
/// The extensions' panels shown (D878).
inline constexpr diagnostics::EventIdentity kPanelsShown{"studio", "studio_panels_shown"};
/// A game exported, or its export failed (D872).
inline constexpr diagnostics::EventIdentity kExported{"studio", "studio_exported"};
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
    Terrain,
    Tilemap,
    Scatter,
    Sequence,
};

/// What kind of button the frame draws (D915), as the design's: an icon
/// alone, a raised one, the accent's, a quiet one, a tab.
enum class ButtonKind : std::uint8_t {
    Icon,
    Normal,
    Primary,
    Quiet,
    Tab
};

/// A drawn button's parts, so it is lit or relabeled in place: its icon
/// and which, its label, its kind, and its words.
struct ButtonParts {
    ui::Node icon{};
    std::optional<Icon> which;
    ui::Node label{};
    ButtonKind kind = ButtonKind::Normal;
    std::string text;
};

struct EditorsHeld {
    /// The most rows a data document or the settings list.
    static constexpr std::size_t kMostRows = 512;
    /// Whether the edit view's grid was put on once (D914), after which
    /// it is the author's to toggle.
    bool gridShown = false;
    /// Each drawn button's parts by its node's key (D915); the toolbar's
    /// buttons that run a command by its name.
    std::map<std::uint64_t, ButtonParts> buttonParts;
    std::vector<std::pair<ui::Node, std::string>> commandButtons;
    /// Where the title row puts the palette's field; the toolbar's tools,
    /// World, snaps, 3D and 2D, players, and the status bar's figures.
    ui::Node searchAt{};
    std::array<ui::Node, 4> tools{};
    std::uint8_t tool = 1;
    ui::Node worldButton{};
    bool local = false;
    ui::Node moveSnap{};
    ui::Node turnSnap{};
    ui::Node threeD{};
    ui::Node twoD{};
    ui::Node playersButton{};
    ui::Node statusFigures{};
    /// What the status bar's figures last said, its errors' and warnings'
    /// buttons, and when they are next counted (D923).
    std::string figuresSaid;
    std::vector<ui::Node> figureButtons;
    double nextFigures = 0;
    /// What the frame does not draw but the commands reach (D915); the
    /// right dock and its tabs.
    ui::Node shelf{};
    ui::Node rightDock{};
    ui::Node inspectorTab{};
    ui::Node sceneSettingsTab{};
    ui::Node titleGame{};
    ui::Node titleDocument{};
    /// The chosen entity's turn as the Inspector shows it, in degrees, and
    /// the pose it is of (D915).
    std::array<double, 3> turn{};
    std::string turnComponent;
    /// The chosen blockout's lines as last read (D850), the pixels a meter
    /// is drawn in its plan (D851), and whether the last press was on a
    /// footprint there, which a drag moves (D852).
    std::vector<std::string> blockoutLines;
    float planScale = 1;
    bool planPressed = false;
    /// The pixels a second takes on the clip's timeline shown, nought where
    /// none is: a mark pressed there is a press on the plan too, and a drag
    /// moves its key, event, or marker in time (D890).
    float timelineScale = 0;
    /// The chosen terrain's map, each block and the place in meters a
    /// stroke there brushes, and the brush (D856).
    std::vector<std::pair<ui::Node, std::array<double, 2>>> mapBlocks;
    std::string brushTool = "raise";
    double brushRadius = 2;
    double brushAmount = 1;
    std::size_t strokes = 0;
    /// The chosen tilemap's grid, each block and the cells a paint there
    /// covers (column, row, width, height), its rows' tiles (-1 none), and
    /// the tile painted (D861).
    std::vector<std::pair<ui::Node, std::array<std::uint32_t, 4>>> tileBlocks;
    std::vector<int> tileRows;
    int tilePaint = 0;
    std::size_t tilesPainted = 0;
    /// The chosen scatter's lines as last read (D866).
    std::vector<std::string> scatterLines;
    /// The chosen sequence's lines as last read (D869).
    std::vector<std::string> sequenceLines;
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
    /// Its list of the scene's environment, and below it the viewport's
    /// camera as numbers (D925).
    ui::Node environmentList{};
    ui::Node viewCamera{};
    std::vector<ui::Node> cameraRows;
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
    /// The Changes tab (D871): its tab and pane, its rows, each changed
    /// file's press with its path, and git as Studio runs it: the one
    /// running, what it is run for, those waiting their turn with their
    /// purposes, and the file its output goes to; then what git last said:
    /// the branch, the game's directory from the repository's top, the
    /// changes, the diff shown and of what, and the commits made.
    ui::Node changesTab{};
    ui::Node changesColumn{};
    /// The status bar's branch.
    ui::Node branchNode{};
    /// Whether the author trusts the game's repository, so git runs in it,
    /// and the press that says so.
    bool gitTrusted = false;
    ui::Node trustPress{};
    std::vector<ui::Node> changesRows;
    std::vector<std::pair<ui::Node, std::string>> changedPresses;
    std::optional<process::Child> git;
    std::string gitFor;
    std::deque<std::pair<std::string, std::vector<std::string>>> gitWaiting;
    std::filesystem::path gitOutput;
    std::string branch;
    std::string gamePrefix;
    std::string changes;
    std::string diff;
    std::string diffOf;
    std::uint64_t commits = 0;
    /// Studio's own directory, made afresh for the output of the tools it
    /// runs, taken away as it stops (D871, D872).
    std::filesystem::path own;
    /// The Export dialog (D872): its node, its close and export presses,
    /// each target's press by its word, and its fields; what the author
    /// chose, an output, title, or address empty for its default; the
    /// export running, the output it writes to, what it said last, and the
    /// exports made.
    std::optional<ui::Node> exportDialog;
    ui::Node exportClose{};
    ui::Node exportPress{};
    std::vector<std::pair<ui::Node, std::string>> exportTargets;
    std::vector<std::pair<ui::Node, std::string>> exportFields;
    std::string exportTarget = "desktop";
    std::string exportOutput;
    std::string exportTitle;
    std::string exportAddress;
    std::optional<process::Child> exporting;
    /// The extension's viewport mode on, by its extension's path and its
    /// name, none for none (D877).
    std::optional<std::pair<std::string, std::string>> extensionMode;
    /// The mode palette on the center's left edge (D910): its column, each
    /// mode's button by its name, the scene a content mode was chosen
    /// from, which Transform goes back to, and how many modes were chosen.
    ui::Node modePalette{};
    /// The viewport's own controls over it (D916): the bar of pills, each
    /// pill and the menu it opens, the axes' disc and its three ends, and
    /// the flying hint.
    ui::Node viewBar{};
    ui::Node splitBar{};
    std::vector<std::pair<ui::Node, std::string>> viewPills;
    ui::Node axisGizmo{};
    std::array<ui::Node, 3> axisEnds{};
    ui::Node flyHint{};
    /// The viewport's note while it has no picture, and its words (D922),
    /// and whether the edited game stopped of itself.
    ui::Node viewportNote{};
    std::string viewportNoteText;
    bool editStopped = false;
    /// The settings page's section headings, each its section (D922).
    std::vector<std::pair<ui::Node, std::size_t>> settingsHeadings;
    /// The Inspector's empty state's buttons, each its New command (D922).
    std::vector<std::pair<ui::Node, std::string>> emptyButtons;
    /// The empty scene's first steps over the viewport (D923).
    std::vector<std::pair<ui::Node, std::string>> sceneStartButtons;
    /// The Preferences dialog's pages, the one shown, and its close (D923).
    std::vector<std::pair<ui::Node, std::size_t>> preferencePages;
    std::size_t preferencePage = 0;
    ui::Node preferencesX{};
    /// Whether the right dock shows the Scene settings tab (D921).
    bool sceneSettingsShown = false;
    /// The Hierarchy's plus, which opens the New menu (D917).
    ui::Node newMenu{};
    /// Whether the view was last turned to 2D.
    bool flat = false;
    std::vector<std::pair<ui::Node, std::string>> modeButtons;
    std::string modeReturn;
    std::uint64_t modesChosen = 0;
    /// The toolbar's layout chooser, and how many layouts were put in
    /// place (D911).
    ui::Node layoutChooser{};
    std::uint64_t layoutsChosen = 0;
    /// The Extension panels tab, its pane, and its rows (D878).
    ui::Node panelsTab{};
    ui::Node panelsColumn{};
    std::vector<ui::Node> panelsRows;
    std::filesystem::path exportingTo;
    std::string exportSaid;
    std::uint64_t exports = 0;
    /// The host Studio joins in place of a session of its own, where
    /// `studio.join` names one (D882): where, and the files of its pin and
    /// token; the link once welcomed, whom it welcomed, what Studio last
    /// said it has open and chosen there, and whether it said the host
    /// closed.
    std::string joinEndpoint;
    std::string joinPin;
    std::string joinToken;
    std::unique_ptr<authoring_session::JoinedSession> joined;
    std::string welcome;
    std::string joinedAs;
    std::string presented;
    /// Everyone presence names, Studio's own author among them, the title
    /// bar's place for the others, and their marks there (D884).
    std::vector<Collaborating> collaborators;
    ui::Node avatarsNode{};
    std::vector<ui::Node> avatars;
    bool saidJoined = false;
    bool toldClosed = false;
};

} // namespace rawframe::studio
