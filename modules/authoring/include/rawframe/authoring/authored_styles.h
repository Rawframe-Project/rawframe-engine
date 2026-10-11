#pragma once

// A game's style sheet open for authoring (D636): a `ui.styles` document
// (rawframe/ui/styles.h) as AuthoredScene is a scene, each change a journal
// of deltas kept in its history (history.h), which undo and redo walk. A
// delta names one slot of one class by the class's identity, from what the
// document holds there, or none, to another:
//
//   set_field     one part of one variant's look, `hovered.fill` say
//   create_node   a class, from none to its record at its place (D638)
//   destroy_node  a class, from its record at its place to none
//   set_name      a class's name
//
// A change is kept only where the whole sheet still reads, so every
// generation is in form.

#include "rawframe/authoring/history.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/styles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring {

/// One slot of one class, from what it holds to another; none is the slot
/// empty: a part not set, a class not there.
struct StyleDelta {
    enum class Kind : std::uint8_t {
        SetPart,
        CreateClass,
        DestroyClass,
        SetName,
    };
    Kind kind = Kind::SetPart;
    std::uint64_t style = 0;
    /// A part's.
    std::string variant;
    std::string part;
    /// A class made or destroyed: where it stands among the sheet's.
    std::size_t place = 0;
    std::optional<document::Value> before;
    std::optional<document::Value> after;
};

class AuthoredStyles {
public:
    /// The styles document `text`, of resource identity `document`, at
    /// generation nought and clean. Refuses (`ValidationFailed`) a text that
    /// does not read as a style sheet.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredStyles>>
    open(base::Bits128 document, std::string_view text, const HistoryLimits& limits = {});

    AuthoredStyles(AuthoredStyles&&) = delete;
    AuthoredStyles& operator=(AuthoredStyles&&) = delete;
    AuthoredStyles(const AuthoredStyles&) = delete;
    AuthoredStyles& operator=(const AuthoredStyles&) = delete;
    ~AuthoredStyles() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    /// The sheet as it reads now.
    [[nodiscard]] const ui::StyleSheet& sheet() const noexcept {
        return sheet_;
    }
    /// The value of `part` of `variant` of class `style`, as the document
    /// holds it; none where it is not set or there is no such class.
    [[nodiscard]] std::optional<document::Value>
    part(std::uint64_t style, std::string_view variant, std::string_view part) const;
    /// The document now, and its bytes.
    [[nodiscard]] const document::Value& documentValue() const noexcept {
        return root_;
    }
    [[nodiscard]] std::string text() const;

    /// SPEC-0040's AtomicBatch over the sheet: each change in order against
    /// `generation`, one entry in the history together, none where every
    /// part already has its value. Any refused keeps nothing, naming its
    /// `index`: (`TargetStale`) another generation, (`TargetNotFound`) a
    /// class the sheet does not have, (`ValidationFailed`) a variant or
    /// part a look has not, an operation that is not a style sheet's, or a
    /// sheet that does not read after them, and (`LimitExceeded`) more than
    /// a batch's operations.
    [[nodiscard]] result::Result<Committed> change(std::uint64_t generation, std::span<const Operation> operations);

    [[nodiscard]] bool canUndo() const noexcept {
        return history_.canUndo();
    }
    [[nodiscard]] bool canRedo() const noexcept {
        return history_.canRedo();
    }
    /// Takes back the last entry, or puts back the last taken back.
    /// Refuses (`TargetStale`) another generation and (`TargetNotFound`)
    /// with nothing to do.
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
    [[nodiscard]] const std::vector<StyleDelta>& journalAt(std::size_t entry) const noexcept {
        return history_.at(entry).journal;
    }

private:
    struct Entry {
        std::vector<StyleDelta> journal;
        std::size_t bytes = 0;
    };

    AuthoredStyles() = default;

    /// `journal` applied forward or backward to the document, kept only
    /// where every delta finds its slot as it expects and the sheet reads.
    [[nodiscard]] result::Status apply(const std::vector<StyleDelta>& journal, bool forward);
    [[nodiscard]] result::Result<Committed> step(std::uint64_t generation, bool undo);

    base::Bits128 identity_{};
    std::uint64_t generation_ = 0;
    document::Value root_;
    ui::StyleSheet sheet_;
    History<Entry> history_;
};

/// What a sheet's journal does, in a line an author reads in a history, as
/// a scene's (D454, D638): its first delta, as `set hovered.fill`,
/// and how many more there are; `no change` for an empty one.
[[nodiscard]] std::string summaryOf(std::span<const StyleDelta> journal);

/// A style sheet's query answered (`styles.read_sheet`: `{"sheet": ...}`, the
/// document whole); refuses (`ValidationFailed`) a scene's.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredStyles& styles, const Query& query);

} // namespace rawframe::authoring
