#include "rawframe/authoring/operations.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/document/json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace rawframe::authoring {

namespace {

/// The most operations one batch holds (SPEC-0040's named limit).
constexpr std::size_t kMaximumBatchOperations = 4096;

constexpr std::array<InputDeclaration, 3> kCreateInputs = {
    {{"entity", InputType::Entity}, {"name", InputType::Text}, {"place", InputType::OptionalPlace}}};
constexpr std::array<InputDeclaration, 1> kDestroyInputs = {{{"entity", InputType::Entity}}};
constexpr std::array<InputDeclaration, 2> kRenameInputs = {{{"entity", InputType::Entity}, {"name", InputType::Text}}};
constexpr std::array<InputDeclaration, 2> kMoveInputs = {{{"entity", InputType::Entity}, {"place", InputType::Place}}};
constexpr std::array<InputDeclaration, 2> kComponentInputs = {
    {{"entity", InputType::Entity}, {"component", InputType::Component}}};
constexpr std::array<InputDeclaration, 4> kSetFieldInputs = {{{"entity", InputType::Entity},
                                                              {"component", InputType::Component},
                                                              {"field", InputType::Field},
                                                              {"value", InputType::Value}}};
constexpr std::array<InputDeclaration, 4> kSetReferenceInputs = {{{"entity", InputType::Entity},
                                                                  {"component", InputType::Component},
                                                                  {"field", InputType::Field},
                                                                  {"target", InputType::OptionalEntity}}};

constexpr std::array<OperationDeclaration, 8> kDeclarations = {{
    {.name = "scene.create_entity", .targets = "rawframe.scene entity", .inputs = kCreateInputs},
    {.name = "scene.destroy_entity", .targets = "rawframe.scene entity", .inputs = kDestroyInputs},
    {.name = "scene.rename_entity", .targets = "rawframe.scene entity", .inputs = kRenameInputs},
    {.name = "scene.move_entity", .targets = "rawframe.scene entity", .inputs = kMoveInputs},
    {.name = "scene.add_component", .targets = "rawframe.scene component", .inputs = kComponentInputs},
    {.name = "scene.remove_component", .targets = "rawframe.scene component", .inputs = kComponentInputs},
    {.name = "scene.set_field", .targets = "rawframe.scene field", .inputs = kSetFieldInputs},
    {.name = "scene.set_reference", .targets = "rawframe.scene field", .inputs = kSetReferenceInputs},
}};

std::unexpected<result::Error>
refuse(const Operation& operation, result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kAuthoringDomain, code(error), why)
                                              .error()
                                              .withContext("operation", declarationOf(operation).name)};
}

std::unexpected<result::Error> notFound(const Operation& operation, std::string_view why) {
    return refuse(operation, result::ErrorClass::NotFound, AuthoringError::TargetNotFound, why);
}

std::unexpected<result::Error> invalid(const Operation& operation, std::string_view why) {
    return refuse(operation, result::ErrorClass::InvalidArgument, AuthoringError::ValidationFailed, why);
}

std::unexpected<result::Error> conflict(const Operation& operation, std::string_view why) {
    return refuse(operation, result::ErrorClass::Conflict, AuthoringError::Conflict, why);
}

const scene::SceneEntity* entityOf(const scene::Scene& scene, base::Bits128 id) {
    const auto kFound = std::ranges::find(scene.entities, id, &scene::SceneEntity::id);
    return kFound != scene.entities.end() ? &*kFound : nullptr;
}

std::size_t placeOf(const scene::Scene& scene, base::Bits128 id) {
    return static_cast<std::size_t>(std::ranges::find(scene.entities, id, &scene::SceneEntity::id) -
                                    scene.entities.begin());
}

std::uint64_t markOf(const scene::Scene& scene, std::string_view component) {
    const auto kFound = std::ranges::find(scene.schema, component, &scene::SchemaMark::component);
    return kFound != scene.schema.end() ? kFound->mark : 0;
}

ComponentRecord recordOf(const scene::Scene& scene, const scene::SceneComponent& component) {
    return ComponentRecord{.name = component.name, .mark = markOf(scene, component.name), .fields = component.fields};
}

/// Whether anything in the scene but `entity` itself names it.
bool referenced(const scene::Scene& scene, base::Bits128 entity) {
    for (const scene::SceneEntity& each : scene.entities) {
        if (each.id == entity) {
            continue;
        }
        for (const scene::SceneComponent& component : each.components) {
            for (const scene::SceneField& field : component.fields) {
                if (field.value.kind == scene::FieldValue::Kind::Entity && field.value.entity == entity) {
                    return true;
                }
            }
        }
    }
    for (const scene::SceneInstance& instance : scene.instances) {
        for (const scene::Override& each : instance.overrides) {
            if (std::ranges::any_of(each.fields, [entity](const scene::SceneField& field) {
                    return field.value.kind == scene::FieldValue::Kind::Entity && field.value.entity == entity;
                })) {
                return true;
            }
        }
    }
    return false;
}

/// The value a field input writes; none for the field's default.
result::Result<std::optional<scene::FieldValue>>
valueOf(const Operation& operation, const FieldInput& input, FieldKind kind) {
    using Kind = FieldInput::Kind;
    const bool kFits = input.kind == Kind::Default || (input.kind == Kind::Signed && kind == FieldKind::Signed) ||
                       (input.kind == Kind::Unsigned && kind == FieldKind::Unsigned) ||
                       (input.kind == Kind::Real && kind == FieldKind::Real) ||
                       (input.kind == Kind::Truth && kind == FieldKind::Truth);
    if (!kFits) {
        return invalid(operation, "a field's value is of the field's kind");
    }
    std::string text;
    switch (input.kind) {
    case Kind::Default:
        return std::optional<scene::FieldValue>{};
    case Kind::Truth:
        return input.truth ? std::optional{scene::FieldValue{.kind = scene::FieldValue::Kind::True}}
                           : std::optional<scene::FieldValue>{};
    case Kind::Signed:
        text = std::to_string(input.integer);
        break;
    case Kind::Unsigned:
        text = std::to_string(input.whole);
        break;
    case Kind::Real:
        if (!std::isfinite(input.real)) {
            return invalid(operation, "a real field's value is finite");
        }
        text = document::write(document::Value::real(input.real == 0.0 ? 0.0 : input.real));
        text.pop_back();
        break;
    }
    if (text == "0") {
        return std::optional<scene::FieldValue>{};
    }
    return std::optional{scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::move(text)}};
}

/// The component an operation names, present on the entity.
struct Target {
    const scene::SceneEntity* entity = nullptr;
    const ComponentSchema* schema = nullptr;
    const scene::SceneComponent* component = nullptr;
};

result::Result<Target> targetOf(const scene::Scene& scene,
                                const Operation& operation,
                                base::Bits128 entity,
                                schema::ComponentTypeId component,
                                const ComponentCatalog& catalog,
                                bool present) {
    Target made{.entity = entityOf(scene, entity), .schema = catalog.find(component), .component = nullptr};
    if (made.entity == nullptr) {
        return notFound(operation, "the scene has no such entity");
    }
    if (made.schema == nullptr) {
        return notFound(operation, "the catalog has no such component");
    }
    const auto kFound = std::ranges::find(made.entity->components, made.schema->name, &scene::SceneComponent::name);
    if (kFound != made.entity->components.end()) {
        made.component = &*kFound;
    }
    if (present && made.component == nullptr) {
        return notFound(operation, "the entity has no such component");
    }
    return made;
}

/// Validates `operation` against `scene` and derives its deltas, touching
/// nothing.
result::Result<Journal> derive(const scene::Scene& scene, const Operation& operation, const ComponentCatalog& catalog) {
    Journal journal;
    if (const auto* create = std::get_if<CreateEntity>(&operation)) {
        if (create->entity == base::Bits128{}) {
            return invalid(operation, "an entity's SourceEntityId is not nought");
        }
        if (entityOf(scene, create->entity) != nullptr) {
            return conflict(operation, "the scene already has an entity of that id");
        }
        const std::size_t kPlace = create->place.value_or(scene.entities.size());
        if (kPlace > scene.entities.size()) {
            return invalid(operation, "an entity is made at a place the scene has");
        }
        journal.push_back(
            Delta{.kind = DeltaKind::CreateNode,
                  .entity = create->entity,
                  .after = {.node = NodeRecord{.place = kPlace, .name = create->name, .components = {}}}});
    } else if (const auto* destroy = std::get_if<DestroyEntity>(&operation)) {
        const scene::SceneEntity* entity = entityOf(scene, destroy->entity);
        if (entity == nullptr) {
            return notFound(operation, "the scene has no such entity");
        }
        if (referenced(scene, destroy->entity)) {
            return conflict(operation, "an entity another names is not destroyed");
        }
        NodeRecord node{.place = placeOf(scene, destroy->entity), .name = entity->name, .components = {}};
        for (const scene::SceneComponent& component : entity->components) {
            node.components.push_back(recordOf(scene, component));
        }
        journal.push_back(
            Delta{.kind = DeltaKind::DestroyNode, .entity = destroy->entity, .before = {.node = std::move(node)}});
    } else if (const auto* rename = std::get_if<RenameEntity>(&operation)) {
        const scene::SceneEntity* entity = entityOf(scene, rename->entity);
        if (entity == nullptr) {
            return notFound(operation, "the scene has no such entity");
        }
        if (entity->name != rename->name) {
            journal.push_back(Delta{.kind = DeltaKind::SetName,
                                    .entity = rename->entity,
                                    .before = {.name = entity->name},
                                    .after = {.name = rename->name}});
        }
    } else if (const auto* move = std::get_if<MoveEntity>(&operation)) {
        if (entityOf(scene, move->entity) == nullptr) {
            return notFound(operation, "the scene has no such entity");
        }
        if (move->place >= scene.entities.size()) {
            return invalid(operation, "an entity moves to a place the scene has");
        }
        const std::size_t kFrom = placeOf(scene, move->entity);
        if (kFrom != move->place) {
            journal.push_back(Delta{.kind = DeltaKind::Reorder,
                                    .entity = move->entity,
                                    .before = {.place = kFrom},
                                    .after = {.place = move->place}});
        }
    } else if (const auto* add = std::get_if<AddComponent>(&operation)) {
        RAWFRAME_TRY_ASSIGN(const Target kTarget,
                            targetOf(scene, operation, add->entity, add->component, catalog, false));
        if (kTarget.component != nullptr) {
            return conflict(operation, "the entity already has that component");
        }
        const auto kMark = std::ranges::find(scene.schema, kTarget.schema->name, &scene::SchemaMark::component);
        if (kMark != scene.schema.end() && kMark->mark != kTarget.schema->mark) {
            return invalid(operation, "the scene was authored against another layout of that component");
        }
        journal.push_back(
            Delta{.kind = DeltaKind::AddComponent,
                  .entity = add->entity,
                  .component = kTarget.schema->name,
                  .after = {.component = ComponentRecord{.name = kTarget.schema->name, .mark = kTarget.schema->mark}}});
    } else if (const auto* remove = std::get_if<RemoveComponent>(&operation)) {
        RAWFRAME_TRY_ASSIGN(const Target kTarget,
                            targetOf(scene, operation, remove->entity, remove->component, catalog, true));
        journal.push_back(Delta{.kind = DeltaKind::RemoveComponent,
                                .entity = remove->entity,
                                .component = kTarget.schema->name,
                                .before = {.component = recordOf(scene, *kTarget.component)}});
    } else {
        const bool kReference = std::holds_alternative<SetReference>(operation);
        const base::Bits128 kEntity =
            kReference ? std::get<SetReference>(operation).entity : std::get<SetField>(operation).entity;
        const schema::ComponentTypeId kComponent =
            kReference ? std::get<SetReference>(operation).component : std::get<SetField>(operation).component;
        const std::string& fieldName =
            kReference ? std::get<SetReference>(operation).field : std::get<SetField>(operation).field;
        RAWFRAME_TRY_ASSIGN(const Target kTarget, targetOf(scene, operation, kEntity, kComponent, catalog, true));
        const auto kField = std::ranges::find(kTarget.schema->fields, fieldName, &FieldSchema::name);
        if (kField == kTarget.schema->fields.end()) {
            return notFound(operation, "the component has no such field");
        }
        if ((kField->kind == FieldKind::Reference) != kReference) {
            return invalid(operation, "an entity field is set by scene.set_reference, and only it");
        }
        std::optional<scene::FieldValue> after;
        if (kReference) {
            const std::optional<base::Bits128>& target = std::get<SetReference>(operation).target;
            if (target.has_value()) {
                if (entityOf(scene, *target) == nullptr) {
                    return invalid(operation, "a reference names an entity of the scene");
                }
                after = scene::FieldValue{.kind = scene::FieldValue::Kind::Entity, .entity = *target};
            }
        } else {
            RAWFRAME_TRY_ASSIGN(after, valueOf(operation, std::get<SetField>(operation).value, kField->kind));
        }
        std::optional<scene::FieldValue> before;
        const auto kNow = std::ranges::find(kTarget.component->fields, fieldName, &scene::SceneField::name);
        if (kNow != kTarget.component->fields.end()) {
            before = kNow->value;
        }
        if (before != after) {
            journal.push_back(Delta{.kind = kReference ? DeltaKind::SetReference : DeltaKind::SetField,
                                    .entity = kEntity,
                                    .component = kTarget.schema->name,
                                    .field = fieldName,
                                    .before = {.field = before},
                                    .after = {.field = after}});
        }
    }
    return journal;
}

/// The stale check, after addressing (SPEC-0040's order): an operation
/// whose targets do not resolve says so first.
result::Status fresh(const AuthoredScene& scene, std::uint64_t generation, const Operation& operation) {
    if (generation != scene.generation()) {
        return refuse(operation,
                      result::ErrorClass::FailedPrecondition,
                      AuthoringError::TargetStale,
                      "an operation is computed against the document's generation");
    }
    return {};
}

} // namespace

result::Status ComponentCatalog::add(ComponentSchema component) {
    std::set<std::string_view> names;
    for (const FieldSchema& field : component.fields) {
        if (field.name.empty() || !names.insert(field.name).second) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kAuthoringDomain,
                                code(AuthoringError::ValidationFailed),
                                "a component's fields have names, each once");
        }
    }
    if (component.name.empty() || std::ranges::contains(components_, component.id, &ComponentSchema::id) ||
        std::ranges::contains(components_, component.name, &ComponentSchema::name)) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kAuthoringDomain,
                            code(AuthoringError::ValidationFailed),
                            "a catalog has each component once, by id and by name");
    }
    components_.push_back(std::move(component));
    return {};
}

const ComponentSchema* ComponentCatalog::find(schema::ComponentTypeId id) const noexcept {
    const auto kFound = std::ranges::find(components_, id, &ComponentSchema::id);
    return kFound != components_.end() ? &*kFound : nullptr;
}

std::span<const OperationDeclaration> declarations() noexcept {
    return kDeclarations;
}

const OperationDeclaration& declarationOf(const Operation& operation) noexcept {
    return kDeclarations[operation.index()];
}

result::Result<Committed> execute(AuthoredScene& scene,
                                  std::uint64_t generation,
                                  const Operation& operation,
                                  const ComponentCatalog& catalog,
                                  Mode mode) {
    // Addressing, then staleness, then meaning, then staging.
    auto journal = derive(scene.scene(), operation, catalog);
    if (!journal.has_value() && journal.error().code() == code(AuthoringError::TargetNotFound)) {
        return std::unexpected<result::Error>{std::move(journal).error()};
    }
    RAWFRAME_TRY(fresh(scene, generation, operation));
    RAWFRAME_TRY_ASSIGN(const Journal kJournal, std::move(journal));
    if (mode == Mode::DryRun) {
        return Committed{.generation = generation, .deltas = 0};
    }
    RAWFRAME_TRY_ASSIGN(Transaction transaction, scene.begin(generation));
    for (const Delta& delta : kJournal) {
        RAWFRAME_TRY(transaction.stage(delta));
    }
    return transaction.commit();
}

result::Status stage(Transaction& transaction, const Operation& operation, const ComponentCatalog& catalog) {
    auto journal = derive(transaction.staged(), operation, catalog);
    if (!journal.has_value()) {
        transaction.cancel();
        return std::unexpected<result::Error>{std::move(journal).error()};
    }
    for (const Delta& delta : *journal) {
        RAWFRAME_TRY(transaction.stage(delta));
    }
    return {};
}

result::Result<Committed> executeAtomic(AuthoredScene& scene,
                                        std::uint64_t generation,
                                        std::span<const Operation> operations,
                                        const ComponentCatalog& catalog) {
    if (operations.size() > kMaximumBatchOperations) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kAuthoringDomain,
                            code(AuthoringError::LimitExceeded),
                            "a batch has more operations than its limit");
    }
    RAWFRAME_TRY_ASSIGN(Transaction transaction, scene.begin(generation));
    for (std::size_t at = 0; at < operations.size(); ++at) {
        auto staged = stage(transaction, operations[at], catalog);
        if (!staged.has_value()) {
            return std::unexpected<result::Error>{std::move(staged).error().withContext("index", std::to_string(at))};
        }
    }
    return transaction.commit();
}

IndependentOutcome executeIndependent(AuthoredScene& scene,
                                      std::uint64_t generation,
                                      std::span<const Operation> operations,
                                      const ComponentCatalog& catalog,
                                      OnFailure onFailure) {
    IndependentOutcome made;
    if (operations.size() > kMaximumBatchOperations) {
        made.outcomes.emplace_back(result::fail(result::ErrorClass::ResourceExhausted,
                                                kAuthoringDomain,
                                                code(AuthoringError::LimitExceeded),
                                                "a batch has more operations than its limit"));
        made.skipped = operations.size();
        return made;
    }
    std::uint64_t now = generation;
    for (std::size_t at = 0; at < operations.size(); ++at) {
        auto outcome = execute(scene, now, operations[at], catalog);
        const bool kFailed = !outcome.has_value();
        if (!kFailed) {
            now = outcome->generation;
        }
        made.outcomes.push_back(std::move(outcome));
        if (kFailed && onFailure == OnFailure::HaltRemaining) {
            made.skipped = operations.size() - at - 1;
            break;
        }
    }
    return made;
}

} // namespace rawframe::authoring
