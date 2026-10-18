#pragma once

// An authored document that is canonical JSON (D640): its value, its
// generation, and its history (history.h), whatever kind it is. A style
// sheet (authored_styles.h) and a UI document (authored_ui.h) are each one
// of these and the operations of their kind. A delta names one slot by a
// path from the root: members by name, and an item of an array by the text
// of a member its items are known by (a class by its `styleId`, a node by
// its `component`); the last step may name the item itself, which comes or
// goes whole at its place. A slot's `before` and `after` are what the
// document holds there, or none.
//
// A change rebuilds the objects on its path with their members in their
// document's order, which the kind gives by the member that holds them,
// and omits an object or array it rebuilt and left empty, as the canonical
// profile omits a field at its default, unless the root holds it. It is kept only where the kind's reader
// reads the whole document, so every generation is in form.

#include "rawframe/authoring/history.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

/// One step of a slot's path: the member `member`, or, with a `key`, the
/// item of that member's array whose `key` holds the text `id`, or, with a
/// `place`, the item at that place of it (an array whose items are known
/// by nothing of their own, D827).
struct JsonStep {
    std::string member;
    std::string key;
    std::string id;
    std::optional<std::size_t> place;
};
using JsonPath = std::vector<JsonStep>;

/// Whether `step` names an item of an array, not a member.
[[nodiscard]] inline bool namesItem(const JsonStep& step) noexcept {
    return !step.key.empty() || step.place.has_value();
}

/// One slot, from what it holds to another; `place` is where an item that
/// comes or goes whole stands among its array's.
struct JsonDelta {
    JsonPath path;
    std::size_t place = 0;
    std::optional<document::Value> before;
    std::optional<document::Value> after;
};

/// The one name of an order that keeps an object's members as they stand,
/// a new one last (a data document's, D811).
inline constexpr std::string_view kMembersAsHeld = "*";

/// An object's members in its document's order, by the path from the root
/// to it (empty for the root, D729); none for an object whose members are
/// names of its own, kept in name order (D721).
using MemberOrder = std::span<const std::string_view> (*)(std::span<const JsonStep> at) noexcept;
/// Whether the object at `at` stays when a change leaves it empty, as its
/// kind holds it even then (a material graph node's params and inputs);
/// without one, a container a change empties goes.
using KeptEmpty = bool (*)(std::span<const JsonStep> at) noexcept;
/// Whether a document's text reads as its kind; the reader's refusal, said.
using DocumentReader = result::Status (*)(std::string_view text);

/// What `root` holds at `path`; none where the path finds nothing.
[[nodiscard]] std::optional<document::Value> slotAt(const document::Value& root, std::span<const JsonStep> path);

/// Where `path`'s item stands among its array's, if it is there.
[[nodiscard]] std::optional<std::size_t> placeAt(const document::Value& root, std::span<const JsonStep> path);

class JsonDocument {
public:
    /// A batch of deltas staged on a copy of the document, kept together.
    struct Staged {
        document::Value root;
        std::vector<JsonDelta> journal;
        std::size_t bytes = 0;
    };

    /// `text` at generation nought and clean; refused (`ValidationFailed`)
    /// where `reads` does not read it.
    [[nodiscard]] static result::Result<JsonDocument> open(std::string_view text,
                                                           MemberOrder order,
                                                           DocumentReader reads,
                                                           const HistoryLimits& limits = {},
                                                           KeptEmpty kept = nullptr);

    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] const document::Value& root() const noexcept {
        return root_;
    }
    [[nodiscard]] std::string text() const;

    /// A batch begun against `generation`; refused (`TargetStale`) for any
    /// other.
    [[nodiscard]] result::Result<Staged> stage(std::uint64_t generation) const;
    /// `delta` applied to `staged` and journaled, nothing where its
    /// `before` is its `after`; refused (`DeltaMismatch`) where its slot
    /// does not hold its `before`, and (`TargetNotFound`) where its path
    /// finds no item it passes through.
    [[nodiscard]] result::Status apply(Staged& staged, JsonDelta delta) const;
    /// `staged` kept as one entry, where it changed anything and reads;
    /// refused (`ValidationFailed`, `LimitExceeded`) otherwise, nothing kept.
    [[nodiscard]] result::Result<Committed> keep(Staged staged);

    [[nodiscard]] bool canUndo() const noexcept {
        return history_.canUndo();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return history_.canRedo();
    }
    /// Who keeps, undoes, or redoes next (ADR-0085, D879): nought, the
    /// session's own, until told.
    void setAuthor(std::uint32_t author) noexcept {
        author_ = author;
    }
    /// Takes back the author's latest change, or puts back what they took
    /// back last, as a scene does (D879). Refuses (`TargetStale`) another
    /// generation, (`Conflict`) where a later change by another author
    /// stands in its way, and (`TargetNotFound`) with nothing to do.
    [[nodiscard]] result::Result<Committed> undo(std::uint64_t generation);
    [[nodiscard]] result::Result<Committed> redo(std::uint64_t generation);

    [[nodiscard]] bool dirty() const noexcept {
        return history_.dirty();
    }
    void markSaved() noexcept {
        history_.markSaved();
    }
    [[nodiscard]] std::size_t undoable() const noexcept {
        return history_.undoable();
    }
    [[nodiscard]] std::size_t redoable() const noexcept {
        return history_.redoable();
    }
    /// The journal of the history's entry at `entry`, oldest first.
    [[nodiscard]] const std::vector<JsonDelta>& journalAt(std::size_t entry) const noexcept {
        return history_.at(entry).journal;
    }
    /// Who made the history's entry at `entry`, and what it is (D879).
    [[nodiscard]] const Authorship& authorshipAt(std::size_t entry) const noexcept {
        return history_.authorshipAt(entry);
    }

private:
    struct Entry {
        std::vector<JsonDelta> journal;
        /// Whether what the entry did is its journal applied backward
        /// (D879).
        bool reversed = false;
        std::size_t bytes = 0;
    };

    JsonDocument(
        document::Value root, MemberOrder order, DocumentReader reads, const HistoryLimits& limits, KeptEmpty kept)
        : root_(std::move(root)), order_(order), kept_(kept), reads_(reads), history_(limits) {
    }

    [[nodiscard]] result::Status applyTo(document::Value& root, const JsonDelta& delta, bool forward) const;
    [[nodiscard]] result::Result<Committed> step(std::uint64_t generation, bool undo);

    document::Value root_;
    MemberOrder order_ = nullptr;
    KeptEmpty kept_ = nullptr;
    DocumentReader reads_ = nullptr;
    std::uint64_t generation_ = 0;
    History<Entry> history_;
    std::uint32_t author_ = 0;
};

} // namespace rawframe::authoring
