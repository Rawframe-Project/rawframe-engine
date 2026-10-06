#pragma once

// The session's records as Studio speaks them (D435): what it asks, built
// as any client builds them, and what it is answered, read as any client
// reads them. Studio keeps no document of its own: a panel shows what an
// answer says.

#include "rawframe/document/json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

using document::Value;

/// A field's value as a row shows it: a number or a text as itself,
/// anything else as its compact JSON.
[[nodiscard]] std::string shown(const Value& value);

/// What `text` typed into a field of `kind` (a read's value key) asks the
/// session to set it to; none for a kind Studio does not edit, or text
/// that is not one of its values. The session checks it again.
[[nodiscard]] std::optional<Value> typedValue(std::string_view kind, std::string_view text);

/// The first answer of a read's reply, if it is one.
[[nodiscard]] std::optional<Value> firstAnswer(std::string_view reply);

/// What an outcome (`authoring.apply`, `authoring.undo`, `authoring.redo`)
/// says: whether its one slot was done, the session's message when not,
/// and what can be undone and redone after it.
struct Outcome {
    bool done = false;
    std::string message;
    std::int64_t undoable = 0;
    std::int64_t redoable = 0;
};

[[nodiscard]] Outcome outcomeOf(std::string_view reply);

/// `authoring.read` of `scene`: one query, `operation`, with `entity` if
/// given.
[[nodiscard]] Value
readRecord(std::int64_t id, std::string_view scene, std::string_view operation, std::string_view entity = {});
/// `authoring.apply` to `scene` of one operation, atomic.
[[nodiscard]] Value applyRecord(std::int64_t id, std::string_view scene, Value operation);
/// `authoring.select` of `entity` in `scene`.
[[nodiscard]] Value selectRecord(std::int64_t id, std::string_view scene, std::string_view entity);
/// `authoring.undo` or `authoring.redo` (`kind`) of `scene`.
[[nodiscard]] Value stepRecord(std::int64_t id, std::string_view kind, std::string_view scene);

/// What `authoring.describe` says Studio may do: the operations it offers
/// by name, and the component catalog's types by identity and name.
struct Catalog {
    struct Component {
        std::string id;
        std::string name;
    };
    std::vector<std::string> operations;
    std::vector<Component> components;

    [[nodiscard]] bool offers(std::string_view operation) const;
};

[[nodiscard]] Catalog catalogOf(std::string_view reply);

/// The catalog's component `text` names: by its whole name, or by the
/// part after its last dot, or by the start of either, when one alone
/// matches; else none, and `why` says so.
[[nodiscard]] std::optional<Catalog::Component>
componentNamed(const Catalog& catalog, std::string_view text, std::string& why);

/// A new identity, a random version 4 UUID in its text form: a new entity's
/// is the client's to choose (D438), and an identity is data, not
/// simulation, so nothing needs it repeated.
[[nodiscard]] std::string mintedIdentity();

} // namespace rawframe::studio
