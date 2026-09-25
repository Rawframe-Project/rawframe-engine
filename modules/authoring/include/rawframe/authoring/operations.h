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

/// The surface's generation: its operations, their inputs, and its error
/// codes as a whole. Not the engine's version.
inline constexpr std::uint32_t kSurfaceGeneration = 1;

enum class FieldKind : std::uint8_t {
    Signed,
    Unsigned,
    Real,
    Truth,
    Reference,
};

struct FieldSchema {
    std::string name;
    FieldKind kind = FieldKind::Real;
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
    /// already here, or whose fields repeat a name.
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

/// A field's new value: its default, or a number or truth of the field's
/// kind.
struct FieldInput {
    enum class Kind : std::uint8_t {
        Default,
        Signed,
        Unsigned,
        Real,
        Truth,
    };
    Kind kind = Kind::Default;
    std::int64_t integer = 0;
    std::uint64_t whole = 0;
    double real = 0.0;
    bool truth = false;
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
                               RemoveInstance>;

/// SPEC-0040's read operations (queries.h answers them): every entity the
/// scene holds, and one entity whole.
struct ListEntities {};
struct ReadEntity {
    base::Bits128 entity{};
};

using Query = std::variant<ListEntities, ReadEntity>;

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
