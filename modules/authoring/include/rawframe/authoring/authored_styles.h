#pragma once

// A game's style sheet open for authoring: a `ui.styles` document
// (rawframe/ui/styles.h), a JsonDocument whose slots are a class
// (`styles` by `styleId`), whole at its place; its `name`; its
// `transitions`; and one part of one variant's look (`hovered`,
// `fill` say). Its operations are `styles.set_part`, `styles.create_class`,
// `styles.remove_class`, `styles.rename_class`, and
// `styles.set_transitions`, in operations.h.

#include "rawframe/authoring/json_document.h"
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
        return document_.generation();
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
        return document_.root();
    }
    [[nodiscard]] std::string text() const {
        return document_.text();
    }

    /// The AtomicBatch over the sheet: each change in order against
    /// `generation`, one entry in the history together, none where every
    /// slot already holds its value. Any refused keeps nothing, naming its
    /// `index`: (`TargetStale`) another generation, (`TargetNotFound`) a
    /// class the sheet does not have, (`Conflict`) a new class's identity
    /// taken or nought, (`ValidationFailed`) a variant or part a look has
    /// not, an operation that is not a style sheet's, or a sheet that does
    /// not read after them, and (`LimitExceeded`) more than a batch's
    /// operations.
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
    /// What the history's entry at `entry` does, in a line an author reads
    /// in a history, as a scene's: its first delta, as `set
    /// hovered.fill`, and how many more there are.
    [[nodiscard]] std::string summaryAt(std::size_t entry) const;

private:
    AuthoredStyles(base::Bits128 identity, JsonDocument document, ui::StyleSheet sheet)
        : identity_(identity), document_(std::move(document)), sheet_(std::move(sheet)) {
    }

    /// The sheet read again after a change, an undo, or a redo.
    [[nodiscard]] result::Result<Committed> reread(result::Result<Committed> done);

    base::Bits128 identity_{};
    JsonDocument document_;
    ui::StyleSheet sheet_;
};

/// A style sheet's query answered (`styles.read_sheet`: `{"sheet": ...}`, the
/// document whole); refuses (`ValidationFailed`) a scene's.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredStyles& styles, const Query& query);

} // namespace rawframe::authoring
