#pragma once

// A game's UI document open for authoring: a `ui.document`
// (rawframe/ui/document.h), a JsonDocument whose slots are a node,
// known by its component's name wherever it stands (`nodes`, or a node's
// `children`), whole at its place; and one of its fields. Its operations
// are `ui.set_field`, `ui.create_node`, `ui.remove_node`, and
// `ui.move_node`, and its query `ui.read_document`, in operations.h.

#include "rawframe/authoring/json_document.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/document.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

class AuthoredUi {
public:
    /// The UI document `text`, of resource identity `document`, at
    /// generation nought and clean. Refuses (`ValidationFailed`) a text that
    /// does not read as a UI document.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredUi>>
    open(base::Bits128 document, std::string_view text, const HistoryLimits& limits = {});

    AuthoredUi(AuthoredUi&&) = delete;
    AuthoredUi& operator=(AuthoredUi&&) = delete;
    AuthoredUi(const AuthoredUi&) = delete;
    AuthoredUi& operator=(const AuthoredUi&) = delete;
    ~AuthoredUi() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return document_.generation();
    }
    /// The document as it reads now.
    [[nodiscard]] const ui::UiDocument& read() const noexcept {
        return read_;
    }
    /// The document now, and its bytes.
    [[nodiscard]] const document::Value& documentValue() const noexcept {
        return document_.root();
    }
    [[nodiscard]] std::string text() const {
        return document_.text();
    }
    /// The path to node `node`, wherever it stands; none where the document
    /// has no such node.
    [[nodiscard]] std::optional<JsonPath> pathOf(std::string_view node) const;

    /// The AtomicBatch over the document, as a style sheet's
    /// (AuthoredStyles::change): (`TargetNotFound`) a node or parent the
    /// document does not have, (`Conflict`) a new node's component already
    /// a node's, (`ValidationFailed`) a field a node has not, a place past
    /// its siblings, an operation that is not a UI document's, or a
    /// document that does not read after them.
    [[nodiscard]] result::Result<Committed> change(std::uint64_t generation, std::span<const Operation> operations);

    /// Who changes, undoes, or redoes next.
    void setAuthor(std::uint32_t author) noexcept {
        document_.setAuthor(author);
    }
    [[nodiscard]] bool canUndo() const noexcept {
        return document_.canUndo();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return document_.canRedo();
    }
    [[nodiscard]] result::Result<Committed> undo(std::uint64_t generation);
    [[nodiscard]] result::Result<Committed> redo(std::uint64_t generation);

    [[nodiscard]] bool dirty() const noexcept {
        return document_.dirty();
    }
    void markSaved() noexcept {
        document_.markSaved();
    }
    [[nodiscard]] std::size_t undoable() const noexcept {
        return document_.undoable();
    }
    [[nodiscard]] std::size_t redoable() const noexcept {
        return document_.redoable();
    }
    /// Who made the history's entry at `entry`, and what it is.
    [[nodiscard]] const Authorship& authorshipAt(std::size_t entry) const noexcept {
        return document_.authorshipAt(entry);
    }
    [[nodiscard]] const std::vector<JsonDelta>& journalAt(std::size_t entry) const noexcept {
        return document_.journalAt(entry);
    }
    /// The entry the author's undo or redo would take back or put back, if
    /// any.
    [[nodiscard]] std::optional<std::size_t> stepTarget(bool undo) const noexcept {
        return document_.stepTarget(undo);
    }
    /// What the history's entry at `entry` does, in a line: its first
    /// delta, as `set fill of game.hud`, and how many more there are.
    [[nodiscard]] std::string summaryAt(std::size_t entry) const;

private:
    AuthoredUi(base::Bits128 identity, JsonDocument document, ui::UiDocument read)
        : identity_(identity), document_(std::move(document)), read_(std::move(read)) {
    }

    [[nodiscard]] result::Result<Committed> reread(result::Result<Committed> done);
    /// `move` staged: the node taken out, then put where it goes.
    [[nodiscard]] result::Status stageMove(JsonDocument::Staged& staged, const MoveUiNode& move) const;

    base::Bits128 identity_{};
    JsonDocument document_;
    ui::UiDocument read_;
};

/// A UI document's query answered (`ui.read_document`: `{"document": ...}`,
/// the document whole); refuses (`ValidationFailed`) any other.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredUi& document, const Query& query);

} // namespace rawframe::authoring
