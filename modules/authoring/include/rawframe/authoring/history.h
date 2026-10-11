#pragma once

// One document's history (ADR-0065, SPEC-0040), whatever kind the document
// is (D636): its entries oldest first, those before `undoable()` applied
// and the rest redoable, where the saved document stands among them, and
// eviction by the named limits, redo first, then oldest. An entry is
// whatever its document's kind keeps of a transaction; it says what it
// weighs as `bytes`.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace rawframe::authoring {

/// SPEC-0040's and ADR-0065's named limits for one document.
struct HistoryLimits {
    std::size_t maximumEntries = 256;
    std::size_t maximumBytes = std::size_t{64} * 1024 * 1024;
    std::size_t maximumJournalDeltas = 65536;
    std::size_t maximumJournalBytes = std::size_t{16} * 1024 * 1024;
    /// Entities selected at most.
    std::size_t maximumSelection = 65536;
};

/// What a commit, undo, or redo did.
struct Committed {
    /// The document's generation after it; unchanged by a no-op.
    std::uint64_t generation = 0;
    /// The deltas it applied; none for a no-op.
    std::size_t deltas = 0;
};

template <class Entry> class History {
public:
    explicit History(const HistoryLimits& limits = {}) : limits_(limits) {
    }

    [[nodiscard]] const HistoryLimits& limits() const noexcept {
        return limits_;
    }
    [[nodiscard]] bool canUndo() const noexcept {
        return applied_ > 0;
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return applied_ < entries_.size();
    }
    /// Entries that can be undone, and redone.
    [[nodiscard]] std::size_t undoable() const noexcept {
        return applied_;
    }
    [[nodiscard]] std::size_t redoable() const noexcept {
        return entries_.size() - applied_;
    }
    /// The entry at `entry`, below `undoable() + redoable()`.
    [[nodiscard]] const Entry& at(std::size_t entry) const noexcept {
        return entries_[entry];
    }
    /// The entry an undo takes back, or a redo puts back; there is one.
    [[nodiscard]] const Entry& next(bool undo) const noexcept {
        return entries_[undo ? applied_ - 1 : applied_];
    }
    /// That entry taken back, or put back.
    void stepped(bool undo) noexcept {
        applied_ = undo ? applied_ - 1 : applied_ + 1;
    }

    /// Whether the document differs from what was last saved.
    [[nodiscard]] bool dirty() const noexcept {
        return clean_ != std::optional<std::size_t>{applied_};
    }
    /// The document as it is now is what was saved.
    void markSaved() noexcept {
        clean_ = applied_;
    }

    /// A committed transaction's entry, the redo side dropped.
    void append(Entry entry) {
        while (entries_.size() > applied_) {
            bytes_ -= entries_.back().bytes;
            entries_.pop_back();
        }
        if (clean_.has_value() && *clean_ > applied_) {
            clean_.reset();
        }
        bytes_ += entry.bytes;
        entries_.push_back(std::move(entry));
        ++applied_;
        // Oldest first; a clean place evicted is definitely dirty.
        while (!entries_.empty() && (entries_.size() > limits_.maximumEntries || bytes_ > limits_.maximumBytes)) {
            bytes_ -= entries_.front().bytes;
            entries_.pop_front();
            --applied_;
            if (clean_.has_value()) {
                clean_ = *clean_ == 0 ? std::nullopt : std::optional{*clean_ - 1};
            }
        }
    }

private:
    HistoryLimits limits_;
    std::deque<Entry> entries_;
    std::size_t bytes_ = 0;
    /// Entries [0, applied_) are undoable; the rest are redoable.
    std::size_t applied_ = 0;
    /// Where the saved document is in the history; none once evicted.
    std::optional<std::size_t> clean_ = 0;
};

} // namespace rawframe::authoring
