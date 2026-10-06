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
//   {"kind":"authoring.end","id":8}
//
// `hello` comes first and names the surface generation the client speaks;
// before any public stability promise only the tool's own is accepted
// (SPEC-0040's capability exchange, exact match). `id` is the client's, any
// JSON value, given back in the reply; `scene` is a scene's path under the
// game's directory; `expects`, on undo and redo, is the generation the
// client last saw, as a request's is. `select` chooses the scene's entities
// by their SourceEntityIds, in place of what was chosen: no change to the
// document, but what undo and redo put back with it (D417). Every reply is
//
//   {"kind":"authoring.reply","id":...,"answer":{...}}
//
// or, for a record that could not be run at all,
//
//   {"kind":"authoring.reply","id":...,"error":{<the one error record>}}

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

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
    End,
};

/// Where a preview's tooling endpoint is (D433): its address, and the files
/// holding the certificate fingerprint to trust and the token to say.
struct PreviewTarget {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
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

} // namespace rawframe::authoring
