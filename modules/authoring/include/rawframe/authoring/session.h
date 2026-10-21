#pragma once

// An authoring session (SPEC-0040's remote projection, D407): one record a
// line in each direction, each the compact form of a strict JSON object
// (SPEC-0028's hash-input form, so no line holds a line feed), between a
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
// (SPEC-0040's capability exchange, exact match). `id` is the client's, any
// JSON value, given back in the reply; `scene` is a scene's path under the
// game's directory, or for apply, read, undo, redo, and history a style
// sheet's (`.styles`, D636, D638); `expects`, on undo and redo, is the
// generation the client last saw, as a request's is. `dryRun`, on apply,
// is SPEC-0040's DryRun (D650): the request runs on a copy of the document
// and is answered as it would be kept, nothing kept, written, or entered
// in its history. `select` chooses the
// scene's entities by their SourceEntityIds, in place of what was chosen:
// no change to the document, but what undo and redo put back with it
// (D417). `apply_together` is SPEC-0040's multi-document transaction
// (D497): each part an atomic request to its scene, all kept or none, each
// scene's part one entry in its own history. `cook` is SPEC-0040's
// long-running operation (D502): it cooks the game's directory into
// `output` (an absolute path, outside it), reusing what `cache` holds if it
// names one, and answers when it ends, other records answered meanwhile,
// telling how far it is as it goes:
//
//   {"kind":"authoring.progress","id":9,"step":3,"steps":21,"source":"gate.scene"}
//
// `cancel` asks the operation the client gave `operation` as its id to
// stop; that operation still answers, cancelled. `import` copies a file
// from outside (an absolute `source`) into the game's directory at `as`,
// gives it a sidecar, and declares it in the game's description (D503).
// `cooked_file` reads back a piece of a file a cook in the session wrote,
// its manifest or a locator the manifest gives, from `offset`, as Base64
// with the file's size (D905), so a cook is brought to another machine:
//
//   {"kind":"authoring.cooked_file","id":17,"output":"/games/plaza-content","file":"content.manifest","offset":0}
//
// Several authors share a session (ADR-0085): a change, undo, or redo may
// name its `author`, and an author undoes only their own (D879); each says
// what they have chosen and where they look, and may claim a document or
// an entity in it, so that another's change to it is refused (D880), or a
// node of a UI document, by its component, and what the node holds (D886):
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
// or, for an operation stopped as asked, which is no error (SPEC-0040, after
// ADR-0010's outcomes),
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

/// SPEC-0040's limit point for one record's bytes, in either direction.
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
    /// holds neither (D449).
    CreateScene,
    /// A scene's history, oldest first, each entry summed up (D454).
    History,
    /// The assets the game declares, each by its kind, identity, and name
    /// (D455).
    Assets,
    /// What the author last clicked in a scene's preview, if anything new
    /// (D456).
    Pick,
    /// A point the scene's preview marks, where the author chose
    /// something, or none (D464).
    Mark,
    /// What an author did in an editor's viewport showing the scene's
    /// preview, handed to it as its own window's (D790).
    PreviewInput,
    /// The layers the scene's preview leaves out (D791).
    HideLayers,
    /// The scene's view turned on what is chosen in it, from where it
    /// looked, and near enough to hold it all (D792).
    Frame,
    /// How the scene's preview is drawn, by the view mode's name (D794).
    ViewMode,
    /// A view kept in one of the scene's nine bookmarks, or one cleared,
    /// in the scene's editor state (D796).
    Bookmark,
    /// The scene's bookmarks.
    Bookmarks,
    /// The scene's preview split into one, two, or three views (D797).
    Split,
    /// What each author has chosen, a box about each in the preview, in
    /// their color (D907).
    Outline,
    /// What the scene's preview draws over it, by the overlays' names
    /// (D798).
    Overlays,
    /// A source's import settings and the settings its importer offers
    /// (D800); and settings given it, its importer's to accept.
    ImportSettings,
    SetImportSettings,
    /// A source's thumbnail from the session's last cook (D801).
    Thumbnail,
    /// What a source uses and what uses it, by the last cook (D803).
    AssetReferences,
    /// A file or folder of the game's moved with its sidecar and the
    /// description's lines naming it, one removed, and a folder made
    /// (D929).
    MoveFile,
    RemoveFile,
    MakeFolder,
    /// The previewed game's World paused, let run, or asked (D806).
    Run,
    /// The paused World stepped some ticks (D806).
    Step,
    /// The played game's server's traffic so far (D807).
    Traffic,
    /// What the played game's World's last ticks took, each tick and each
    /// system, read on its server (D838).
    Profile,
    /// What the frame the scene's preview last drew holds, its steps when
    /// asked, and kept as a capture file when one is named (D842).
    DrawnFrame,
    /// The played game's living entities, a page, and one of them, read on
    /// its server (D808).
    LiveEntities,
    LiveEntity,
    /// One atomic request over several scenes, one transaction (D497).
    ApplyTogether,
    /// The game's directory cooked, as a long-running operation (D502).
    Cook,
    /// A running operation asked to stop (D502).
    Cancel,
    /// A file of a cook's output read back a piece at a time: its manifest
    /// or a file the manifest locates, so what was cooked elsewhere is
    /// played here (D905).
    CookedFile,
    /// A file from outside made one of the game's assets (D503).
    Import,
    /// The game's Studio extensions and the commands each offers, and one
    /// of their commands run on a scene, what it staged one step (D874).
    Extensions,
    Extension,
    /// An extension's Inspector section filled for one entity (D875), and
    /// its panel for the entities chosen (D878).
    ExtensionSection,
    ExtensionPanel,
    /// An author's presence: the document they have open, what they have
    /// chosen in it, and where they look; everyone's, with their claims; a
    /// claim on a document or an entity in it, and one let go; and an
    /// author leaving, their presence and claims gone (ADR-0085, D880).
    Present,
    Presence,
    Claim,
    Release,
    Leave,
    End,
};

/// One author's outline asked of a scene's preview (D907): their color,
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

/// Where a preview's tooling endpoint is (D433): its address, and the files
/// holding the certificate fingerprint to trust and the token to say.
struct PreviewTarget {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
    /// The server the previewed game plays on, if picking is wanted
    /// (D456): its tooling endpoint and the fingerprint to trust, the token
    /// the preview's.
    std::optional<std::string> serverEndpoint;
    std::optional<std::string> serverPinFile;
    /// Whether the game is only observed as it plays: seen through its own
    /// camera, never the scene's view (D923).
    bool observe = false;
};

/// One record from a client, read whole.
struct SessionRecord {
    SessionVerb verb = SessionVerb::Hello;
    /// The client's id for it; null when it gave none.
    document::Value id;
    /// Who makes the change, undo, or redo it asks for (ADR-0085, D879):
    /// a collaborator's or agent's name, empty for the session's own.
    /// Apply, apply_together, undo, redo, and extension may name one;
    /// present, claim, release, and leave must (D880).
    std::string author;
    /// The scene's path under the game's directory, for apply, read, undo,
    /// redo, and the verbs naming a scene.
    std::string scene;
    /// Hello's surface generation.
    std::uint32_t surfaceGeneration = 0;
    /// Undo's and redo's.
    std::optional<std::string> expects;
    /// Apply's, and whether it runs dry (D650).
    Request request;
    bool dryRun = false;
    /// Read's.
    std::vector<Query> queries;
    /// Select's, as given; present's, chosen; a claim's entity, none for
    /// the whole document.
    std::vector<base::Bits128> entities;
    /// Present's nodes chosen, in a UI document, by their components; a
    /// claim's node, its one (D886).
    std::vector<std::string> nodes;
    /// View's (D432); present's, where the author looks, none for nowhere.
    SceneView view;
    bool looking = false;
    /// Preview's; none to let the preview go (D433).
    std::optional<PreviewTarget> preview;
    /// Mark's: the point, or none (D464); an extension's mode's point,
    /// where it runs (D877).
    std::optional<std::array<double, 3>> mark;
    /// Apply's `amends`: its change folds into the author's latest, as a
    /// drag's steps shown as it goes do (D976).
    bool amends = false;
    /// Mark's handles (D974), a bit each, 1 its axes and 2 its ring, by
    /// `handles`' name: none, axes, ring, or all, all unless named.
    std::uint8_t handles = 3;
    /// `preview_input`'s events, as the record gave them, which the
    /// session reads as tooling input before it hands them on.
    document::Value events;
    /// `hide_layers`' layers, each above nought, at most 64.
    std::vector<std::uint32_t> layers;
    /// `view_mode`'s name, at most 32 bytes (D794).
    std::string mode;
    /// `bookmark`'s slot, 1 to 9, and whether it clears the slot in place
    /// of keeping `view` there (D796).
    std::uint8_t slot = 0;
    bool clears = false;
    /// `split`'s views: 1, 2, or 3 (D797).
    std::uint8_t views = 1;
    /// `overlays`' names, at most 8 of at most 32 bytes (D798).
    std::vector<std::string> overlays;
    /// `outline`'s, at most 16 authors' of at most 64 entities (D907).
    std::vector<SessionOutline> outlines;
    /// Apply together's, two or more, each scene named once (D497).
    std::vector<SessionPart> parts;
    /// Cook's: where it cooks to, and the cache it reuses, if any (D502);
    /// cooked_file's, the output it reads from.
    std::string output;
    std::optional<std::string> cache;
    /// Cooked file's: the file, `content.manifest` or a locator its
    /// manifest gives, and where in it to read from (D905).
    std::string file;
    std::uint64_t offset = 0;
    /// Cancel's: the id the operation to stop was given.
    document::Value operation;
    /// Import's: the file, and where under the game's directory it goes;
    /// a move's `as` where its `asset` goes (D929).
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
    /// the settings given it, an object (D800); the file moved or removed,
    /// or the folder made (D929).
    std::string asset;
    document::Value settings;
    /// Thumbnail's: the most pixels on its longer side, 16 to 256 (D801).
    std::uint32_t size = 64;
    /// Run's: whether to pause, none to ask only; step's ticks, 1 to 600
    /// (D806).
    std::optional<bool> paused;
    std::uint32_t ticks = 1;
    /// A drawn frame's: whether its steps are given, and the capture file
    /// it is kept in, a path under the game's directory, empty for none
    /// (D842); a profile's capture file too (D844).
    bool steps = false;
    std::string capture;
    /// Live entities' and live entity's: what is handed on to the server,
    /// `after`, `component`, `limit`, and `entity`, as given (D808).
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
