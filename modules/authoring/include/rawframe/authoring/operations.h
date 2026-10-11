#pragma once

// Authoring operations (SPEC-0040): the closed, typed, semantic verbs that
// are the only public way to change an authored scene, each decomposing
// into deltas (delta.h) staged in a transaction (authored_scene.h).
//
// Operations address what they change by identity: an entity by its
// SourceEntityId, a component by its ComponentTypeId, and a field by its
// name in that component's layout, which is a field's identity in a
// Kest-declared schema (D151). A component's type id resolves to the name
// and layout mark the scene document keys it by through a
// `ComponentCatalog`, the schema authority a caller builds from a game;
// no request addresses by a name the document happens to use.
//
// An entity an instance brings (D96) is addressed the same way, and the
// same verbs change it through the instance's patch (D154): a field set on
// it is a `set` entry, a component added or removed an `add` or `remove`
// entry, and destroying it its removal. Its name and place are its source
// scene's. The revert verbs drop what the patch does, so the source's
// value holds again. What the source scene itself holds is checked when
// the instance is resolved, not here.
//
// Every request names the generation it was computed against. Validation
// runs in SPEC-0040's order, addressing, staleness, schema and meaning,
// limits, and only then stages; `DryRun` runs exactly that validation and
// stages nothing. A failure is one of SPEC-0040's classes
// (`AuthoringError`), with the failing operation as the `operation`
// context.

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/authoring/delta.h"
#include "rawframe/base/bits128.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/resolve.h"
#include "rawframe/schema/stable_id.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rawframe::authoring {

/// The most operations one batch holds (SPEC-0040's named limit).
inline constexpr std::size_t kMaximumBatchOperations = 4096;

/// The surface's generation: its operations, their inputs, and its error
/// codes as a whole. Not the engine's version.
inline constexpr std::uint32_t kSurfaceGeneration = 1;

enum class FieldKind : std::uint8_t {
    Signed,
    Unsigned,
    Real,
    Truth,
    Reference,
    /// One of an enum's cases, by name (D271).
    Case,
};

struct FieldSchema {
    std::string name;
    FieldKind kind = FieldKind::Real;
    /// A case field's cases in the order of their numbers; the first is its
    /// default. Empty for every other kind.
    std::vector<std::string> cases;
};

/// A component as authoring knows it: its identity, the name and layout
/// mark a scene keys it by, and its fields.
struct ComponentSchema {
    schema::ComponentTypeId id{};
    std::string name;
    std::uint64_t mark = 0;
    std::vector<FieldSchema> fields;
};

class ComponentCatalog {
public:
    /// Refuses (`ValidationFailed`) a component whose id or name is
    /// already here, whose fields repeat a name, or whose case field has
    /// no cases or repeats one.
    [[nodiscard]] result::Status add(ComponentSchema component);
    [[nodiscard]] const ComponentSchema* find(schema::ComponentTypeId id) const noexcept;
    /// By the name a scene records it by: for tools reading a document,
    /// never for addressing an operation.
    [[nodiscard]] const ComponentSchema* findNamed(std::string_view name) const noexcept;
    /// In the order they were added.
    [[nodiscard]] std::span<const ComponentSchema> components() const noexcept {
        return components_;
    }

private:
    std::vector<ComponentSchema> components_;
};

/// A field's new value: its default, or a number, truth, or case of the
/// field's kind.
struct FieldInput {
    enum class Kind : std::uint8_t {
        Default,
        Signed,
        Unsigned,
        Real,
        Truth,
        Case,
    };
    Kind kind = Kind::Default;
    std::int64_t integer = 0;
    std::uint64_t whole = 0;
    double real = 0.0;
    bool truth = false;
    std::string caseName;
};

struct CreateEntity {
    base::Bits128 entity{};
    std::string name;
    /// Where among the scene's entities; none for the end.
    std::optional<std::size_t> place;
};
struct DestroyEntity {
    base::Bits128 entity{};
};
struct RenameEntity {
    base::Bits128 entity{};
    std::string name;
};
struct MoveEntity {
    base::Bits128 entity{};
    std::size_t place = 0;
};
/// Every field at its default.
struct AddComponent {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
};
struct RemoveComponent {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
};
struct SetField {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
    std::string field;
    FieldInput value;
};
struct SetReference {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
    std::string field;
    /// None for the field's default, no entity.
    std::optional<base::Bits128> target;
};

/// Records the component's current layout mark in place of the one the
/// scene was authored against (ADR-0067, D153), when every field the scene
/// gives it still exists by name and each value fits the field's kind now.
/// Otherwise refuses (`ValidationFailed`) naming the fields that do not
/// carry over as `residual`: nothing is dropped or reinterpreted silently.
struct RemarkComponent {
    schema::ComponentTypeId component{};
};

/// Drops an instance's own value for one field of one of its entities.
struct RevertField {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
    std::string field;
};
/// Drops whatever an instance does to one component of one of its
/// entities: its values, its addition, or its removal.
struct RevertComponent {
    base::Bits128 entity{};
    schema::ComponentTypeId component{};
};
/// Takes back an instance's removal of one of its entities.
struct RestoreEntity {
    base::Bits128 entity{};
};

/// Instances the scene of resource identity `scene` after the scene's
/// instances, in SPEC-0006's two passes: every entity the source holds once
/// resolved is given an id (`instanceEntityId`) from `instance`, a fresh
/// identity the caller makes as it makes an entity's, and the entity's id
/// in the source. So one request always makes the same ids, and two
/// instantiations never share one. It needs the scenes at hand.
struct AddInstance {
    base::Bits128 scene{};
    base::Bits128 instance{};
};
/// Removes the instance that brings `entity`, its patch with it.
struct RemoveInstance {
    base::Bits128 entity{};
};

/// One part of one variant's look of one class of a style sheet (D636),
/// set to its value as the styles document holds it, or unset for none: a
/// `ui.styles` document's operation, refused by a scene.
struct SetStylePart {
    std::uint64_t style = 0;
    std::string variant;
    std::string part;
    std::optional<document::Value> value;
};
/// A new class at `place` among the sheet's, or after them all (D638):
/// `style` its identity, fresh, as the caller makes one (16 hex digits,
/// not all nought), and `name` its name; it sets no look yet.
struct CreateStyleClass {
    std::uint64_t style = 0;
    std::string name;
    std::optional<std::size_t> place;
};
/// A class gone, with its looks and transitions. A node naming it is
/// unstyled until a class of its identity comes again.
struct RemoveStyleClass {
    std::uint64_t style = 0;
};
struct RenameStyleClass {
    std::uint64_t style = 0;
    std::string name;
};
/// A class's transitions, all of them, as the styles document holds them
/// (an array), or none for no transitions (D639).
struct SetStyleTransitions {
    std::uint64_t style = 0;
    std::optional<document::Value> value;
};
/// One field of the UI document's node `node` (its component's name), as
/// the document holds it, or none for its default: `shows`, or any of
/// `rawframe.ui.Node`'s fields (D641).
struct SetUiField {
    std::string node;
    std::string field;
    std::optional<document::Value> value;
};
/// A new node of component `node`, inside `parent` or a root for none, at
/// `place` among its siblings or after them; it starts at every default.
struct CreateUiNode {
    std::string node;
    std::optional<std::string> parent;
    std::optional<std::size_t> place;
};
/// A node gone, with the nodes inside it.
struct RemoveUiNode {
    std::string node;
};

/// The id an instantiation named `instance` gives the source's entity
/// `source`: a version 8 UUID from their SHA-256 (D156).
[[nodiscard]] base::Bits128 instanceEntityId(base::Bits128 instance, base::Bits128 source) noexcept;

using Operation = std::variant<CreateEntity,
                               DestroyEntity,
                               RenameEntity,
                               MoveEntity,
                               AddComponent,
                               RemoveComponent,
                               SetField,
                               SetReference,
                               RemarkComponent,
                               RevertField,
                               RevertComponent,
                               RestoreEntity,
                               AddInstance,
                               RemoveInstance,
                               SetStylePart,
                               CreateStyleClass,
                               RemoveStyleClass,
                               RenameStyleClass,
                               SetStyleTransitions,
                               SetUiField,
                               CreateUiNode,
                               RemoveUiNode>;

/// Whether `operation` changes a scene, rather than a style sheet (D636) or
/// a UI document (D641), which follow the scene's in `Operation`.
[[nodiscard]] constexpr bool changesScene(const Operation& operation) noexcept {
    return operation.index() < std::variant_size_v<Operation> - 8;
}

/// SPEC-0040's read operations (queries.h answers them): every entity the
/// scene holds, one entity whole, and the entities a typed search finds
/// (ADR-0066's hierarchy search, D473).
struct ListEntities {};
struct ReadEntity {
    base::Bits128 entity{};
};
/// Entities whose name holds `named` (ASCII letters in either case; empty
/// for any), and that hold `having` when one is named.
struct FindEntities {
    std::string named;
    std::optional<schema::ComponentTypeId> having;
};

/// A style sheet's document whole (D636), as a `ui.styles` document holds
/// it: a style sheet's query, refused by a scene.
struct ReadSheet {};
/// A UI document whole (D641), as it holds itself: a UI document's query.
struct ReadUiDocument {};

using Query = std::variant<ListEntities, ReadEntity, FindEntities, ReadSheet, ReadUiDocument>;

/// SPEC-0040's history classes: every operation that changes a scene is
/// `Undoable`, and every query `ReadOnly`.
enum class HistoryClass : std::uint8_t {
    Undoable,
    NonDirtying,
    BulkNonUndoable,
    ReadOnly,
};

enum class InputType : std::uint8_t {
    Entity,
    Component,
    Field,
    Text,
    Place,
    OptionalPlace,
    Value,
    OptionalEntity,
    /// A resource identity, as 32 hex digits.
    Resource,
    /// A fresh identity the caller makes, as UUID text.
    Identity,
    /// A component that may be left out.
    OptionalComponent,
    /// A style class's identity, as 16 hex digits (D636).
    Style,
    /// A value as a JSON document holds it, or null for none (D636).
    DocumentValue,
    /// Text that may be null (D641).
    OptionalText,
};

struct InputDeclaration {
    std::string_view name;
    InputType type = InputType::Entity;
};

/// What discovery publishes of one operation (SPEC-0040): its stable name,
/// the surface generation that brought it, its history class, what it
/// targets, and its inputs.
struct OperationDeclaration {
    std::string_view name;
    std::uint32_t since = kSurfaceGeneration;
    HistoryClass history = HistoryClass::Undoable;
    std::string_view targets;
    std::span<const InputDeclaration> inputs;
};

/// Every operation, in the order of `Operation`'s alternatives, then every
/// query in the order of `Query`'s.
[[nodiscard]] std::span<const OperationDeclaration> declarations() noexcept;
[[nodiscard]] const OperationDeclaration& declarationOf(const Operation& operation) noexcept;
[[nodiscard]] const OperationDeclaration& declarationOf(const Query& query) noexcept;

enum class Mode : std::uint8_t {
    Execute,
    DryRun,
};

/// One operation in its own transaction against `generation`. With
/// `DryRun`, the same validation and nothing staged: the answer is the
/// generation it was checked against and no deltas.
/// `sources` gives the scenes an instance may name; without it, an
/// operation that needs one refuses (`TargetNotFound`).
[[nodiscard]] result::Result<Committed> execute(AuthoredScene& scene,
                                                std::uint64_t generation,
                                                const Operation& operation,
                                                const ComponentCatalog& catalog,
                                                Mode mode = Mode::Execute,
                                                const scene::SceneSource* sources = nullptr);

/// One operation staged in an open transaction, validated against what it
/// has staged so far; a failure fails the transaction.
[[nodiscard]] result::Status stage(Transaction& transaction,
                                   const Operation& operation,
                                   const ComponentCatalog& catalog,
                                   const scene::SceneSource* sources = nullptr);

/// SPEC-0040's AtomicBatch: every operation in order in one transaction;
/// any failure keeps nothing.
[[nodiscard]] result::Result<Committed> executeAtomic(AuthoredScene& scene,
                                                      std::uint64_t generation,
                                                      std::span<const Operation> operations,
                                                      const ComponentCatalog& catalog,
                                                      const scene::SceneSource* sources = nullptr);

/// One document's part of a multi-document transaction: its scene, the
/// generation it was computed against, and its operations in order.
struct DocumentPart {
    AuthoredScene* scene = nullptr;
    std::uint64_t generation = 0;
    std::span<const Operation> operations;
};

/// SPEC-0040's AtomicBatch over several documents, one transaction (D497):
/// each part's operations staged in order in its document's transaction,
/// then all committed together (`commitTogether`). Any failure keeps
/// nothing in any document and names the part (`part`) and, below it, the
/// operation (`index`).
[[nodiscard]] result::Result<std::vector<Committed>> executeTogether(std::span<const DocumentPart> parts,
                                                                     const ComponentCatalog& catalog,
                                                                     const scene::SceneSource* sources = nullptr);

enum class OnFailure : std::uint8_t {
    ContinuePerItem,
    HaltRemaining,
};

/// SPEC-0040's IndependentBatch: each operation its own transaction
/// against the generation the one before it left, each with its own
/// outcome; halting reports how many were skipped.
struct IndependentOutcome {
    std::vector<result::Result<Committed>> outcomes;
    std::size_t skipped = 0;
};
[[nodiscard]] IndependentOutcome executeIndependent(AuthoredScene& scene,
                                                    std::uint64_t generation,
                                                    std::span<const Operation> operations,
                                                    const ComponentCatalog& catalog,
                                                    OnFailure onFailure,
                                                    const scene::SceneSource* sources = nullptr);

} // namespace rawframe::authoring
