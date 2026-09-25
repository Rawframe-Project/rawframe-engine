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

using Operation = std::variant<CreateEntity,
                               DestroyEntity,
                               RenameEntity,
                               MoveEntity,
                               AddComponent,
                               RemoveComponent,
                               SetField,
                               SetReference>;

/// SPEC-0040's history classes; every operation here is `Undoable`.
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

/// Every operation, in the order of `Operation`'s alternatives.
[[nodiscard]] std::span<const OperationDeclaration> declarations() noexcept;
[[nodiscard]] const OperationDeclaration& declarationOf(const Operation& operation) noexcept;

enum class Mode : std::uint8_t {
    Execute,
    DryRun,
};

/// One operation in its own transaction against `generation`. With
/// `DryRun`, the same validation and nothing staged: the answer is the
/// generation it was checked against and no deltas.
[[nodiscard]] result::Result<Committed> execute(AuthoredScene& scene,
                                                std::uint64_t generation,
                                                const Operation& operation,
                                                const ComponentCatalog& catalog,
                                                Mode mode = Mode::Execute);

/// One operation staged in an open transaction, validated against what it
/// has staged so far; a failure fails the transaction.
[[nodiscard]] result::Status
stage(Transaction& transaction, const Operation& operation, const ComponentCatalog& catalog);

/// SPEC-0040's AtomicBatch: every operation in order in one transaction;
/// any failure keeps nothing.
[[nodiscard]] result::Result<Committed> executeAtomic(AuthoredScene& scene,
                                                      std::uint64_t generation,
                                                      std::span<const Operation> operations,
                                                      const ComponentCatalog& catalog);

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
                                                    OnFailure onFailure);

} // namespace rawframe::authoring
