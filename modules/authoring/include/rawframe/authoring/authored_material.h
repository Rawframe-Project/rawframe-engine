#pragma once

// A game's material instance open for authoring: a surface
// material instance document (rawframe/material/instance.h), a
// JsonDocument whose slots are its overrides, each by its base's
// interface input or static switch. Its operation is
// `materials.set_override`, in operations.h; what each override may be is
// its base's to say, which the cook checks it against.

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

class AuthoredMaterial {
public:
    /// The instance document `text`, of resource identity `document`, at
    /// generation nought and clean. Refuses (`ValidationFailed`) a text that
    /// does not read as an instance: a material graph's, say.
    [[nodiscard]] static result::Result<std::unique_ptr<AuthoredMaterial>>
    open(base::Bits128 document, std::string_view text, const HistoryLimits& limits = {});

    AuthoredMaterial(AuthoredMaterial&&) = delete;
    AuthoredMaterial& operator=(AuthoredMaterial&&) = delete;
    AuthoredMaterial(const AuthoredMaterial&) = delete;
    AuthoredMaterial& operator=(const AuthoredMaterial&) = delete;
    ~AuthoredMaterial() = default;

    [[nodiscard]] base::Bits128 identity() const noexcept {
        return identity_;
    }
    /// Whether it is a material's graph, not an instance of one.
    [[nodiscard]] bool graph() const noexcept {
        return graph_;
    }
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return document_.generation();
    }
    /// The instance's base, its path from the instance's own.
    [[nodiscard]] std::string base() const;
    /// The document now, and its bytes.
    [[nodiscard]] const document::Value& documentValue() const noexcept {
        return document_.root();
    }
    [[nodiscard]] std::string text() const {
        return document_.text();
    }

    /// The AtomicBatch over the instance: each change in order
    /// against `generation`, one entry in the history together, none where
    /// every slot already holds its value. Any refused keeps nothing,
    /// naming its `index`: (`TargetStale`) another generation,
    /// (`ValidationFailed`) an input not named as a Kest name is, an
    /// operation that is not a material's, or an instance that does not
    /// read after them (one with no override left, say), and
    /// (`LimitExceeded`) more than a batch's operations.
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
    /// Who made the history's entry at `entry`, and what it is.
    [[nodiscard]] const Authorship& authorshipAt(std::size_t entry) const noexcept {
        return document_.authorshipAt(entry);
    }
    [[nodiscard]] const std::vector<JsonDelta>& journalAt(std::size_t entry) const noexcept {
        return document_.journalAt(entry);
    }
    /// What the history's entry at `entry` does, in a line an author reads:
    /// `set tint` or `unset tint`, and how many more there are.
    [[nodiscard]] std::string summaryAt(std::size_t entry) const;

private:
    AuthoredMaterial(base::Bits128 identity, JsonDocument document, bool graph)
        : identity_(identity), document_(std::move(document)), graph_(graph) {
    }

    base::Bits128 identity_{};
    JsonDocument document_;
    bool graph_ = false;
};

/// A material's query answered: an instance's `materials.read_instance`
/// (`{"instance": ...}`), a graph's `materials.read_graph` (`{"graph":.
/// ..}`) and `materials.list_node_types` (`{"types": [...]}`, each
/// `{"type", "params", "inputs", "outputs"}` as the material module's
/// signature says); refuses (`ValidationFailed`) any other.
[[nodiscard]] result::Result<document::Value> answer(const AuthoredMaterial& material, const Query& query);

} // namespace rawframe::authoring
