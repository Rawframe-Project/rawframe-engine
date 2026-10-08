#pragma once

// The session's records as Studio speaks them (D435): what it asks, built
// as any client builds them, and what it is answered, read as any client
// reads them. Studio keeps no document of its own: a panel shows what an
// answer says.

#include "rawframe/document/json.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

using document::Value;

/// A field's value as a row shows it: a number or a text as itself,
/// anything else as its compact JSON.
[[nodiscard]] std::string shown(const Value& value);

/// What `text` typed into a field of `kind` (a read's value key) asks the
/// session to set it to; none for a kind Studio does not edit, or text
/// that is not one of its values. The session checks it again.
[[nodiscard]] std::optional<Value> typedValue(std::string_view kind, std::string_view text);

/// The first answer of a read's reply, if it is one.
[[nodiscard]] std::optional<Value> firstAnswer(std::string_view reply);

/// What an outcome (`authoring.apply`, `authoring.undo`, `authoring.redo`)
/// says: whether its one slot was done, the session's message when not,
/// and what can be undone and redone after it.
struct Outcome {
    bool done = false;
    std::string message;
    std::int64_t undoable = 0;
    std::int64_t redoable = 0;
    /// The scene's view after it, when the answer says (null for none).
    std::optional<Value> view;
};

/// Of an apply's reply, or of the first scene's in an apply together's.
[[nodiscard]] Outcome outcomeOf(std::string_view reply);

/// What `authoring.view` or `authoring.preview` answers: whether it was
/// done, the session's message when not, the scene's view when said (null
/// for none), and whether a preview of the scene is live.
struct Answered {
    bool done = false;
    std::string message;
    std::optional<Value> view;
    bool previewing = false;
    /// A new scene's identity, as 32 hex digits (D449).
    std::string resource;
};

[[nodiscard]] Answered answeredOf(std::string_view reply);

/// One entry of a scene's history (D454): what it did, and whether it is
/// applied (undoable) or undone (redoable).
struct HistoryEntry {
    std::string summary;
    bool applied = false;
};

/// `authoring.history` for `scene`, and the entries its answer lists,
/// oldest first; none for a refusal.
[[nodiscard]] Value historyRecord(std::int64_t id, std::string_view scene);
[[nodiscard]] std::vector<HistoryEntry> historyOf(std::string_view reply);

/// An asset the game declares (D455): its kind, the identity a field
/// holds it by, and its name.
struct Asset {
    std::string kind;
    std::uint64_t id = 0;
    std::string name;
};

/// `authoring.assets`, and the assets its answer lists; none for a refusal
/// or an entry out of shape.
[[nodiscard]] Value assetsRecord(std::int64_t id);
[[nodiscard]] std::vector<Asset> assetsOf(std::string_view reply);

/// The asset an unsigned field's value names, its number as the scene
/// writes it; none when it names none.
[[nodiscard]] const Asset* assetHeld(std::span<const Asset> assets, std::string_view value);

/// The asset `text` names: as Studio shows it (`name (kind)`), by its whole
/// name, by its file's name, or by the start of its name, when one alone
/// matches; else none, and `why` says so.
[[nodiscard]] std::optional<Asset> assetNamed(std::span<const Asset> assets, std::string_view text, std::string& why);

/// How Studio shows `asset` in a field.
[[nodiscard]] std::string assetShown(const Asset& asset);

/// `authoring.pick` for `scene` (D456), and what its answer says the
/// author's newest click met in the scene: its id there; none for no new
/// click, or one that met nothing `scene` brings.
[[nodiscard]] Value pickRecord(std::int64_t id, std::string_view scene);

/// `authoring.cook` (D502): the game cooked into `output`, reusing what
/// `cache` holds; and `authoring.cancel` for the cook given `operation`.
[[nodiscard]] Value
cookRecord(std::int64_t id, const std::filesystem::path& output, const std::filesystem::path& cache);
[[nodiscard]] Value cancelRecord(std::int64_t id, std::int64_t operation);

/// What the session said unasked of a cook (D502): how far it is, or how
/// it ended; `None` for any other line.
struct Heard {
    enum class Kind : std::uint8_t {
        None,
        Progress,
        Cooked,
        Failed,
        Cancelled
    };
    Kind kind = Kind::None;
    /// The cook's id, as Studio gave it.
    std::int64_t id = 0;
    std::int64_t step = 0;
    std::int64_t steps = 0;
    /// Progress's source; a failure's message.
    std::string text;
    std::int64_t cooked = 0;
    std::int64_t reused = 0;
};

[[nodiscard]] Heard heardOf(std::string_view line);

/// `authoring.mark` for `scene` (D464): the point its preview marks, or
/// none.
[[nodiscard]] Value markRecord(std::int64_t id, std::string_view scene, const std::optional<std::array<double, 3>>& at);
[[nodiscard]] std::optional<std::string> pickedIn(std::string_view reply, std::string_view scene);

/// The part of the preview's mark a pick's answer says the pointer has
/// lit (D468): x, y, z, or ring; empty for none.
[[nodiscard]] std::string litIn(std::string_view reply);

/// An entity the author dragged in the preview (D457): its id in the scene,
/// and how: carried across the level plane, in metres along x and z;
/// raised or lowered, along y, with Shift; or turned about its own place
/// with Control, from where it was pressed to where it was let go on the
/// level plane (D463).
struct Moved {
    enum class How : std::uint8_t {
        Move,
        Height,
        Turn
    };
    std::string source;
    How how = How::Move;
    double x = 0;
    double y = 0;
    double z = 0;
    std::array<double, 3> from{};
    std::array<double, 3> to{};
};

/// What `authoring.pick`'s answer says was dragged in `scene`; none for no
/// drag, or one of another scene's entity.
[[nodiscard]] std::optional<Moved> movedIn(std::string_view reply, std::string_view scene);

/// The view `authoring.pick`'s answer says the played game moved the
/// scene's to by its wheel or a drag with the right button (D469); none
/// where nothing moved.
[[nodiscard]] std::optional<Value> viewedIn(std::string_view reply);

/// An entity search as typed (D473): its words a name the entities' hold,
/// and a `has:` word the component they hold, by any name `componentNamed`
/// takes; sugar over `scene.find_entities`'s typed inputs.
struct Search {
    std::string named;
    std::string having;
};

[[nodiscard]] Search searchOf(std::string_view text);

/// A `scene.find_entities` query of `scene`: `having` a component id, or
/// empty for none.
[[nodiscard]] Value
findRecord(std::int64_t id, std::string_view scene, std::string_view named, std::string_view having);

/// What a drag in the played game snaps to (D471): a place to whole steps
/// of `move` meters, a turn to whole steps of `turn` degrees; nought for
/// none. Studio's own, never the scene's (ADR-0066).
struct Snap {
    double move = 0;
    double turn = 0;
};

/// A snap as typed: two numbers, meters and degrees, or one for meters
/// alone, or `0` for none; none for anything else, or a step below nought.
[[nodiscard]] std::optional<Snap> snapOf(std::string_view text);

/// A snap as Studio shows it.
[[nodiscard]] std::string snapText(const Snap& snap);

/// `value` to the nearest whole step of `step`, or as it is for none.
[[nodiscard]] double snapped(double value, double step) noexcept;

/// `authoring.create_scene` for `scene` (D449).
[[nodiscard]] Value createSceneRecord(std::int64_t id, std::string_view scene);

/// A running Runtime's tooling endpoint to preview a scene in (D433).
struct Preview {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
    /// The previewed game's server, for picking (D456): its tooling
    /// endpoint and fingerprint file, the token the preview's; empty for
    /// none.
    std::string serverEndpoint;
    std::string serverPinFile;
};

/// `authoring.view` of `scene`.
[[nodiscard]] Value viewRecord(std::int64_t id, std::string_view scene, const Value& view);
/// `authoring.preview` of `scene` in `preview`, or letting it go.
[[nodiscard]] Value previewRecord(std::int64_t id, std::string_view scene, const Preview* preview);

/// A whole view (`eye`, `target`, `fieldOfView`) with its part `part` as
/// `text` says: three numbers apart by spaces or commas for a point, one
/// for the field of view, the rest as `current` has them, or a view from
/// above the origin when there is none; none when the text is not that.
[[nodiscard]] std::optional<Value>
viewWith(const std::optional<Value>& current, std::string_view part, std::string_view text);

/// A view's part `part` as a field shows it, empty while there is none.
[[nodiscard]] std::string viewText(const std::optional<Value>& view, std::string_view part);

/// `authoring.read` of `scene`: one query, `operation`, with `entity` if
/// given.
[[nodiscard]] Value
readRecord(std::int64_t id, std::string_view scene, std::string_view operation, std::string_view entity = {});
/// `authoring.apply` to `scene` of one operation, atomic.
[[nodiscard]] Value applyRecord(std::int64_t id, std::string_view scene, Value operation);
/// The same with several operations, applied together or not at all.
[[nodiscard]] Value applyRecord(std::int64_t id, std::string_view scene, std::vector<Value> operations);
/// `authoring.apply_together` (D497): each scene's operations, atomic, all
/// kept or none.
[[nodiscard]] Value togetherRecord(std::int64_t id, std::vector<std::pair<std::string, std::vector<Value>>> parts);
/// `authoring.select` of `entity` in `scene`.
[[nodiscard]] Value selectRecord(std::int64_t id, std::string_view scene, std::string_view entity);
/// `authoring.undo` or `authoring.redo` (`kind`) of `scene`.
[[nodiscard]] Value stepRecord(std::int64_t id, std::string_view kind, std::string_view scene);

/// What `authoring.describe` says Studio may do: the operations it offers
/// by name, and the component catalog's types by identity and name.
struct Catalog {
    struct Field {
        std::string name;
        /// Its kind as a read value's key names it (`real`, `unsigned`...),
        /// empty where the catalog has none.
        std::string kind;
    };
    struct Component {
        std::string id;
        std::string name;
        /// Its fields in the catalog's order.
        std::vector<Field> fields;
    };
    std::vector<std::string> operations;
    std::vector<Component> components;

    [[nodiscard]] bool offers(std::string_view operation) const;
    /// The catalog's type of identity `id`, if it has one.
    [[nodiscard]] const Component* component(std::string_view id) const;
};

[[nodiscard]] Catalog catalogOf(std::string_view reply);

/// A component's field as Studio lists it: its name, its kind, and its
/// value's text when the scene sets it.
struct FieldShown {
    std::string name;
    std::string kind;
    std::optional<std::string> text;
};

/// A component's fields as `read_entity` gave them (`fields`, may be
/// null) over its type's in the catalog (`type`, may be null): the
/// catalog's in its order, each with the scene's value when set, then any
/// the scene sets that the catalog does not know. A field's kind is its
/// value's, else the catalog's.
[[nodiscard]] std::vector<FieldShown> fieldsShown(const Catalog::Component* type, const Value* fields);

/// The scene of `scenes` (paths) `text` names: by its whole path, its
/// file's name, or its file's name without `.scene`, then by the start
/// of either, when one alone matches; else none, and `why` says so.
[[nodiscard]] std::optional<std::size_t>
sceneNamed(std::span<const std::string> scenes, std::string_view text, std::string& why);

/// The catalog's component `text` names: by its whole name, or by the
/// part after its last dot, or by the start of either, when one alone
/// matches; else none, and `why` says so.
[[nodiscard]] std::optional<Catalog::Component>
componentNamed(const Catalog& catalog, std::string_view text, std::string& why);

/// The scene's entity `text` names, of `ids` and their `names` in the
/// same order: by its whole name, by the start of its identity (four
/// characters at least), or by the start of its name, when one alone
/// matches; else none, and `why` says so (D447).
[[nodiscard]] std::optional<std::string> entityNamed(std::span<const std::string> ids,
                                                     std::span<const std::string> names,
                                                     std::string_view text,
                                                     std::string& why);

/// Where a game's program does not compile, as the session's refusal of
/// hello says it (`file:line:column: message`, D453).
struct Diagnostic {
    std::string file;
    std::int64_t line = 0;
    std::int64_t column = 0;
    std::string message;
};

/// The diagnostic a refusal names (`details.diagnostic`), if it is in
/// that shape.
[[nodiscard]] std::optional<Diagnostic> diagnosticOf(std::string_view reply);

/// The program and arguments that open `file` at a diagnostic's line and
/// column in the author's editor: `command` split at its spaces, each
/// `{file}`, `{line}`, and `{column}` in it written in (ADR-0066's
/// open-at-line hand-off). None for an empty command.
[[nodiscard]] std::vector<std::string>
editorCommand(std::string_view command, const std::string& file, std::int64_t line, std::int64_t column);

/// Where `program` is: itself when it names a directory, else the first
/// directory of `path` (PATH's text) holding it, as a shell finds it; on
/// Windows also with `.exe` or `.cmd` after it. None when nowhere.
[[nodiscard]] std::optional<std::filesystem::path> programOnPath(const std::string& program, std::string_view path);

/// Whether `program` is a batch file (`.cmd` or `.bat`, in any case),
/// which Windows starts through cmd.exe, parsing its arguments again.
[[nodiscard]] bool isBatch(const std::filesystem::path& program);

/// Whether `word` is safe as a batch file's argument: ASCII letters,
/// digits, and `space _ - . : / \ + , = @ # ~ { } [ ] '` alone, nothing
/// cmd.exe acts on (`& | < > ^ % ! " ( )`), no control and no non-ASCII
/// byte, so a game's file name cannot run a command through the editor's
/// launcher (D453a).
[[nodiscard]] bool batchSafe(std::string_view word);

/// A new identity, a random version 4 UUID in its text form: a new entity's
/// is the client's to choose (D438), and an identity is data, not
/// simulation, so nothing needs it repeated.
[[nodiscard]] std::string mintedIdentity();

} // namespace rawframe::studio
