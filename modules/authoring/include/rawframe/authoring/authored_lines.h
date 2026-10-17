#pragma once

// A game's line document open for authoring (D817): a game description,
// whose settings are lines of words its own module reads (world_kest's
// `parseGame`), comments and blank lines among them. It is kept as its
// lines; a change sets, takes away, or puts in whole lines by their place
// from nought, and every change is read by the kind's own reader before it
// is kept, so the document never holds what its game would refuse. The
// lines a change does not name stay as they stand, comments with them. Its
// operations are `lines.set` and `lines.insert`, and its query
// `lines.read`.

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

/// The lines a line document's kind makes of an operation of its own, as a
/// terrain's brush stroke (D855); refused as it refuses.
using LineRewriter = result::Result<std::vector<std::string>> (*)(std::span<const std::string> lines,
                                                                  const Operation& operation);

class AuthoredLines {
public:
    /// The document `text`, of resource identity `document`, at generation
    /// nought and clean; refused (`ValidationFailed`) where `reads` does
    /// not read it. Its kind's own operations go to `rewrites`, where given.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredLines>> open(base::Bits128 document,
                                                                             std::string_view text,
                                                                             DocumentReader reads,
                                                                             const HistoryLimits& limits = {},
                                                                             LineRewriter rewrites = nullptr);

    AuthoredLines(AuthoredLines&&) = delete;
    AuthoredLines& operator=(AuthoredLines&&) = delete;
    AuthoredLines(const AuthoredLines&) = delete;
    AuthoredLines& operator=(const AuthoredLines&) = delete;
    ~AuthoredLines() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    /// Its lines, without their line ends.
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept {
        return lines_;
    }
    /// Its lines, each ended by a line feed, as it is written.
    [[nodiscard]] std::string text() const;

    /// SPEC-0040's AtomicBatch over the document: each `lines.set` or
    /// `lines.insert` in order against `generation`, a place counted in the
    /// lines as the operations before it left them, one entry in the
    /// history together. Any refused keeps nothing, naming its `index`:
    /// (`TargetStale`) another generation, (`TargetNotFound`) a place past
    /// the lines, (`ValidationFailed`) a line holding a line end, an
    /// operation of another kind, or a document its reader refuses after
    /// them, and (`LimitExceeded`) more than a batch's operations.
    [[nodiscard]] result::Result<Committed> change(std::uint64_t generation, std::span<const Operation> operations);

    [[nodiscard]] bool canUndo() const noexcept {
        return history_.canUndo();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return history_.canRedo();
    }
    /// The last change taken back, or put back, against `generation`;
    /// (`TargetStale`) another, and (`TargetNotFound`) nothing to step.
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
    /// How many lines the history's entry at `entry` set, took away, or
    /// put in.
    [[nodiscard]] std::size_t deltasAt(std::size_t entry) const noexcept {
        return history_.at(entry).deltas;
    }
    /// What the history's entry at `entry` does, as `set line 12` and how
    /// many more.
    [[nodiscard]] std::string summaryAt(std::size_t entry) const {
        return history_.at(entry).summary;
    }

private:
    /// What a change keeps: the lines before it and after it whole (a
    /// game description is a few kilobytes), and what it said it did.
    struct Entry {
        std::vector<std::string> before;
        std::vector<std::string> after;
        std::size_t deltas = 0;
        std::string summary;
        std::size_t bytes = 0;
    };

    AuthoredLines(base::Bits128 identity,
                  std::vector<std::string> lines,
                  DocumentReader reads,
                  const HistoryLimits& limits,
                  LineRewriter rewrites)
        : identity_(identity), lines_(std::move(lines)), reads_(reads), rewrites_(rewrites), history_(limits) {
    }
    [[nodiscard]] result::Result<Committed> step(std::uint64_t generation, bool undo);

    base::Bits128 identity_{};
    std::vector<std::string> lines_;
    DocumentReader reads_ = nullptr;
    LineRewriter rewrites_ = nullptr;
    History<Entry> history_;
    std::uint64_t generation_ = 0;
};

/// A line document's query answered (`lines.read`: `{"lines": [...]}`, each
/// line's text by its place); refuses (`ValidationFailed`) any other.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredLines& lines, const Query& query);

} // namespace rawframe::authoring
