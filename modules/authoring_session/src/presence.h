#pragma once

// Presence and claims: who is in a
// session, what each has chosen and where each is looking, and what each
// has claimed. They are the session's own records, never in a document or
// its history, and gone when the author leaves. A claim is soft: a whole
// document, an entity in a scene, or a node in a UI document and what it
// holds, held by one author, so that a change another author's
// record asks of it is refused until it is released.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/delta.h"
#include "rawframe/authoring/json_document.h"
#include "rawframe/authoring/session.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// The most authors present at once, entities or nodes one presence
/// names, and claims a session holds.
inline constexpr std::size_t kMostPresent = 1024;
inline constexpr std::size_t kMostChosen = 1024;
inline constexpr std::size_t kMostClaims = 4096;

/// What an author has chosen and where they are looking: the document
/// they have open, by its path under the root, the entities, or a UI
/// document's nodes, chosen in it, and the view, where they look at a
/// scene.
struct Presence {
    std::string author;
    std::string document;
    std::vector<std::string> chosen;
    std::optional<authoring::SceneView> view;
};

/// A document, by its path under the root, and in it an entity, or a node
/// by its component, or the whole document where neither is named.
struct Claim {
    std::string author;
    std::string document;
    std::optional<std::string> entity;
    std::optional<std::string> node;
};

/// The document `name` is as presence and claims know it: its path under
/// `root`, however a record spelled it. Refuses (`TargetNotFound`) a name
/// that is not a file under the root.
[[nodiscard]] result::Result<std::string> documentKey(const std::filesystem::path& root, const std::string& name);

/// The entities a journal changes, by their ids, each once.
[[nodiscard]] std::vector<std::string> entitiesOf(const authoring::Journal& journal);
/// The nodes a UI document's journal changes, by their components, each
/// once: every node a slot's path passes through, and every node in one
/// that comes or goes whole.
[[nodiscard]] std::vector<std::string> nodesOf(std::span<const authoring::JsonDelta> journal);

class Presences {
public:
    /// The answer to a present, presence, claim, release, or leave record,
    /// its documents under `root`.
    [[nodiscard]] result::Result<document::Value> answer(const authoring::SessionRecord& record,
                                                         const std::filesystem::path& root);

    /// An author's presence, in place of what it was. Refuses
    /// (`LimitExceeded`) a presence past the limits above.
    [[nodiscard]] result::Status present(Presence presence);
    /// A claim kept. Refuses (`Conflict`) one another author holds, whole
    /// or in part: an entity or node of a document another holds whole, a
    /// whole document another holds a part of, naming the holder; and
    /// (`LimitExceeded`) a claim past the session's limit. A node and one
    /// it holds are claimed apart, each refusing the other's holder.
    [[nodiscard]] result::Status claim(Claim claim);
    /// A claim let go. Refuses (`TargetNotFound`) one its author does not
    /// hold.
    [[nodiscard]] result::Status release(const Claim& claim);
    /// Everything of the author's let go.
    void leave(std::string_view author);

    /// Refuses (`Conflict`, naming the holder) a change by `author` to
    /// `document` where another author holds it whole, or holds one of the
    /// entities or nodes, `parts`, it touches.
    [[nodiscard]] result::Status unclaimed(std::string_view author, std::string_view document) const;
    [[nodiscard]] result::Status
    untouched(std::string_view author, std::string_view document, std::span<const std::string> parts) const;
    /// Whether an author other than `author` holds an entity or a node of
    /// `document`, so that a change to it must say what it touches.
    [[nodiscard]] bool othersHoldParts(std::string_view author, std::string_view document) const;

    /// Every presence and claim, as `authoring.presence` answers them.
    [[nodiscard]] document::Value value() const;

private:
    std::vector<Presence> present_;
    std::vector<Claim> claims_;
};

} // namespace rawframe::authoring_session
