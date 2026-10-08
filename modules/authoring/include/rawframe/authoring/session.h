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
// game's directory; `expects`, on undo and redo, is the generation the
// client last saw, as a request's is. `select` chooses the scene's entities
// by their SourceEntityIds, in place of what was chosen: no change to the
// document, but what undo and redo put back with it (D417). `apply_together`
// is SPEC-0040's multi-document transaction (D497): each part an atomic
// request to its scene, all kept or none, each scene's part one entry in
// its own history. `cook` is SPEC-0040's long-running operation (D502): it
// cooks the game's directory into `output` (an absolute path, outside it),
// reusing what `cache` holds if it names one, and answers when it ends,
// other records answered meanwhile, telling how far it is as it goes:
//
//   {"kind":"authoring.progress","id":9,"step":3,"steps":21,"source":"gate.scene"}
//
// `cancel` asks the operation the client gave `operation` as its id to
// stop; that operation still answers, cancelled. `import` copies a file
// from outside (an absolute `source`) into the game's directory at `as`,
// gives it a sidecar, and declares it in the game's description (D503).
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
    /// One atomic request over several scenes, one transaction (D497).
    ApplyTogether,
    /// The game's directory cooked, as a long-running operation (D502).
    Cook,
    /// A running operation asked to stop (D502).
    Cancel,
    /// A file from outside made one of the game's assets (D503).
    Import,
    End,
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
};

/// One record from a client, read whole.
struct SessionRecord {
    SessionVerb verb = SessionVerb::Hello;
    /// The client's id for it; null when it gave none.
    document::Value id;
    /// The scene's path under the game's directory, for apply, read, undo,
    /// redo, and the verbs naming a scene.
    std::string scene;
    /// Hello's surface generation.
    std::uint32_t surfaceGeneration = 0;
    /// Undo's and redo's.
    std::optional<std::string> expects;
    /// Apply's.
    Request request;
    /// Read's.
    std::vector<Query> queries;
    /// Select's, as given.
    std::vector<base::Bits128> entities;
    /// View's (D432).
    SceneView view;
    /// Preview's; none to let the preview go (D433).
    std::optional<PreviewTarget> preview;
    /// Mark's: the point, or none (D464).
    std::optional<std::array<double, 3>> mark;
    /// Apply together's, two or more, each scene named once (D497).
    std::vector<SessionPart> parts;
    /// Cook's: where it cooks to, and the cache it reuses, if any (D502).
    std::string output;
    std::optional<std::string> cache;
    /// Cancel's: the id the operation to stop was given.
    document::Value operation;
    /// Import's: the file, and where under the game's directory it goes.
    std::string source;
    std::string as;
};

/// Reads one record; refuses (`ValidationFailed`) anything out of the form
/// above, and passes on what its request or query document refuses. The
/// id read before a refusal, if any, is left in `idRead` so its reply can
/// name it.
[[nodiscard]] result::Result<SessionRecord> readSessionRecord(std::string_view line, document::Value& idRead);

/// A reply line, its line feed included.
[[nodiscard]] std::string writeReply(const document::Value& id, document::Value answer);

/// A refusal's reply line, its line feed included.
[[nodiscard]] std::string writeRefusal(const document::Value& id, const result::Error& error);

/// A cancelled operation's reply line, its line feed included: stopped as
/// its client asked (`requested`), or as its session ended (`ended`).
[[nodiscard]] std::string writeCancelled(const document::Value& id, std::string_view reason);

} // namespace rawframe::authoring
