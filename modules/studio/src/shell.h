#pragma once

// Studio's shell (D435): a participant holding an authoring session on a
// game in its own process and showing it as a UI of its own. What its
// parts share: Studio's colors, and the shell itself (the rows and buttons
// it acts on are rows.h's), whose lifecycle is participant.cpp's, its panels
// panels.cpp's, its presses, typing, and session records editing.cpp's,
// and the game it plays playing.cpp's.

#include "browser.h"
#include "console.h"
#include "editors.h"
#include "live.h"
#include "network_panel.h"
#include "play.h"
#include "preferences.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/process/child.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/access.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/text_edit.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/dropping.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/typing.h"
#include "rawframe/world_tooling/forwarding.h"
#include "records.h"
#include "rows.h"
#include "viewing.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

inline constexpr diagnostics::EventIdentity kSummary{"studio", "studio_summary"};
inline constexpr diagnostics::EventIdentity kLayout{"studio", "studio_layout"};
inline constexpr diagnostics::EventIdentity kStartScreen{"studio", "studio_start"};
inline constexpr diagnostics::EventIdentity kSelection{"studio", "studio_selection"};
inline constexpr diagnostics::EventIdentity kViewported{"studio", "studio_viewport"};
inline constexpr diagnostics::EventIdentity kBrowsed{"studio", "studio_assets"};
inline constexpr diagnostics::EventIdentity kPlayedRecord{"studio", "studio_played"};
/// The image key the viewport's picture is drawn under (D789).
inline constexpr std::uint64_t kViewportImage = 0x5649455750ULL;
inline constexpr diagnostics::EventIdentity kShown{"studio", "studio_shown"};
/// Studio's window read by assistive technology (D575), or why not.
inline constexpr diagnostics::EventIdentity kAccessible{"studio", "studio_accessibility_ready"};
inline constexpr diagnostics::EventIdentity kInaccessible{"studio", "studio_accessibility_unavailable"};
inline constexpr diagnostics::EventIdentity kPreviewing{"studio", "studio_previewing"};
/// The preview shows where the chosen entity stands (D464), so its handles
/// can be grabbed (D466).
inline constexpr diagnostics::EventIdentity kMarked{"studio", "studio_marked"};
inline constexpr diagnostics::EventIdentity kNetworkRead{"studio", "studio_network"};
/// A part of the mark lit under the pointer, as the session says (D504).
inline constexpr diagnostics::EventIdentity kLit{"studio", "studio_lit"};
/// A game asked to stop has ended, its server and its client both (D504).
inline constexpr diagnostics::EventIdentity kGameEnded{"studio", "studio_game_ended"};
/// A drag in the played game committed to the chosen scene (D457, D463,
/// D466, D467): how, and what it moved.
inline constexpr diagnostics::EventIdentity kDragged{"studio", "studio_dragged"};
/// The view moved in the played game (D469): how far its eye is from its
/// target now, and where each is (D472).
inline constexpr diagnostics::EventIdentity kViewMoved{"studio", "studio_view_moved"};
/// The entity column shows what a search found (D473): how many, and the
/// name and component searched for.
inline constexpr diagnostics::EventIdentity kSearched{"studio", "studio_searched"};
/// Each status the status line shows, as it shows it: what a test waits on
/// before it stops Studio, however slowly frames come (D489).
inline constexpr diagnostics::EventIdentity kSaid{"studio", "studio_said"};

/// Studio's colors, 0xRRGGBBAA: the window behind everything, a panel, a
/// row, and the header.
inline constexpr std::uint32_t kBackground = 0x1E1F24FFU;
inline constexpr std::uint32_t kPanel = 0x2A2C33FFU;
inline constexpr std::uint32_t kRow = 0x3A3D47FFU;
inline constexpr std::uint32_t kChosen = 0x4A6FA5FFU;
inline constexpr std::uint32_t kField = 0x23252BFFU;
inline constexpr std::uint32_t kEditing = 0x1A1C21FFU;
inline constexpr std::uint32_t kHeaderFill = 0x30343FFFU;
inline constexpr std::uint32_t kText = 0xE6E8EEFFU;
inline constexpr std::uint32_t kQuiet = 0x9AA0ADFFU;
/// Words that warn: a chord two commands share (D782).
inline constexpr std::uint32_t kWarning = 0xE8B04AFFU;

/// The shell: a session on the game, and the UI that shows it.
class ShellParticipant final : public composition::Participant, public ui::UiFrames {
public:
    result::Status load(composition::ParticipantContext& context);

    result::Status start(composition::ParticipantContext& context) noexcept override;

    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override;

    void stop() noexcept override;

    composition::CapabilityObject provide(std::string_view capability) noexcept override;

    const ui::DrawList* drawn() const noexcept override;

    void resize(std::uint32_t width, std::uint32_t height, bool panel) noexcept override;

    std::shared_ptr<const texture::Texture> image(std::uint64_t) const override;

private:
    /// A box of `color` as `layout` places it under `parent`, if any.
    result::Result<ui::Node> box(std::optional<ui::Node> parent, const ui::Layout& layout, std::uint32_t color);

    /// Words on `node`, in Studio's font.
    result::Status words(ui::Node node, std::string_view text, std::uint32_t color, float size = 15);

    /// A column under `parent` headed `heading`, its row's width or `width`.
    result::Result<ui::Node> column(ui::Node parent, std::string_view heading, float width = 0);

    /// The played game's newest picture, when it plays in the viewport (D789).
    void followPicture();

    /// The viewport, under the view's fields, showing that picture.
    void showViewport();

    /// The viewport's input and area handed to the game played there (D790).
    void forwardInput();

    /// The left dock's layout, `width` wide (D788).
    static ui::Layout leftLayout(float width);

    /// A left dock column headed `heading`, `tab` showing `other` (D788).
    result::Result<ui::Node> tabbedColumn(ui::Node parent,
                                          std::string_view heading,
                                          std::string_view other,
                                          ui::Node& tab,
                                          std::string_view extra = {},
                                          ui::Node* extraButton = nullptr);

    /// What the left dock shows: the Hierarchy, the Layers (D788), or the
    /// World, the scene's districts (D870).
    enum class Left : std::uint8_t {
        Hierarchy,
        Layers,
        World,
    };
    void showLeft(Left shown);

    /// The World: each instance of the scene, how it reaches clients as a
    /// district, pressed to stream it the next way (D870).
    void showWorld();
    /// A press on the World's rows; whether it was.
    bool pressWorld(ui::Node node);

    /// The Layers: each layer, how many are on it, its choosing and lock (D788).
    void showLayers();

    /// A press on the left dock's tabs or the Layers' rows; whether it was.
    bool pressLayers(ui::Node node);

    /// `layer`'s name, as the game's field line names it, else its number.
    [[nodiscard]] std::string layerName(std::uint32_t layer) const;

    /// Tells the preview of the scene shown the layers hidden (D791), the
    /// view mode (D794), and how many views it shows (D797).
    void tellHidden();
    void tellViewMode();
    void tellSplit();
    /// Tells the preview what it draws over its scene (D798).
    void tellOverlays();
    /// The asset browser (D799): sources asked again, its controls, its rows.
    void listSources();
    result::Status showBrowser();
    result::Status showSources();
    bool pressBrowser(ui::Node node);
    /// The chosen source's import settings, one set, and a recook (D800).
    void showImportSettings();
    /// Imports `path` (absolute) as `as` in the game's directory, saying how (D505).
    bool importFile(const std::string& path, const std::string& as);
    /// Each file dropped on the window imported into its folder (D802).
    void takeDrops();
    /// The chosen source's references in the center, read only (D803).
    void showReferences();
    /// `source`'s thumbnail from the last cook, made once; or none (D801).
    std::shared_ptr<const texture::Texture> thumbnailOf(const ListedSource& source);
    void setImportSetting(const FieldRow& row, const std::string& text);

    /// The locked layer's name `entity` is on, none if on no locked one.
    [[nodiscard]] std::optional<std::string> lockedOf(const std::string& entity) const;

    /// A column's layout, sharing its row's width or `width` pixels wide.
    [[nodiscard]] static ui::Layout columnLayout(float width);

    /// The bottom dock's layout: `height` pixels, or nought for its share.
    [[nodiscard]] static ui::Layout dockLayout(float height);

    /// The docks' edges a pointer drags (D779).
    enum class Edge : std::uint8_t {
        Hierarchy,
        Inspector,
        Dock
    };

    /// A splitter under `parent` for `edge`, across a column for the dock's.
    result::Status splitter(ui::Node parent, Edge edge);

    /// While a splitter is held, its dock follows the pointer.
    void followSplitter();

    /// A splitter let go: the sizes kept in the preferences' file, if any.
    void releaseSplitter();

    /// A dock's pane, which scrolls, named `name` and headed by its tab.
    result::Result<ui::Node> pane(std::string_view name);

    /// A button reading `text` under `parent`.
    result::Result<ui::Node> button(ui::Node parent, std::string_view text);

    /// `text` on `node`, something to press, which it names for assistive
    /// technology (D575).
    result::Status pressWords(ui::Node node, std::string_view text, std::uint32_t color, float size);

    /// The window as SPEC-0051 plans it (D776): the toolbar; the Hierarchy
    /// on the left, the center with the open documents' tabs over the
    /// active one's view, and the Assets and History under both; the
    /// Inspector on the right the body's height; the status bar.
    result::Status build();

    /// The bottom dock's panes, one shown at a time.
    enum class Dock : std::uint8_t {
        Assets,
        History,
        Console,
        Network,
        Environment,
        Profiler,
        Frame
    };
    /// The bottom dock showing `pane`.
    void showDock(Dock pane);

    /// A line added to the Console, the oldest let go past its bound.
    void addConsoleLine(ConsoleLine line);

    /// The played game's logs read as they grow, twice a second (D778).
    void readLogs(double seconds);

    /// The Console's pane, made once: its filters over its list.
    result::Status makeConsole();

    /// The Console's filters lit as they stand, and its lines that pass
    /// them in its list, newest last.
    void showConsole();

    /// The commands the window's buttons do (D777), then a button in `bar`
    /// for each menu that has any, and the palette's field after them.
    result::Status showMenus(ui::Node bar);

    /// Does the command named `name`; false when there is none.
    bool runCommand(std::string_view name);

    /// Does the command `chord` is a shortcut of, if one is.
    void runChord(std::string_view chord);

    /// The menu at `at` in a popup under its button, or closed if it is
    /// the one open.
    void showMenu(std::size_t at);

    /// The commands whose names hold `typed`, in a popup under the
    /// palette's field.
    void showMatches(std::string_view typed);

    /// What the palette's field does with a key it left: Enter does the
    /// first match, Escape closes the matches, and text shows them.
    void paletteTyped(std::optional<view::TypingKey> left);

    /// The palette's field given the keyboard, emptied, its matches all.
    void openPalette();

    /// The palette's field let go, if it held the keyboard, reading its hint.
    void closePalette();

    /// The open menu or matches closed, if any is.
    void closePopup();

    /// A tab for each document opened, the chosen one marked; `opened`
    /// the scene listed at that place, opened now.
    void showTabs(std::optional<std::size_t> opened = std::nullopt);

    /// A row of `text` under `column`, its height fixed; one to press read
    /// as a button (D575).
    result::Result<ui::Node>
    row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill = kRow, bool pressed = false);

    /// Removes `rows` from the tree.
    void clear(std::vector<ui::Node>& rows);
    /// One record to the session, counted; its reply; the next record's id.
    std::string ask(const Value& record);
    std::int64_t next() const noexcept;

    /// A read of `scene`: one query, `operation`, with `entity` if given.
    std::optional<Value> read(const std::string& scene, std::string_view operation, std::string_view entity = {});

    /// A press at `x`, `y`: what `pressOn` does for the node hit.
    void pressAt(float x, float y);

    /// What comes before a press, taken: keys typed, and a layout of what
    /// an earlier press brought. Whether the press chooses the scene to
    /// move to.
    bool beforePress();

    /// A press on `node`, a pointer's or a screen reader's (D575): a scene
    /// row shows its entities, an entity row chooses it and shows its
    /// components, a field takes the keyboard, and a button does its
    /// operation.
    void pressOn(ui::Node node, bool moving);

    void showScene(std::size_t at);

    /// The chosen scene's entities as the session lists them, or as the
    /// typed search finds them (D473).
    std::optional<Value> entitiesShown();

    void showEntity(std::size_t at);

    /// A component's heading row: its name, and Remove if offered, or for
    /// an instance's entity Revert, which drops what its patch does.
    result::Status componentHeading(std::string_view name, const std::string& component, bool brought, bool stale);

    /// A small button reading `text` at the end of `row`, asking `asked`.
    result::Status action(ui::Node row, std::string_view text, ActionButton asked);

    /// A field's line: its name, and its value in a node that edits.
    result::Result<std::pair<ui::Node, ui::Node>> fieldRow(std::string_view name,
                                                           std::string_view value,
                                                           std::optional<ui::Node> column = std::nullopt,
                                                           bool set = true);

    /// `row`'s field takes the keyboard, its text chosen whole.
    void beginEdit(const FieldRow& row);

    /// The keyboard let go.
    void endEdit();

    /// One thing typed into the field that holds the keyboard: Enter sets
    /// it, Escape puts back what the session holds.
    void take(const view::Typing& typing);

    /// The chosen scene's view section under its rows: the view's eye,
    /// target, and field of view, each a field.
    void showView();

    /// The view's parts in their fields, as the session last said.
    void showViewText();

    /// The scene's view with `part` as `text` says, set by the session
    /// (`authoring.view`), which hands it to a live preview.
    void setView(const std::string& part, const std::string& text);
    /// The view turned on what is chosen, by the session (D792).
    void frameChosen();
    /// The view seen in 2D or out of it (D793).
    void seeIn2D(bool flat);
    /// The scene's bookmark `slot` gone to, or kept from the view (D796).
    void bookmark(std::uint8_t slot, bool go);

    /// `operation` with the chosen entity as its target.
    Value operationOn(std::string_view operation) const;

    /// What `text`, given in `row`, asks of the session: the field set
    /// (`scene.set_field`), the entity renamed, or a component added; text
    /// that cannot be what the row asks is refused before it is asked.
    void apply(const FieldRow& row, const std::string& text);

    /// A new entity at the end of the scene's own, its identity minted
    /// (D438), chosen once made.
    void create();

    /// Copies the chosen entity: a new entity named after it, with each of
    /// its components and the values the scene gives them, in one
    /// transaction (D458).
    void duplicate();
    /// The operations that make `entity`, named `called`, with each of the
    /// components `components` reads and the values the scene gives them.
    [[nodiscard]] static std::vector<Value>
    copyOf(const Value& components, const std::string& entity, const std::string& called);
    /// Moves the chosen entity, its id and components, to the scene listed
    /// at `sceneAt`: made there and deleted here in one transaction over
    /// both scenes (D498).
    void moveTo(std::size_t sceneAt);

    /// Moves the chosen entity one place earlier (`by` -1) or later (+1)
    /// among the scene's own entities.
    void move(int by);

    /// Places in the chosen scene an instance of the scene `text` names.
    void place(const std::string& text);

    /// Makes a new scene at the path `text` gives, `.scene` added if it
    /// has none, and chooses it; or, for a file's absolute path, imports
    /// the file as one of the game's assets (D505).
    void makeScene(std::string text);

    /// Greets the session and reads what it offers and the game's scenes;
    /// false, with `broken_` set, when the game does not read (D453).
    bool openGame();

    /// The game that does not read: why, where, and what to do.
    result::Status showBroken(ui::Node body);

    /// Studio on `description`, its scenes under `root`: a session over it,
    /// where it plays and cooks, and the game kept first among those opened
    /// last (D781).
    void settle(const std::filesystem::path& description, const std::filesystem::path& root);

    /// The start screen, while Studio has no game (D781): the games opened
    /// last, a new one from a template, and one to open by its path.
    result::Status showStart(ui::Node body);

    /// The Preferences dialog over the center (D782): the editor a file is
    /// opened in, then each command's keys, a chord two commands share
    /// said under both; closed again if open.
    void openPreferences();
    void closePreferences();

    /// An entity row pressed (D787): with Control held, added to the
    /// selection or taken from it; with Shift, the rows from the last one
    /// chosen to it; else it alone.
    void chooseEntity(std::size_t at);

    /// The Inspector over every entity chosen, `at` (a place in
    /// `entities_`) the one pressed last: the components they all have,
    /// each field's value where they agree, empty where they differ.
    void showSelection(std::size_t at);

    /// `operation`, done to the entity chosen, or to each of several as one
    /// step (D787).
    void commitChosen(Value operation, const std::string& done);

    /// An entity row let go after a drag (D785): onto another entity, it
    /// follows that one; onto the Hierarchy below its entities, none.
    void dropEntity();

    /// `entity` (a place in `entities_`) made to follow `parent`, or none:
    /// its attachment added if it has none and its parent set, or the
    /// attachment removed; refused for an instance's entity, one that
    /// would follow its own follower, or a game with no attachment.
    void reparent(std::size_t entity, std::optional<std::size_t> parent);

    /// The values `row`'s field is chosen from, in a list under it (D784).
    void showChoices(const FieldRow& row, const std::vector<std::string>& choices);

    /// A press in the Preferences dialog, taken whole; false for a press
    /// outside it.
    bool pressPreferences(ui::Node node);

    /// `command`'s keys from the text typed, its own again when they are
    /// its own, kept in the preferences; refused when one is not a chord.
    void setKeys(const std::string& command, const std::string& text);

    /// The editor's command from the text typed; the setting's or the
    /// default for none.
    void setEditor(const std::string& text);

    /// A press on the start screen: a template chosen, a game opened, or a
    /// field begun.
    void pressStart(ui::Node node);

    /// Studio on the game `description` names, the tree built again for it;
    /// said so, and left on the start screen, when it is not a game.
    void chooseGame(const std::filesystem::path& description);

    /// A new game named `name` from the chosen template, where new games
    /// go, by the same function as `rawframe-author new` (D780), and chosen.
    void makeGame(const std::string& name);

    /// The tree built again, as the game or Studio's state now is, and
    /// given to assistive technology afresh.
    void rebuild();

    /// The preferences written where `studio.preferences` keeps them, if
    /// it names a file.
    void keepPreferences();

    /// The chosen scene's history in the bottom dock's History (D454,
    /// D776): each entry a row, the undone quiet.
    void showHistoryList();

    /// Undoes or redoes until the history's entry `entry` is the last one
    /// applied.
    void stepTo(std::size_t entry);

    /// Opens the diagnostic's file at its line in the author's editor.
    void openEditor(const Diagnostic& at);

    /// The scenes' rows, in order, and the new scene's field under them;
    /// the view's rows, if shown, after them again.
    result::Status showScenes();

    /// `why` in the header, nothing asked, the entity shown as it stands.
    void refuse(const std::string& why);

    /// Whether the chosen document is of `kind`: a style sheet (D637), a UI
    /// document (D643), a material (D722) or a graph (D730), a data document
    /// (D812), the game's own description (D818), or a frame's capture,
    /// read only (D843).
    [[nodiscard]] bool chosen(Listed kind) const noexcept {
        return !scene_.empty() && sceneAt_ < listed_.size() && listed_[sceneAt_] == kind;
    }
    [[nodiscard]] bool documentChosen() const noexcept {
        return !scene_.empty() && sceneAt_ < listed_.size() && listed_[sceneAt_] != Listed::Scene;
    }
    /// What a document's row and tab say: its path, or "Project settings"
    /// for the game's description (D818).
    [[nodiscard]] std::string documentLabel(const std::string& path) const;
    /// The game's settings in the entity column, its lines grouped by the
    /// word they begin with (`lines.read`); a group's lines each a field;
    /// a line set, taken away, or put in through `lines.set` and
    /// `lines.insert` (D818).
    void showSettings();
    void showSettingsGroup(std::size_t at);
    /// A blockout's solids as rows, and the chosen one's words (D850).
    void showBlockout();
    void showPlan();
    void showBlockoutSolid(std::size_t at);
    void dropSolid(std::size_t at, std::array<float, 2> moved);
    /// A terrain's layers as rows, its brush, and its map (D856).
    void showTerrain();
    void showBrush();
    void showTerrainLayer(std::size_t at);
    void setBrush(const FieldRow& row, const std::string& text);
    bool pressMap(ui::Node node);
    /// A tilemap's tiles as rows, the tile painted, and its grid (D861).
    void showTilemap();
    void showTilePaint();
    void showTileRow(std::size_t at);
    void setTilePaint(const std::string& text);
    bool pressTiles(ui::Node node);
    /// A scatter's lines as fields, its copies drawn, and a reseed (D866).
    void showScatter();
    void showScatterLines();
    void reseedScatter();
    /// A sequence's lines as fields, its tracks and keys along its
    /// duration, and a row that adds a track (D869).
    void showSequence();
    void showSequenceLines();
    void addSequenceTrack();
    void setSolid(const FieldRow& row, const std::string& text);
    /// A frame's capture (D843): its figures and its steps in the entity
    /// column, the one chosen in the Inspector, nothing to edit.
    void showCapture();
    void showCaptureEntry(std::size_t at);
    void setSettingLine(const FieldRow& row, const std::string& text);
    void putSettingLine(const FieldRow& row, const std::string& text);
    /// The chosen data document's objects and keyed items in the entity
    /// column, read whole (`data.read`); one of them, its values each a
    /// field; and a value set through `data.set` (D812).
    void showData();
    /// A graph's nodes laid out in the center by how far wires run into
    /// them, wires drawn (D819); `prefix` what each node id is among the
    /// rows' entities. A node pressed is its row pressed.
    void showCanvas(const Value& nodes, const std::string& prefix);
    /// A clip's lanes (D827), a UI document laid out (D832): parts as rows.
    void showTimeline(const Value& clip);
    void showUiPreview(const Value& document);
    void showSheetPreview(const Value& sheet);
    void markUiPreview();
    void forgetUiPreview();
    [[nodiscard]] std::optional<std::size_t> canvasEntityOf(ui::Node node) const;
    void markCanvas();
    void showDataEntry(std::size_t at);
    void setDataValue(const FieldRow& row, const std::string& text);
    /// The chosen style sheet's classes in the entity column, read whole
    /// from the session (`styles.read_sheet`).
    void showSheet();
    /// The class at `at`: each variant's look, every part a field showing
    /// what the sheet sets.
    void showClass(std::size_t at);
    /// `text` set as the part `row` names (`styles.set_part`): nothing
    /// unsets it, JSON is taken as it reads, and anything else as text, so
    /// a color is typed as `#rrggbbaa`.
    void setPart(const FieldRow& row, const std::string& text);
    /// The chosen UI document's nodes in the entity column, each indented
    /// by how deep it stands, read whole (`ui.read_document`, D643).
    void showUi();
    /// The node at `at`: `shows` and every field of `rawframe.ui.Node`, each
    /// a field showing what the document sets.
    void showNode(std::size_t at);
    /// `text` set as the node's field `row` names (`ui.set_field`), typed
    /// as a part's is.
    void setNodeField(const FieldRow& row, const std::string& text);
    /// The chosen node moved one place earlier (`by` -1) or later (+1)
    /// among its siblings (`ui.move_node`, D644).
    void moveNode(int by);
    /// The chosen node moved into `parent`, or to the document's top where
    /// there is none, last there (`ui.move_node`, D671).
    void moveNodeInto(const std::optional<std::string>& parent);
    /// The form New shows with a UI document chosen (D672): a row for each
    /// of the game's node components the document does not hold, pressed
    /// to make a node of it at the document's top (`ui.create_node`).
    void showNodeForm();

    /// The node types a material graph may hold, each a button making one
    /// (D731).
    void showGraphForm();
    /// The chosen material instance's base's inputs, then its switches, in
    /// the entity column, read with the instance from the session
    /// (`materials.read_instance`, D722); those it overrides brighter.
    void showMaterial();
    /// The input or switch at `at`: its type, its base's default, and the
    /// instance's override as a field.
    void showInput(std::size_t at);
    /// `text` set as the chosen input's override (`materials.set_override`),
    /// typed as a part's is: nothing unsets it.
    void setOverride(const std::string& text);
    /// The chosen material graph's nodes in the entity column, each by its
    /// identity's first eight digits and its type, read whole from the
    /// session (`materials.read_graph`, D730).
    void showGraph();
    /// The node at `at`: its params, then its inputs, each a field showing
    /// what the graph holds, a connection as its JSON.
    void showGraphNode(std::size_t at);
    /// The chosen graph node's input `row` connected from `from`'s node and
    /// output (D733).
    void connectGraphInput(const FieldRow& row, const std::pair<std::string, std::string>& from);
    /// `text` set as the chosen node's param or input `row` names
    /// (`materials.set_node_param`, `materials.set_node_input`), typed as a
    /// part's is: nothing unsets it.
    void setGraphMember(const FieldRow& row, const std::string& text);

    /// One operation asked of the session for the chosen scene; `done` or
    /// the session's message said; the scene shown again with `choose`
    /// chosen if it stands.
    void commit(Value operation, const std::string& done, const std::optional<std::string>& choose);
    /// The same with several operations, one transaction, one history
    /// entry (ADR-0065).
    void commitAll(std::vector<Value> operations, const std::string& done, const std::optional<std::string>& choose);

    /// Moves the chosen scene's entity a drag carried (D457): its 3D
    /// pose's x and z, in one transaction.
    void moveDragged(const Moved& moved);

    /// The pose of an entity of the chosen scene, its 3D pose's fields by
    /// name, each at nought where the scene leaves it at its default; none
    /// for an entity with no such pose (D457, D464).
    std::optional<std::map<std::string, double, std::less<>>> poseOf(const std::string& source);

    /// Has the preview mark where the chosen entity stands, or nothing for
    /// none or one with no pose (D464).
    void markChosen();

    /// The chosen scene listed again, `entity` chosen if it stands, the
    /// columns scrolled where they were: a change is not a new place.
    void refresh(const std::optional<std::string>& entity);

    /// The chosen scene's last change undone, or the last undone redone,
    /// then the scene shown again, its entity still chosen if it stands.
    void step(std::string_view kind);

    /// What can be undone and redone, as an outcome says.
    void told(const Outcome& outcome);

    /// Undo and redo, quiet when there is nothing to do.
    result::Status showHistory();

    /// The game played to preview it: its server and client started, the
    /// client attached as the chosen scene's preview once it says who it is.
    void startPlaying();
    /// The played game's World paused or let run, or stepped a tick (D806).
    void runPlayed(bool step);
    /// The Network panel made, a link button pressed, the server's traffic
    /// read once a second while it is shown, and its lines (D807).
    result::Status makeNetwork();
    /// The environment panel: the scene's sun, sky, probes, and passes (D834).
    result::Status makeEnvironment();
    void showEnvironment();
    bool pressEnvironment(ui::Node node);
    /// The Profiler (D838) and the Frame tab (D842): the played game's
    /// server's last ticks and its preview's last frame, read once a second
    /// while shown; each keeps a capture when its button is pressed (D844).
    result::Status makeProfiler();
    void readProfiler(double seconds);
    void showProfiler(const std::optional<Value>& profile);
    result::Status makeFrame();
    void readFrame(double seconds, std::string_view capture = {});
    bool pressCapture(ui::Node node);
    bool pressNetwork(ui::Node node);
    void readNetwork(double seconds);
    void showNetwork(const std::vector<std::string>& lines);
    /// The live World (D808): switched on or off, listed in the Hierarchy,
    /// an entity of it shown and read again, and a press on its rows.
    void switchLive();
    void showLive();
    void showLiveEntity(const std::string& entity);
    void readLive(double seconds);
    bool pressLive(ui::Node node);

    /// The game let go and asked to stop.
    void stopPlaying();

    /// The game cooked into its play directory, its content then what
    /// Play reads (D502); or, while it cooks, asked to stop.
    void cookOrStop();

    /// What the session says of a running cook, shown as it says it.
    void hearCook();

    /// A material changed while a game plays from Studio's cook: cooked
    /// again, or once the running cook ends, so the game, following its
    /// content, draws it (D734).
    void recookPlayed();

    /// While the game plays and no preview is live, its client attached as
    /// the chosen scene's preview, tried twice a second: it says who it is,
    /// then admits a session once its player is in.
    void attachPlayed(double seconds);

    /// Selects what the author last clicked in the preview, if it is new
    /// and the chosen scene brought it (D456); asked four times a second.
    void pollPicks(double seconds);

    /// Attaches the chosen scene's preview to `preview_`, saying how it
    /// went and logging `studio_previewing` once it is live.
    void attachPreview();

    /// `text` in the header's status.
    void say(std::string text);

    std::unique_ptr<authoring_session::Session> session_;
    std::unique_ptr<ui::Tree> tree_;
    ui::Node root_{};
    std::uint64_t keys_ = 0;
    ui::DrawList list_;
    const ui::DrawList* drawn_ = nullptr;
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 720;
    std::filesystem::path description_;
    std::filesystem::path sceneRoot_;
    /// Why the game does not read, while it does not; and where.
    std::optional<std::string> broken_;
    std::optional<Diagnostic> diagnostic_;
    /// The author's editor, as `studio.editor` gives it (D453).
    std::string editor_;
    std::vector<process::Child> editors_;
    std::vector<ui::Node> historyRows_;
    std::vector<Command> commands_;
    /// The menu bar's buttons and the menus they open.
    std::vector<std::pair<ui::Node, std::string>> menus_;
    /// An open menu or the palette's matches, the menu if it is one, and
    /// its rows with the commands they do.
    std::optional<ui::Node> popup_;
    std::optional<std::size_t> menuOpen_;
    std::vector<std::pair<ui::Node, std::size_t>> popupRows_;
    std::optional<FieldRow> palette_;
    /// The bottom dock, its tabs, and its panes: one attached at a time.
    ui::Node bottomDock_{};
    ui::Node assetsTab_{};
    ui::Node historyTab_{};
    ui::Node historyColumn_{};
    Dock dockShown_ = Dock::Assets;
    /// The splitters by edge, the one held with where the pointer was and
    /// the size then, and the sizes preferred, kept in `preferencesFile_`
    /// when Studio has one (`studio.preferences`, D779).
    std::array<ui::Node, 3> splitters_{};
    struct Resizing {
        Edge edge = Edge::Hierarchy;
        float from = 0;
        float size = 0;
    };
    std::optional<Resizing> resizing_;
    std::array<float, 2> pressedAt_{};
    /// The entity row pressed, until the press is let go (D785).
    std::optional<std::size_t> heldEntity_;
    /// The entities chosen, a Shift press's start, edits to many (D787).
    std::vector<std::string> selected_;
    std::optional<std::size_t> anchor_;
    std::uint64_t together_ = 0;
    bool pointerPress_ = false;
    /// A press let go, and where (D833).
    bool released_ = false;
    std::optional<std::array<float, 2>> releasedAt_;
    Preferences preferences_;
    std::filesystem::path preferencesFile_;
    std::uint64_t resized_ = 0;
    ui::Node consoleTab_{};
    ui::Node networkTab_{};
    /// The Console's pane: its filters over the list of its lines, which
    /// alone scrolls, so the filters stay in sight.
    ui::Node consoleColumn_{};
    /// The Network panel (D807).
    ui::Node networkColumn_{};
    ui::Node environmentTab_{};
    ui::Node profilerTab_{};
    NetworkShown network_;
    LiveShown live_;
    ui::Node consoleList_{};
    /// The Console's lines, oldest first, and the played game's logs.
    std::deque<ConsoleLine> console_;
    std::vector<LogTail> tails_;
    double nextLogRead_ = 0;
    /// Its rows with the lines they show, and its filters: the sources
    /// shown, by `ConsoleLine::Source`, and warnings and errors alone.
    std::vector<std::pair<ui::Node, std::size_t>> consoleRows_;
    std::array<bool, 3> consoleSources_{true, true, true};
    bool consoleProblems_ = false;
    std::vector<std::pair<ui::Node, int>> consoleFilters_;
    std::uint64_t consoleLines_ = 0;
    /// The center: the documents' tabs, then the active one's view.
    ui::Node centerColumn_{};
    ui::Node tabsRow_{};
    /// The documents opened, by path, oldest first, and their tabs.
    std::vector<std::string> openDocuments_;
    std::vector<ui::Node> tabRows_;
    /// The rows of the history's entries alone, oldest first.
    std::vector<ui::Node> entryRows_;
    ui::Node openNode_{};
    ui::Node retryNode_{};
    std::uint64_t opened_ = 0;
    std::vector<std::string> scenes_;
    /// What each document listed with the scenes is, the chosen sheet or
    /// UI document as the session last read it, and what the document
    /// editors last read and laid out (editors.h).
    std::vector<Listed> listed_;
    std::optional<Value> sheet_;
    EditorsHeld held_;
    /// The node types a material graph may hold, each with its params and
    /// inputs, read once a graph is first shown (D732).
    std::optional<Value> nodeTypes_;
    /// The assets the game declares, read as it opens (D455).
    std::vector<Asset> assets_;
    /// Each scene's identity, from its sidecar, as 32 hex digits.
    std::vector<std::string> sceneSources_;
    /// The chosen scene's own field rows under its entities.
    std::vector<ui::Node> sceneFieldRows_;
    std::vector<FieldRow> sceneFields_;
    ui::Node newSceneRow_{};
    std::optional<FieldRow> newScene_;
    Catalog catalog_;
    std::vector<std::string> names_;
    std::vector<bool> brought_;
    /// Whether each brought entity is one its instance removed.
    std::vector<bool> removed_;
    /// Each listed entity's parent, empty for none, beside `entities_` (D785).
    std::vector<std::string> parents_;
    /// Each of the scene's own entities' place among them; none for one an
    /// instance brought.
    std::vector<std::optional<std::int64_t>> places_;
    ui::Node newNode_{};
    ui::Node deleteNode_{};
    ui::Node restoreNode_{};
    ui::Node uninstanceNode_{};
    ui::Node upNode_{};
    ui::Node duplicateNode_{};
    ui::Node moveNode_{};
    /// Move to was pressed: the next press on a scene's row moves the
    /// chosen entity there, and any other lets it go (D498).
    bool moving_ = false;
    /// An output was pressed, its node's identity and its name: choosing a
    /// node keeps it, an input field pressed takes a connection from it,
    /// and any other press lets it go (D733).
    std::optional<std::pair<std::string, std::string>> connecting_;
    ui::Node downNode_{};
    std::vector<ActionButton> actions_;
    view::UiPointing* pointing_ = nullptr;
    view::UiTyping* typing_ = nullptr;
    view::FilesDropped* dropped_ = nullptr;
    /// Where the window is read for assistive technology, if its program
    /// lends one (D575).
    ui::AccessSeat* seat_ = nullptr;
    std::vector<view::Typing> typed_;
    std::unique_ptr<ui::TextEdit> edit_;
    std::vector<FieldRow> fields_;
    std::optional<FieldRow> editing_;
    std::optional<Preview> preview_;
    bool previewing_ = false;
    std::optional<Value> view_;
    std::vector<ui::Node> viewRows_;
    std::vector<FieldRow> viewFields_;
    std::uint64_t viewsSet_ = 0;
    /// Views moved in the played game (D469).
    std::uint64_t viewsMoved_ = 0;
    /// What drags in the played game snap to, and how many snapped (D471).
    Snap snap_;
    std::uint64_t snapped_ = 0;
    /// The entity search as typed, the field it is typed in, and how many
    /// searches the session answered (D473).
    std::string find_;
    std::optional<FieldRow> findField_;
    std::uint64_t searched_ = 0;
    std::optional<PlaySettings> play_;
    /// Where a game plays as Studio's settings name it; empty for each
    /// game's own (D462).
    std::filesystem::path playDirectory_;
    /// The start screen's (D781): the templates' directory and where new
    /// games go, the templates found and the chosen one, the games opened
    /// last as rows, and its fields.
    std::filesystem::path templates_;
    std::filesystem::path projects_;
    std::vector<std::pair<ui::Node, std::filesystem::path>> templateRows_;
    std::size_t templateAt_ = 0;
    std::vector<std::pair<ui::Node, std::filesystem::path>> recentRows_;
    std::optional<FieldRow> newGameField_;
    std::optional<FieldRow> openGameField_;
    std::uint64_t made_ = 0;
    /// The Preferences dialog while open, its fields, and its Close; each
    /// command's own keys, and the editor's command as Studio's settings
    /// give it (D782).
    std::optional<ui::Node> dialog_;
    /// A field's list of values while open, and the field (D784).
    std::vector<std::pair<ui::Node, std::string>> choiceRows_;
    std::optional<FieldRow> choosing_;
    std::vector<FieldRow> shortcutFields_;
    std::optional<FieldRow> editorField_;
    ui::Node dialogClose_{};
    std::map<std::string, std::vector<std::string>, std::less<>> defaultKeys_;
    std::string configuredEditor_;
    std::uint64_t rebound_ = 0;
    std::optional<Play> playing_;
    std::optional<Play> stopping_;
    ui::Node playNode_{};
    std::uint64_t played_ = 0;
    /// The mark's part last said lit (D504).
    std::string lit_;
    /// The cook tool, where the game's content and the cook's cache go,
    /// and the running cook's id, if any (D502).
    std::filesystem::path cookTool_;
    std::filesystem::path cookAt_;
    ui::Node cookNode_{};
    std::optional<std::int64_t> cooking_;
    /// A material changed during a cook: cooked again once it ends (D734).
    bool recook_ = false;
    double nextAttach_ = 0;
    double nextPick_ = 0;
    std::uint64_t picked_ = 0;
    std::uint64_t dragged_ = 0;
    /// Of those, raised and turned (D463).
    std::uint64_t raised_ = 0;
    std::uint64_t turned_ = 0;
    /// The marks the preview showed (D464).
    std::uint64_t marked_ = 0;
    std::vector<std::array<float, 4>> wheels_;
    std::uint64_t wheeled_ = 0;
    /// Whether a press was acted on, or keys taken before one, since the
    /// tree was last laid out: the next press is hit against it laid out
    /// again, so input read in one frame each finds what came before it
    /// brought (D486, D489).
    bool pressedSinceLayout_ = false;
    std::chrono::steady_clock::time_point began_ = std::chrono::steady_clock::now();
    std::size_t entityAt_ = 0;
    ui::Node statusNode_{};
    ui::Node undoNode_{};
    ui::Node redoNode_{};
    std::size_t sceneAt_ = 0;
    std::int64_t undoable_ = 0;
    std::int64_t redoable_ = 0;
    std::uint64_t undone_ = 0;
    std::uint64_t redone_ = 0;
    std::string status_;
    std::uint64_t applied_ = 0;
    std::uint64_t refused_ = 0;
    ui::Node scenesColumn_{};
    ui::Node entitiesColumn_{};
    ui::Node componentsColumn_{};
    /// The left dock, its Layers in place of the Hierarchy, each heading's
    /// press for the other, the Layers' rows with what they do, and the
    /// layers locked (D788).
    ui::Node leftDock_{};
    ui::Node layersColumn_{};
    ui::Node layersTab_{};
    ui::Node hierarchyTab_{};
    Left leftShown_ = Left::Hierarchy;
    /// The World's column, its heading's press back to the Hierarchy, the
    /// Layers' press for it, its rows, and each district's press with the
    /// entity its instance is named by and how it is streamed (D870).
    ui::Node worldColumn_{};
    ui::Node worldTab_{};
    ui::Node worldBackTab_{};
    std::vector<ui::Node> worldRows_;
    struct DistrictPress {
        std::string entity;
        std::string streaming;
    };
    std::vector<std::pair<ui::Node, DistrictPress>> districtPresses_;
    struct LayerPress {
        enum class Does : std::uint8_t {
            Choose,
            Lock,
            Hide,
        };
        std::uint32_t layer = 0;
        Does does = Does::Choose;
    };
    std::vector<ui::Node> layerRows_;
    std::vector<std::pair<ui::Node, LayerPress>> layerPresses_;
    std::vector<std::uint32_t> locked_;
    /// What the preview is told to show, told it again when one is attached.
    PreviewShown shown_;
    AssetBrowser browser_;
    /// The layer each entity in `entities_` is on, nought for none.
    std::vector<std::uint32_t> entityLayers_;
    std::uint64_t locks_ = 0;
    std::uint64_t layersChosen_ = 0;
    std::uint64_t lockRefused_ = 0;
    /// The viewport, whether Studio plays a game in it, and the picture it
    /// shows: when its file was written, its sequence, and how many were
    /// shown (D789).
    ui::Node viewport_{};
    PlayedViewport inViewport_;
    /// How many players a play has (D804), and the device its first is
    /// tried as (D809).
    std::uint32_t players_ = 1;
    std::optional<std::array<std::uint32_t, 2>> device_;
    std::vector<ui::Node> sceneRows_;
    std::vector<ui::Node> entityRows_;
    std::vector<ui::Node> componentRows_;
    std::vector<std::string> entities_;
    std::string scene_;
    std::string entity_;
    std::uint64_t components_ = 0;
    std::string title_;
    ui::Font font_{};
    std::uint64_t records_ = 0;
    std::uint64_t framesDrawn_ = 0;
    diagnostics::Emitter emitter_;
};

} // namespace rawframe::studio
