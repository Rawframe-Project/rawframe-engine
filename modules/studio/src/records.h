#pragma once

// The session's records as Studio speaks them (D435): what it asks, built
// as any client builds them, and what it is answered, read as any client
// reads them. Studio keeps no document of its own: a panel shows what an
// answer says.

#include "rawframe/document/json.h"

#include <cstdint>
#include <optional>
#include <span>
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
    /// The scene's view after it, when the answer says (null for none).
    std::optional<Value> view;
};

[[nodiscard]] Outcome outcomeOf(std::string_view reply);

/// What `authoring.view` or `authoring.preview` answers: whether it was
/// done, the session's message when not, the scene's view when said (null
/// for none), and whether a preview of the scene is live.
struct Answered {
    bool done = false;
    std::string message;
    std::optional<Value> view;
    bool previewing = false;
};

[[nodiscard]] Answered answeredOf(std::string_view reply);

/// A running Runtime's tooling endpoint to preview a scene in (D433).
struct Preview {
    std::string endpoint;
    std::string pinFile;
    std::string tokenFile;
};

/// `authoring.view` of `scene`.
[[nodiscard]] Value viewRecord(std::int64_t id, std::string_view scene, const Value& view);
/// `authoring.preview` of `scene` in `preview`, or letting it go.
[[nodiscard]] Value previewRecord(std::int64_t id, std::string_view scene, const Preview* preview);

/// A whole view (`eye`, `target`, `fieldOfView`) with its part `part` as
/// `text` says: three numbers apart by spaces or commas for a point, one
/// for the field of view, the rest as `current` has them, or a view from
/// above the origin when there is none; none when the text is not that.
[[nodiscard]] std::optional<Value>
viewWith(const std::optional<Value>& current, std::string_view part, std::string_view text);

/// A view's part `part` as a field shows it, empty while there is none.
[[nodiscard]] std::string viewText(const std::optional<Value>& view, std::string_view part);

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
    struct Field {
        std::string name;
        /// Its kind as a read value's key names it (`real`, `unsigned`...),
        /// empty where the catalog has none.
        std::string kind;
    };
    struct Component {
        std::string id;
        std::string name;
        /// Its fields in the catalog's order.
        std::vector<Field> fields;
    };
    std::vector<std::string> operations;
    std::vector<Component> components;

    [[nodiscard]] bool offers(std::string_view operation) const;
    /// The catalog's type of identity `id`, if it has one.
    [[nodiscard]] const Component* component(std::string_view id) const;
};

[[nodiscard]] Catalog catalogOf(std::string_view reply);

/// A component's field as Studio lists it: its name, its kind, and its
/// value's text when the scene sets it.
struct FieldShown {
    std::string name;
    std::string kind;
    std::optional<std::string> text;
};

/// A component's fields as `read_entity` gave them (`fields`, may be
/// null) over its type's in the catalog (`type`, may be null): the
/// catalog's in its order, each with the scene's value when set, then any
/// the scene sets that the catalog does not know. A field's kind is its
/// value's, else the catalog's.
[[nodiscard]] std::vector<FieldShown> fieldsShown(const Catalog::Component* type, const Value* fields);

/// The catalog's component `text` names: by its whole name, or by the
/// part after its last dot, or by the start of either, when one alone
/// matches; else none, and `why` says so.
[[nodiscard]] std::optional<Catalog::Component>
componentNamed(const Catalog& catalog, std::string_view text, std::string& why);

/// The scene's entity `text` names, of `ids` and their `names` in the
/// same order: by its whole name, by the start of its identity (four
/// characters at least), or by the start of its name, when one alone
/// matches; else none, and `why` says so (D447).
[[nodiscard]] std::optional<std::string> entityNamed(std::span<const std::string> ids,
                                                     std::span<const std::string> names,
                                                     std::string_view text,
                                                     std::string& why);

/// A new identity, a random version 4 UUID in its text form: a new entity's
/// is the client's to choose (D438), and an identity is data, not
/// simulation, so nothing needs it repeated.
[[nodiscard]] std::string mintedIdentity();

} // namespace rawframe::studio
