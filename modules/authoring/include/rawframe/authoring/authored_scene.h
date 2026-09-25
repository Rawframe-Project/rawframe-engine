#pragma once

// A scene being authored (ADR-0065): the document, its generation, and its
// own history. Every change is a transaction bound to the generation it
// was computed against: deltas stage on a copy, and commit either
// publishes the whole journal as one history entry and a new generation
// or, when the staged scene is not in form, fails and leaves nothing.
// Cancelling, failing, or dropping the scope token discards everything
// staged. A transaction whose journal is empty commits as a no-op: no
// entry, no new generation.
//
// Undo and redo are ordinary calls, bound to a generation like a
// transaction, each a new generation. The clean state is a place in the
// history (Qt's clean index): saving marks it, and undoing or redoing back
// to it is clean again, which determinism makes exact. History is bounded
// by entries and bytes; committing drops the redo side, and eviction drops
// the oldest entries, after which a clean place evicted is definitely
// dirty, never falsely clean.
//
// Opening a transaction while one is open joins it: only the outermost
// token commits, and any token's cancel, or any failure, discards the
// whole; a token outliving its transaction does nothing. A coalescing
// transaction (a drag, a slider) folds a staged change into the journal's
// last delta when both change the same slot, so its entry keeps the first
// state before and the last after.

#include "rawframe/authoring/delta.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::authoring {

/// SPEC-0040's and ADR-0065's named limits for one document.
struct HistoryLimits {
    std::size_t maximumEntries = 256;
    std::size_t maximumBytes = std::size_t{64} * 1024 * 1024;
    std::size_t maximumJournalDeltas = 65536;
    std::size_t maximumJournalBytes = std::size_t{16} * 1024 * 1024;
};

enum class Coalescing : std::uint8_t {
    None,
    /// Each slot's changes fold into one: its first `before`, its last
    /// `after`.
    FoldSlots,
};

/// What a commit, undo, or redo did.
struct Committed {
    /// The document's generation after it; unchanged by a no-op.
    std::uint64_t generation = 0;
    /// The deltas it applied; none for a no-op.
    std::size_t deltas = 0;
};

class AuthoredScene;

/// An open transaction's scope token. Moving it moves the scope; dropping
/// it open cancels the whole transaction.
class Transaction {
public:
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) noexcept;
    ~Transaction();

    /// The scene as the transaction has staged it so far; once it has
    /// closed, the document's scene, or an empty one if the token let go.
    [[nodiscard]] const scene::Scene& staged() const noexcept;
    /// The resource identity of the document it changes; nought once it
    /// let go.
    [[nodiscard]] base::Bits128 document() const noexcept;
    /// Stages one delta onto the staged scene. A delta that does not apply,
    /// or passes the journal's limits (`LimitExceeded`), fails the whole
    /// transaction.
    [[nodiscard]] result::Status stage(const Delta& delta);
    /// The outermost token publishes the journal: the staged scene must be
    /// in form (`ValidationFailed` otherwise, and nothing is kept). An
    /// inner token only closes its scope.
    [[nodiscard]] result::Result<Committed> commit();
    /// Discards everything the transaction staged, however deep.
    void cancel() noexcept;

private:
    friend class AuthoredScene;
    Transaction(AuthoredScene* scene, std::uint64_t serial, bool outermost) noexcept;
    [[nodiscard]] bool live() const noexcept;

    AuthoredScene* scene_ = nullptr;
    std::uint64_t serial_ = 0;
    bool outermost_ = false;
};

class AuthoredScene {
public:
    /// The scene document `text`, of resource identity `document`, at
    /// generation nought and clean. Refuses (`ValidationFailed`) a text
    /// that does not read as a scene. It does not move, since its
    /// transactions point at it.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredScene>>
    open(base::Bits128 document, std::string_view text, const HistoryLimits& limits = {});

    AuthoredScene(AuthoredScene&&) = delete;
    AuthoredScene& operator=(AuthoredScene&&) = delete;
    AuthoredScene(const AuthoredScene&) = delete;
    AuthoredScene& operator=(const AuthoredScene&) = delete;
    ~AuthoredScene() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] const scene::Scene& scene() const noexcept {
        return scene_;
    }
    /// The document's bytes now.
    [[nodiscard]] std::string text() const;

    /// Opens a transaction against `generation`, or joins the open one.
    /// Refuses (`TargetStale`) any generation but the document's.
    [[nodiscard]] result::Result<Transaction> begin(std::uint64_t generation, Coalescing coalescing = Coalescing::None);
    [[nodiscard]] bool inTransaction() const noexcept {
        return open_.has_value();
    }

    [[nodiscard]] bool canUndo() const noexcept {
        return applied_ > 0;
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return applied_ < history_.size();
    }
    /// Takes back the last entry, or puts back the last taken back.
    /// Refuses (`TargetStale`) another generation, (`Conflict`) while a
    /// transaction is open, and (`TargetNotFound`) with nothing to do.
    [[nodiscard]] result::Result<Committed> undo(std::uint64_t generation);
    [[nodiscard]] result::Result<Committed> redo(std::uint64_t generation);

    /// Whether the document differs from what was last saved.
    [[nodiscard]] bool dirty() const noexcept {
        return clean_ != std::optional<std::size_t>{applied_};
    }
    /// The document as it is now is what was saved.
    void markSaved() noexcept {
        clean_ = applied_;
    }
    /// Entries that can be undone, and redone.
    [[nodiscard]] std::size_t undoable() const noexcept {
        return applied_;
    }
    [[nodiscard]] std::size_t redoable() const noexcept {
        return history_.size() - applied_;
    }

private:
    friend class Transaction;

    struct Entry {
        Journal journal;
        std::size_t bytes = 0;
    };
    struct Open {
        scene::Scene staged;
        Journal journal;
        std::size_t journalBytes = 0;
        std::uint64_t serial = 0;
        Coalescing coalescing = Coalescing::None;
    };

    AuthoredScene() = default;

    result::Status stage(const Delta& delta);
    result::Result<Committed> commit();
    void discard() noexcept;
    void append(Entry entry);
    [[nodiscard]] result::Result<Committed> step(std::uint64_t generation, bool undo);

    base::Bits128 identity_{};
    std::uint64_t generation_ = 0;
    scene::Scene scene_;
    HistoryLimits limits_;
    std::deque<Entry> history_;
    std::size_t historyBytes_ = 0;
    /// Entries [0, applied_) are undoable; the rest are redoable.
    std::size_t applied_ = 0;
    /// Where the saved document is in the history; none once evicted.
    std::optional<std::size_t> clean_ = 0;
    std::optional<Open> open_;
    /// Each transaction's number, so a token outliving one is inert.
    std::uint64_t serials_ = 0;
};

} // namespace rawframe::authoring
