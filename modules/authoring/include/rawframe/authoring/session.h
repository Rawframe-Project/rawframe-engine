#pragma once

// An authoring session: one record a
// line in each direction, each the compact form of a strict JSON object
// (the hash-input form, so no line holds a line feed), between a
// client and a tool holding the documents open, their histories kept from
// one record to the next. The records carry the same request, query, and
// discovery documents as the command line; a session adds only the
// envelope, so a client gains no operation and loses none.
//
//   {"kind":"authoring.hello","id":1,"surfaceGeneration":1}
//   {"kind":"authoring.apply","id":2,"scene":"level.scene","request":{...}}
//   {"kind":"authoring.read","id":3,"scene":"level.scene","queries":{...}}
//   {"kind":"authoring.undo","id":4,"scene":"level.scene","expects":"sha256:..."}
//   {"kind":"authoring.redo","id":5,"scene":"level.scene"}
//   {"kind":"authoring.select","id":6,"scene":"level.scene","entities":["...", ...]}
//   {"kind":"authoring.describe","id":7}
//   {"kind":"authoring.apply_together","id":8,"documents":[
//     {"scene":"level.scene","request":{...}},{"scene":"hall.scene","request":{...}}]}
//   {"kind":"authoring.cook","id":9,"output":"/games/plaza-content","cache":null}
//   {"kind":"authoring.cancel","id":10,"operation":9}
//   {"kind":"authoring.import","id":12,"source":"/home/me/art/bark.png","as":"art/bark.png"}
//   {"kind":"authoring.end","id":11}
//
// `hello` comes first and names the surface generation the client speaks;
// before any public stability promise only the tool's own is accepted
// (the capability exchange, exact match). `id` is the client's, any
// JSON value, given back in the reply; `scene` is a scene's path under the
// game's directory, or for apply, read, undo, redo, and history a style
// sheet's (`.styles`); `expects`, on undo and redo, is the
// generation the client last saw, as a request's is. `dryRun`, on apply,
// is the DryRun: the request runs on a copy of the document
// and is answered as it would be kept, nothing kept, written, or entered
// in its history. `select` chooses the
// scene's entities by their SourceEntityIds, in place of what was chosen:
// no change to the document, but what undo and redo put back with it.
// `apply_together` is the multi-document transaction:
// each part an atomic request to its scene, all kept or none, each
// scene's part one entry in its own history. `cook` is a
// long-running operation: it cooks the game's directory into
// `output` (an absolute path, outside it), reusing what `cache` holds if it
// names one, and answers when it ends, other records answered meanwhile,
// telling how far it is as it goes:
//
//   {"kind":"authoring.progress","id":9,"step":3,"steps":21,"source":"gate.scene"}
//
// `cancel` asks the operation the client gave `operation` as its id to
// stop; that operation still answers, cancelled. `import` copies a file
// from outside (an absolute `source`) into the game's directory at `as`,
// gives it a sidecar, and declares it in the game's description.
// `cooked_file` reads back a piece of a file a cook in the session wrote,
// its manifest or a locator the manifest gives, from `offset`, as Base64
// with the file's size, so a cook is brought to another machine:
//
//   {"kind":"authoring.cooked_file","id":17,"output":"/games/plaza-content","file":"content.manifest","offset":0}
//
// Several authors share a session: a change, undo, or redo may
// name its `author`, and an author undoes only their own; each says
// what they have chosen and where they look, and may claim a document or
// an entity in it, so that another's change to it is refused, or a
// node of a UI document, by its component, and what the node holds:
//
//   {"kind":"authoring.present","id":13,"author":"ada","scene":"level.scene","entities":[...],"view":null}
//   {"kind":"authoring.claim","id":14,"author":"ada","scene":"level.scene","entity":"..."}
//   {"kind":"authoring.release","id":15,"author":"ada","scene":"level.scene","entity":null}
//   {"kind":"authoring.claim","id":16,"author":"ada","scene":"hud.ui","node":"game.hud"}
//   {"kind":"authoring.presence","id":17}
//   {"kind":"authoring.leave","id":18,"author":"ada"}
//
// Every reply is
//
//   {"kind":"authoring.reply","id":...,"answer":{...}}
//
// or, for a record that could not be run at all,
//
//   {"kind":"authoring.reply","id":...,"error":{<the one error record>}}
//
// or, for an operation stopped as asked, which is no error (after
// the outcomes),
//
//   {"kind":"authoring.reply","id":...,"cancelled":{"reason":"requested"}}

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

/// The limit point for one record's bytes, in either direction.
inline constexpr std::size_t kMaximumSessionRecordBytes = std::size_t{16} * 1024 * 1024;

enum class SessionVerb : std::uint8_t {
    Hello,
    Describe,
    Apply,
    Read,
    Undo,
    Redo,
    Select,
    View,
    Preview,
    /// A new, empty scene and its sidecar, at a path under the root that
    /// holds neither.
    CreateScene,
    /// A scene's history, oldest first, each entry summed up.
    History,
    /// The assets the game declares, each by its kind, identity, and name.
    Assets,
    /// What the author last clicked in a scene's preview, if anything new.
    Pick,
    /// What a ray in the scene's previewed World meets, asked of its server
    /// as a pick is: where something dropped there rests.
    Cast,
    /// A point the scene's preview marks, where the author chose
    /// something, or none.
    Mark,
    /// What an author did in an editor's viewport showing the scene's
    /// preview, handed to it as its own window's.
    PreviewInput,
    /// The layers the scene's preview leaves out.
    HideLayers,
    /// The scene's view turned on what is chosen in it, from where it
    /// looked, and near enough to hold it all.
    Frame,
    /// How the scene's preview is drawn, by the view mode's name.
    ViewMode,
    /// A view kept in one of the scene's nine bookmarks, or one cleared,
    /// in the scene's editor state.
    Bookmark,
    /// The scene's bookmarks.
    Bookmarks,
    /// The scene's preview split into one to four views.
    Split,
    /// What each author has chosen, a box about each in the preview, in
    /// their color.
    Outline,
    /// What the scene's preview draws over it, by the overlays' names.
    Overlays,
    /// A source's import settings and the settings its importer offers;
    /// and settings given it, its importer's to accept.
    ImportSettings,
    SetImportSettings,
    /// A source's thumbnail from the session's last cook.
    Thumbnail,
    /// A mesh source's box, its least and most corner in its own space, as
    /// the session's last cook made it.
    MeshBounds,
    /// What a source uses and what uses it, by the last cook.
    AssetReferences,
    /// A file or folder of the game's moved with its sidecar and the
    /// description's lines naming it, one removed, and a folder made.
    MoveFile,
    RemoveFile,
    MakeFolder,
    /// The previewed game's World paused, let run, or asked.
    Run,
    /// The paused World stepped some ticks.
    Step,
    /// The played game's server's traffic so far.
    Traffic,
    /// What the played game's World's last ticks took, each tick and each
    /// system, read on its server.
    Profile,
    /// What the frame the scene's preview last drew holds, its steps when
    /// asked, and kept as a capture file when one is named.
    DrawnFrame,
    /// The played game's living entities, a page, and one of them, read on
    /// its server.
    LiveEntities,
    LiveEntity,
    /// One atomic request over several scenes, one transaction.
    ApplyTogether,
    /// The game's directory cooked, as a long-running operation.
    Cook,
    /// A running operation asked to stop.
    Cancel,
    /// A file of a cook's output read back a piece at a time: its manifest
    /// or a file the manifest locates, so what was cooked elsewhere is
    /// played here.
    CookedFile,
    /// A file from outside made one of the game's assets.
    Import,
    /// The game's Studio extensions and the commands each offers, and one
    /// of their commands run on a scene, what it staged one step.
    Extensions,
    Extension,
    /// An extension's Inspector section filled for one entity, and
    /// its panel for the entities chosen.
    ExtensionSection,
    ExtensionPanel,
    /// An author's presence: the document they have open, what they have
    /// chosen in it, and where they look; everyone's, with their claims; a
    /// claim on a document or an entity in it, and one let go; and an
    /// author leaving, their presence and claims gone.
    Present,
    Presence,
    Claim,
    Release,
    Leave,
    End,
};

/// One author's outline asked of a scene's preview: their color,
/// red, green, and blue from nought to one, and the entities they chose.
struct SessionOutline {
    std::array<double, 3> color{};
    std::vector<base::Bits128> entities;
};

/// A scene's part of `apply_together`: its path and its atomic request.
struct SessionPart {
    std::string scene;
    Request request;
};

/// Where a preview's tooling endpoint is: its address, and the files
/// holding the certificate fingerprint to trust and the token to say.
struct PreviewTarget {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
    /// The server the previewed game plays on, if picking is wanted:
    /// its tooling endpoint and the fingerprint to trust, the token
    /// the preview's.
    std::optional<std::string> serverEndpoint;
    std::optional<std::string> serverPinFile;
    /// Whether the game is only observed as it plays: seen through its own
    /// camera, never the scene's view.
    bool observe = false;
};

/// One record from a client, read whole.
struct SessionRecord {
    SessionVerb verb = SessionVerb::Hello;
    /// The client's id for it; null when it gave none.
    document::Value id;
    /// Who makes the change, undo, or redo it asks for:
    /// a collaborator's or agent's name, empty for the session's own.
    /// Apply, apply_together, undo, redo, and extension may name one;
    /// present, claim, release, and leave must.
    std::string author;
    /// The scene's path under the game's directory, for apply, read, undo,
    /// redo, and the verbs naming a scene.
    std::string scene;
    /// Hello's surface generation.
    std::uint32_t surfaceGeneration = 0;
    /// Undo's and redo's.
    std::optional<std::string> expects;
    /// Apply's, and whether it runs dry.
    Request request;
    bool dryRun = false;
    /// Read's.
    std::vector<Query> queries;
    /// Select's, as given; present's, chosen; a claim's entity, none for
    /// the whole document.
    std::vector<base::Bits128> entities;
    /// Present's nodes chosen, in a UI document, by their components; a
    /// claim's node, its one.
    std::vector<std::string> nodes;
    /// View's; present's, where the author looks, none for nowhere.
    SceneView view;
    bool looking = false;
    /// Preview's; none to let the preview go.
    std::optional<PreviewTarget> preview;
    /// Mark's: the point, or none; an extension's mode's point,
    /// where it runs.
    std::optional<std::array<double, 3>> mark;
    /// Cast's ray: its origin, and how far and which way it goes from there.
    std::array<double, 3> origin{};
    std::array<double, 3> toward{};
    /// Apply's `amends`: its change folds into the author's latest, as a
    /// drag's steps shown as it goes do.
    bool amends = false;
    /// Mark's handles, a bit each, 1 its axes and 2 its ring, by
    /// `handles`' name: none, axes, ring, or all, all unless named.
    std::uint8_t handles = 3;
    /// `preview_input`'s events, as the record gave them, which the
    /// session reads as tooling input before it hands them on.
    document::Value events;
    /// `hide_layers`' layers, each above nought, at most 64.
    std::vector<std::uint32_t> layers;
    /// `view_mode`'s name, at most 32 bytes.
    std::string mode;
    /// `bookmark`'s slot, 1 to 9, and whether it clears the slot in place
    /// of keeping `view` there.
    std::uint8_t slot = 0;
    bool clears = false;
    /// `split`'s views: 1 to 4.
    std::uint8_t views = 1;
    /// `overlays`' names, at most 8 of at most 32 bytes.
    std::vector<std::string> overlays;
    /// `outline`'s, at most 16 authors' of at most 64 entities.
    std::vector<SessionOutline> outlines;
    /// Apply together's, two or more, each scene named once.
    std::vector<SessionPart> parts;
    /// Cook's: where it cooks to, and the cache it reuses, if any;
    /// cooked_file's, the output it reads from.
    std::string output;
    std::optional<std::string> cache;
    /// Cooked file's: the file, `content.manifest` or a locator its
    /// manifest gives, and where in it to read from.
    std::string file;
    std::uint64_t offset = 0;
    /// Cancel's: the id the operation to stop was given.
    document::Value operation;
    /// Import's: the file, and where under the game's directory it goes;
    /// a move's `as` where its `asset` goes.
    std::string source;
    std::string as;
    /// Extension's: the extension, by its path under the game's directory,
    /// and its command or mode, or its panel's, by name; the entities
    /// chosen are `entities`.
    std::string extension;
    std::string command;
    /// Extension section's: the component its section is of, by the name
    /// the scene records it by; the entity is `entities`' one.
    std::string component;
    /// Import settings': the source's path under the game's directory, and
    /// the settings given it, an object; the file moved or removed,
    /// or the folder made.
    std::string asset;
    document::Value settings;
    /// Thumbnail's: the most pixels on its longer side, 16 to 256.
    std::uint32_t size = 64;
    /// Run's: whether to pause, none to ask only; step's ticks, 1 to 600.
    std::optional<bool> paused;
    std::uint32_t ticks = 1;
    /// A drawn frame's: whether its steps are given, and the capture file
    /// it is kept in, a path under the game's directory, empty for none;
    /// a profile's capture file too.
    bool steps = false;
    std::string capture;
    /// Live entities' and live entity's: what is handed on to the server,
    /// `after`, `component`, `limit`, and `entity`, as given.
    document::Value query;
};

/// Reads one record; refuses (`ValidationFailed`) anything out of the form
/// above, and passes on what its request or query document refuses. The
/// id read before a refusal, if any, is left in `idRead` so its reply can
/// name it.
[[nodiscard]] result::Result<SessionRecord> readSessionRecord(std::string_view line, document::Value& idRead);

/// A view as records give it (`view`, `bookmark`, `present`, and the
/// replies that say where a scene is looked from): an eye and a target of
/// three numbers each and a field of view; none for anything else. Its
/// ranges are the scene's to check.
[[nodiscard]] std::optional<SceneView> readSceneView(const document::Value& view);

/// A reply line, its line feed included.
[[nodiscard]] std::string writeReply(const document::Value& id, document::Value answer);

/// A refusal's reply line, its line feed included.
[[nodiscard]] std::string writeRefusal(const document::Value& id, const result::Error& error);

/// A cancelled operation's reply line, its line feed included: stopped as
/// its client asked (`requested`), or as its session ended (`ended`).
[[nodiscard]] std::string writeCancelled(const document::Value& id, std::string_view reason);

} // namespace rawframe::authoring
