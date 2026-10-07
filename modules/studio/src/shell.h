#pragma once

// Studio's shell (D435): a participant holding an authoring session on a
// game in its own process and showing it as a UI of its own. What its
// parts share: Studio's colors, the rows and buttons it acts on, and the
// shell itself, whose lifecycle is participant.cpp's, its panels
// panels.cpp's, its presses, typing, and session records editing.cpp's,
// and the game it plays playing.cpp's.

#include "play.h"
#include "rawframe/authoring_session/game.h"
#include "rawframe/authoring_session/session.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/font_import/sanitize.h"
#include "rawframe/process/child.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/ui/frames.h"
#include "rawframe/ui/text_edit.h"
#include "rawframe/ui/tree.h"
#include "rawframe/view/pointing.h"
#include "rawframe/view/typing.h"
#include "records.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

inline constexpr diagnostics::EventIdentity kSummary{"studio", "studio_summary"};
inline constexpr diagnostics::EventIdentity kShown{"studio", "studio_shown"};
inline constexpr diagnostics::EventIdentity kPreviewing{"studio", "studio_previewing"};

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

/// A field Studio edits and what the session needs to act on it: a
/// component's field (its kind the read value's key), the chosen entity's
/// name, the name of a component to add, or a part of the scene's view.
struct FieldRow {
    enum class Role : std::uint8_t {
        Field,
        Name,
        Add,
        View,
        /// The scene an instance placed in the chosen scene is of.
        Instance,
        /// The path of a new scene (D449).
        NewScene
    };
    ui::Node value{};
    Role role = Role::Field;
    std::string component;
    std::string field;
    std::string kind;
};

/// A button on a component or a field and the operation it asks: Remove,
/// or an instance's Revert of a component or a field.
struct ActionButton {
    ui::Node node{};
    std::string operation;
    std::string component;
    /// The field, for a field's.
    std::string field;
    /// What the header says once it is done.
    std::string done;
    /// The scene's operation, naming no entity: a component's remark.
    bool whole = false;
};

/// The shell: a session on the game, and the UI that shows it.
class ShellParticipant final : public composition::Participant, public ui::UiFrames {
public:
    result::Status load(composition::ParticipantContext& context);

    result::Status start(composition::ParticipantContext& context) noexcept override;

    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override;

    void stop() noexcept override;

    composition::CapabilityObject provide(std::string_view capability) noexcept override;

    const ui::DrawList* drawn() const noexcept override;

    void resize(std::uint32_t width, std::uint32_t height) noexcept override;

    std::shared_ptr<const texture::Texture> image(std::uint64_t) const override;

private:
    /// A box of `color` as `layout` places it under `parent`, if any.
    result::Result<ui::Node> box(std::optional<ui::Node> parent, const ui::Layout& layout, std::uint32_t color);

    /// Words on `node`, in Studio's font.
    result::Status words(ui::Node node, std::string_view text, std::uint32_t color, float size = 15);

    /// A column under `parent` headed `heading`.
    result::Result<ui::Node> column(ui::Node parent, std::string_view heading);

    /// A button reading `text` under `parent`.
    result::Result<ui::Node> button(ui::Node parent, std::string_view text);

    /// The window: a header over three columns, the scenes' with a row for
    /// each scene beside the game.
    result::Status build();

    /// A row of `text` under `column`, its height fixed.
    result::Result<ui::Node>
    row(ui::Node column, std::string_view text, std::uint32_t color, std::uint32_t fill = kRow);

    /// Removes `rows` from the tree.
    void clear(std::vector<ui::Node>& rows);

    /// One record to the session, counted; its reply.
    std::string ask(const Value& record);

    /// The next record's id.
    std::int64_t next() const noexcept;

    /// A read of `scene`: one query, `operation`, with `entity` if given.
    std::optional<Value> read(const std::string& scene, std::string_view operation, std::string_view entity = {});

    /// A press at `x`, `y`: a scene row shows its entities, an entity row
    /// chooses it and shows its components, a field takes the keyboard, and
    /// a button does its operation.
    void pressAt(float x, float y);

    void showScene(std::size_t at);

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

    /// `operation` with the chosen entity as its target.
    Value operationOn(std::string_view operation) const;

    /// What `text`, given in `row`, asks of the session: the field set
    /// (`scene.set_field`), the entity renamed, or a component added; text
    /// that cannot be what the row asks is refused before it is asked.
    void apply(const FieldRow& row, const std::string& text);

    /// A new entity at the end of the scene's own, its identity minted
    /// (D438), chosen once made.
    void create();

    /// Moves the chosen entity one place earlier (`by` -1) or later (+1)
    /// among the scene's own entities.
    void move(int by);

    /// Places in the chosen scene an instance of the scene `text` names.
    void place(const std::string& text);

    /// Makes a new scene at the path `text` gives, `.scene` added if it
    /// has none, and chooses it.
    void makeScene(std::string text);

    /// Greets the session and reads what it offers and the game's scenes;
    /// false, with `broken_` set, when the game does not read (D453).
    bool openGame();

    /// The game that does not read: why, where, and what to do.
    result::Status showBroken();

    /// The chosen scene's history under its view (D454): each entry a row,
    /// the undone quiet, rebuilt after the view's rows, which are rebuilt
    /// at the column's end.
    void showHistoryList();

    /// Undoes or redoes until the history's entry `entry` is the last one
    /// applied.
    void stepTo(std::size_t entry);

    /// Opens the diagnostic's file at its line in the author's editor.
    void openEditor();

    /// The scenes' rows, in order, and the new scene's field under them;
    /// the view's rows, if shown, after them again.
    result::Status showScenes();

    /// `why` in the header, nothing asked, the entity shown as it stands.
    void refuse(const std::string& why);

    /// One operation asked of the session for the chosen scene; `done` or
    /// the session's message said; the scene shown again with `choose`
    /// chosen if it stands.
    void commit(Value operation, const std::string& done, const std::optional<std::string>& choose);

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

    /// The game let go and asked to stop.
    void stopPlaying();

    /// While the game plays and no preview is live, its client attached as
    /// the chosen scene's preview, tried twice a second: it says who it is,
    /// then admits a session once its player is in.
    void attachPlayed(double seconds);

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
    /// The rows of the history's entries alone, oldest first.
    std::vector<ui::Node> entryRows_;
    ui::Node openNode_{};
    ui::Node retryNode_{};
    std::uint64_t opened_ = 0;
    std::vector<std::string> scenes_;
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
    /// Each of the scene's own entities' place among them; none for one an
    /// instance brought.
    std::vector<std::optional<std::int64_t>> places_;
    ui::Node newNode_{};
    ui::Node deleteNode_{};
    ui::Node restoreNode_{};
    ui::Node uninstanceNode_{};
    ui::Node upNode_{};
    ui::Node downNode_{};
    std::vector<ActionButton> actions_;
    view::UiPointing* pointing_ = nullptr;
    view::UiTyping* typing_ = nullptr;
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
    std::optional<PlaySettings> play_;
    std::optional<Play> playing_;
    std::optional<Play> stopping_;
    ui::Node playNode_{};
    std::uint64_t played_ = 0;
    double nextAttach_ = 0;
    std::vector<std::array<float, 4>> wheels_;
    std::uint64_t wheeled_ = 0;
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
