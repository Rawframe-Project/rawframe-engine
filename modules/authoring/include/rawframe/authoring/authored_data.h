#pragma once

// A game's data document open for authoring (D811): an action map, a
// mixer, a string table or its translations, or an animation graph or
// clip, each canonical JSON its own module reads. A JsonDocument (D640)
// whose members keep the order they stand in, a new one last, and whose
// every change is read by the kind's own reader before it is kept, so a
// document never holds what its game would refuse. Its operation is
// `data.set` (a value at a path, or none to take it away) and its query
// `data.read`.
//
// A path is members by name apart by dots, an item of an array known by a
// member of its own as `member[key=id]`: `actions[actionId=2b8c866778ceb662].displayName`.
// An item the array does not hold yet is added last; an array whose items
// are known by nothing is set whole.

#include "rawframe/authoring/history.h"
#include "rawframe/authoring/json_document.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

/// The path `text` names, refused (`ValidationFailed`) where it is empty,
/// past 32 steps, or out of the form above.
[[nodiscard]] result::Result<JsonPath> dataPathOf(std::string_view text);

class AuthoredData {
public:
    /// The document `text`, of resource identity `document`, at generation
    /// nought and clean; refused (`ValidationFailed`) where `reads` does
    /// not read it.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredData>>
    open(base::Bits128 document, std::string_view text, DocumentReader reads, const HistoryLimits& limits = {});

    AuthoredData(AuthoredData&&) = delete;
    AuthoredData& operator=(AuthoredData&&) = delete;
    AuthoredData(const AuthoredData&) = delete;
    AuthoredData& operator=(const AuthoredData&) = delete;
    ~AuthoredData() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return document_.generation();
    }
    [[nodiscard]] DocumentReader reader() const noexcept {
        return reads_;
    }
    [[nodiscard]] const document::Value& documentValue() const noexcept {
        return document_.root();
    }
    [[nodiscard]] std::string text() const {
        return document_.text();
    }

    /// SPEC-0040's AtomicBatch over the document: each `data.set` in order
    /// against `generation`, one entry in the history together. Any refused
    /// keeps nothing, naming its `index`: (`TargetStale`) another
    /// generation, (`TargetNotFound`) a path through an item the document
    /// does not hold, (`ValidationFailed`) a path out of form, an operation
    /// that is not `data.set`, or a document its reader refuses after them,
    /// and (`LimitExceeded`) more than a batch's operations.
    [[nodiscard]] result::Result<Committed> change(std::uint64_t generation, std::span<const Operation> operations);

    [[nodiscard]] bool canUndo() const noexcept {
        return document_.canUndo();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return document_.canRedo();
    }
    [[nodiscard]] result::Result<Committed> undo(std::uint64_t generation) {
        return document_.undo(generation);
    }
    [[nodiscard]] result::Result<Committed> redo(std::uint64_t generation) {
        return document_.redo(generation);
    }

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
    [[nodiscard]] const std::vector<JsonDelta>& journalAt(std::size_t entry) const noexcept {
        return document_.journalAt(entry);
    }
    /// What the history's entry at `entry` does, as `set actions[2b8c...].displayName`
    /// and how many more.
    [[nodiscard]] std::string summaryAt(std::size_t entry) const;

private:
    AuthoredData(base::Bits128 identity, JsonDocument document, DocumentReader reads)
        : identity_(identity), document_(std::move(document)), reads_(reads) {
    }

    base::Bits128 identity_{};
    JsonDocument document_;
    DocumentReader reads_ = nullptr;
};

/// A data document's query answered (`data.read`: `{"data": ...}`, the
/// document whole); refuses (`ValidationFailed`) any other.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredData& data, const Query& query);

} // namespace rawframe::authoring
