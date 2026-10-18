#pragma once

// One document's history (ADR-0065, SPEC-0040), whatever kind the document
// is (D636): its entries oldest first, those before `undoable()` applied
// and the rest redoable, where the saved document stands among them, and
// eviction by the named limits, redo first, then oldest. An entry is
// whatever its document's kind keeps of a transaction; it says what it
// weighs as `bytes`.
//
// Each entry has an author (ADR-0085, D879), nought for the session's own,
// and undo is per author: an author's undo takes back that author's latest
// change. Where it is the history's last applied entry, the history steps
// back as one author's always has, its redo kept; where another author's
// change came after it, its document takes it back as a new entry, of kind
// `Undo`, if what came after does not stand in the way, and an author's
// redo takes back that author's latest such entry the same way, as a
// `Redo`. Committing anything drops the redo side for every author.

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

/// What an entry is (D879): a change committed, an author's change taken
/// back after another's came, or such an undo taken back again.
enum class EntryKind : std::uint8_t {
    Change,
    Undo,
    Redo,
};

/// Who made an entry and what it is: its author, nought for the session's
/// own; its number, never reused; what it is; for an undo or a redo, the
/// number of the entry it takes back; and whether it has itself been taken
/// back.
struct Authorship {
    std::uint32_t author = 0;
    std::uint64_t serial = 0;
    EntryKind kind = EntryKind::Change;
    std::uint64_t takesBack = 0;
    bool takenBack = false;
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
    /// The entry at `entry`, below `undoable() + redoable()`, and who made
    /// it.
    [[nodiscard]] const Entry& at(std::size_t entry) const noexcept {
        return entries_[entry];
    }
    [[nodiscard]] const Authorship& authorshipAt(std::size_t entry) const noexcept {
        return authorship_[entry];
    }

    /// The entry `author`'s undo takes back, or redo puts back, if any: for
    /// undo, their latest applied change or redo not taken back; for redo,
    /// the history's next redoable entry where it is theirs, else their
    /// latest applied undo not taken back.
    [[nodiscard]] std::optional<std::size_t> targetOf(std::uint32_t author, bool undo) const noexcept {
        if (!undo && applied_ < entries_.size() && authorship_[applied_].author == author) {
            return applied_;
        }
        for (std::size_t at = applied_; at > 0; --at) {
            const Authorship& kEach = authorship_[at - 1];
            if (kEach.author == author && !kEach.takenBack &&
                (undo ? kEach.kind != EntryKind::Undo : kEach.kind == EntryKind::Undo)) {
                return at - 1;
            }
        }
        return std::nullopt;
    }
    /// Whether taking back the entry at `entry` is a step of the history
    /// (it is the last applied, a change or a redo, or the next redoable)
    /// rather than an entry of its own.
    [[nodiscard]] bool steps(std::size_t entry, bool undo) const noexcept {
        return undo ? entry + 1 == applied_ && authorship_[entry].kind != EntryKind::Undo : entry == applied_;
    }
    /// The latest applied entry after `entry` by another author than
    /// `author`, if any: what may stand in the way of taking it back.
    [[nodiscard]] std::optional<std::size_t> laterByOthers(std::size_t entry, std::uint32_t author) const noexcept {
        for (std::size_t at = applied_; at > entry + 1; --at) {
            if (authorship_[at - 1].author != author) {
                return at - 1;
            }
        }
        return std::nullopt;
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

    /// A committed transaction's entry, the redo side dropped: by
    /// `author`, and for an undo or a redo, taking back the entry at
    /// `takesBack`.
    void append(Entry entry,
                std::uint32_t author = 0,
                EntryKind kind = EntryKind::Change,
                std::optional<std::size_t> takesBack = std::nullopt) {
        while (entries_.size() > applied_) {
            bytes_ -= entries_.back().bytes;
            entries_.pop_back();
            authorship_.pop_back();
        }
        Authorship made{.author = author, .serial = ++serials_, .kind = kind};
        if (takesBack.has_value() && *takesBack < authorship_.size()) {
            made.takesBack = authorship_[*takesBack].serial;
            authorship_[*takesBack].takenBack = true;
        }
        if (clean_.has_value() && *clean_ > applied_) {
            clean_.reset();
        }
        bytes_ += entry.bytes;
        entries_.push_back(std::move(entry));
        authorship_.push_back(made);
        ++applied_;
        // Oldest first; a clean place evicted is definitely dirty.
        while (!entries_.empty() && (entries_.size() > limits_.maximumEntries || bytes_ > limits_.maximumBytes)) {
            bytes_ -= entries_.front().bytes;
            entries_.pop_front();
            authorship_.pop_front();
            --applied_;
            if (clean_.has_value()) {
                clean_ = *clean_ == 0 ? std::nullopt : std::optional{*clean_ - 1};
            }
        }
    }

private:
    HistoryLimits limits_;
    std::deque<Entry> entries_;
    std::deque<Authorship> authorship_;
    std::uint64_t serials_ = 0;
    std::size_t bytes_ = 0;
    /// Entries [0, applied_) are undoable; the rest are redoable.
    std::size_t applied_ = 0;
    /// Where the saved document is in the history; none once evicted.
    std::optional<std::size_t> clean_ = 0;
};

} // namespace rawframe::authoring
