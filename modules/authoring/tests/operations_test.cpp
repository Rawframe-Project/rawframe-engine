// Authoring operations (SPEC-0040): components addressed by type id through
// the catalog, fields by their layout's names, values typed; validation in
// SPEC-0040's order with DryRun agreeing with execution; atomic and
// independent batches; and every failure one of the closed classes.

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/operations.h"
#include "rawframe/test/test.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

constexpr schema::ComponentTypeId kPosition = schema::ComponentTypeId::fromText("5f3a0c2d-9e81-4b74-8a1d-000000000001");
constexpr schema::ComponentTypeId kLink = schema::ComponentTypeId::fromText("5f3a0c2d-9e81-4b74-8a1d-000000000002");
constexpr schema::ComponentTypeId kUnknown = schema::ComponentTypeId::fromText("5f3a0c2d-9e81-4b74-8a1d-000000000009");
const base::Bits128 kSpawn{1, 1};
const base::Bits128 kDoor{1, 2};

ComponentCatalog catalog() {
    ComponentCatalog made;
    RAWFRAME_EXPECT(made.add(ComponentSchema{.id = kPosition,
                                             .name = "game.position",
                                             .mark = 0xa1,
                                             .fields = {{.name = "x", .kind = FieldKind::Real},
                                                        {.name = "count", .kind = FieldKind::Unsigned},
                                                        {.name = "lit", .kind = FieldKind::Truth}}})
                        .has_value());
    RAWFRAME_EXPECT(made.add(ComponentSchema{.id = kLink,
                                             .name = "game.link",
                                             .mark = 0xb2,
                                             .fields = {{.name = "target", .kind = FieldKind::Reference},
                                                        {.name = "offset", .kind = FieldKind::Signed}}})
                        .has_value());
    return made;
}

std::unique_ptr<AuthoredScene> empty() {
    const scene::Scene kNothing{};
    auto made = AuthoredScene::open(base::Bits128{7, 7}, *scene::writeScene(kNothing));
    RAWFRAME_EXPECT(made.has_value());
    return std::move(*made);
}

FieldInput real(double value) {
    return FieldInput{.kind = FieldInput::Kind::Real, .real = value};
}

/// A spawn point at x 1.5 and a door linked to it, in one atomic batch.
std::vector<Operation> level() {
    return {CreateEntity{.entity = kSpawn, .name = "spawn"},
            AddComponent{.entity = kSpawn, .component = kPosition},
            SetField{.entity = kSpawn, .component = kPosition, .field = "x", .value = real(1.5)},
            CreateEntity{.entity = kDoor, .name = "door"},
            AddComponent{.entity = kDoor, .component = kLink},
            SetReference{.entity = kDoor, .component = kLink, .field = "target", .target = kSpawn}};
}

} // namespace

RAWFRAME_TEST(OperationsBuildASceneByIdentity) {
    const auto kScene = empty();
    const ComponentCatalog kCatalog = catalog();
    const auto kBuilt = executeAtomic(*kScene, 0, level(), kCatalog);
    RAWFRAME_EXPECT(kBuilt.has_value() && kBuilt->generation == 1 && kBuilt->deltas == 6);
    const scene::Scene& built = kScene->scene();
    RAWFRAME_EXPECT(built.entities.size() == 2 && built.schema.size() == 2 && built.schema[1].mark == 0xa1 &&
                    built.entities[0].components[0].fields[0].value.number == "1.5" &&
                    built.entities[1].components[0].fields[0].value.entity == kSpawn);
    // One entry: one undo takes the whole batch back.
    RAWFRAME_EXPECT(kScene->undo(1).has_value() && kScene->scene().entities.empty());
    RAWFRAME_EXPECT(kScene->redo(2).has_value() && kScene->scene() == built);
    // Values are typed and a default is no field at all.
    const auto kSet = [&](FieldInput value, std::string_view field) {
        return execute(*kScene,
                       kScene->generation(),
                       SetField{.entity = kSpawn, .component = kPosition, .field = std::string{field}, .value = value},
                       kCatalog);
    };
    RAWFRAME_EXPECT(kSet(FieldInput{.kind = FieldInput::Kind::Unsigned, .whole = 18446744073709551615ULL}, "count"));
    RAWFRAME_EXPECT(kSet(FieldInput{.kind = FieldInput::Kind::Truth, .truth = true}, "lit"));
    RAWFRAME_EXPECT(kScene->scene().entities[0].components[0].fields.size() == 3);
    RAWFRAME_EXPECT(kSet(real(0.0), "x").has_value() && kSet(real(-0.0), "x").has_value());
    RAWFRAME_EXPECT(kSet(FieldInput{}, "count") && kSet(FieldInput{.kind = FieldInput::Kind::Truth}, "lit"));
    RAWFRAME_EXPECT(kScene->scene().entities[0].components[0].fields.empty());
    // Setting what is there is a no-op: no entry, no generation.
    const std::uint64_t kNow = kScene->generation();
    const auto kSame = kSet(FieldInput{}, "x");
    RAWFRAME_EXPECT(kSame.has_value() && kSame->deltas == 0 && kScene->generation() == kNow);
}

RAWFRAME_TEST(FailuresAreTypedAndLeaveNothing) {
    const auto kScene = empty();
    const ComponentCatalog kCatalog = catalog();
    RAWFRAME_EXPECT(executeAtomic(*kScene, 0, level(), kCatalog).has_value());
    const std::string kBefore = kScene->text();
    const auto kRun = [&](const Operation& operation) {
        return execute(*kScene, 1, operation, kCatalog);
    };
    // Addressing.
    RAWFRAME_EXPECT(refusedWith(kRun(DestroyEntity{.entity = {9, 9}}), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(
        refusedWith(kRun(AddComponent{.entity = kSpawn, .component = kUnknown}), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(
        refusedWith(kRun(RemoveComponent{.entity = kSpawn, .component = kLink}), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(
        refusedWith(kRun(SetField{.entity = kSpawn, .component = kPosition, .field = "y", .value = real(1)}),
                    AuthoringError::TargetNotFound));
    // Meaning.
    RAWFRAME_EXPECT(refusedWith(kRun(CreateEntity{.entity = kSpawn}), AuthoringError::Conflict));
    RAWFRAME_EXPECT(refusedWith(kRun(DestroyEntity{.entity = kSpawn}), AuthoringError::Conflict));
    RAWFRAME_EXPECT(
        refusedWith(kRun(AddComponent{.entity = kSpawn, .component = kPosition}), AuthoringError::Conflict));
    RAWFRAME_EXPECT(refusedWith(kRun(CreateEntity{.entity = {}}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kRun(CreateEntity{.entity = {3, 3}, .place = 5}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kRun(MoveEntity{.entity = kSpawn, .place = 2}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kRun(SetField{.entity = kSpawn,
                                              .component = kPosition,
                                              .field = "x",
                                              .value = FieldInput{.kind = FieldInput::Kind::Signed, .integer = 1}}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(kRun(SetField{.entity = kSpawn,
                                              .component = kPosition,
                                              .field = "x",
                                              .value = real(std::numeric_limits<double>::infinity())}),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(kRun(SetField{.entity = kDoor, .component = kLink, .field = "target", .value = FieldInput{}}),
                    AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(
        kRun(SetReference{.entity = kDoor, .component = kLink, .field = "target", .target = base::Bits128{9, 9}}),
        AuthoringError::ValidationFailed));
    // The failing operation is named.
    const auto kNamed = kRun(DestroyEntity{.entity = kSpawn});
    bool named = false;
    for (const auto& field : kNamed.error().context()) {
        named = named || (field.key == "operation" && field.value == "scene.destroy_entity");
    }
    RAWFRAME_EXPECT(named);
    RAWFRAME_EXPECT(kScene->text() == kBefore && kScene->generation() == 1);
}

RAWFRAME_TEST(StalenessComesAfterAddressingAndDryRunAgrees) {
    const auto kScene = empty();
    const ComponentCatalog kCatalog = catalog();
    RAWFRAME_EXPECT(executeAtomic(*kScene, 0, level(), kCatalog).has_value());
    // A target that does not resolve says so even at a stale generation;
    // one that does is stale.
    RAWFRAME_EXPECT(
        refusedWith(execute(*kScene, 0, DestroyEntity{.entity = {9, 9}}, kCatalog), AuthoringError::TargetNotFound));
    const Operation kMove = MoveEntity{.entity = kDoor, .place = 0};
    RAWFRAME_EXPECT(refusedWith(execute(*kScene, 0, kMove, kCatalog), AuthoringError::TargetStale));
    // DryRun: the same answer as execution's validation, and nothing
    // staged.
    for (const Operation& each : {kMove,
                                  Operation{DestroyEntity{.entity = kSpawn}},
                                  Operation{CreateEntity{.entity = kSpawn}},
                                  Operation{RenameEntity{.entity = {9, 9}, .name = "x"}}}) {
        const std::string kBefore = kScene->text();
        const auto kDry = execute(*kScene, 1, each, kCatalog, Mode::DryRun);
        RAWFRAME_EXPECT(kScene->text() == kBefore && kScene->generation() == 1);
        const auto kStale = execute(*kScene, 0, each, kCatalog, Mode::DryRun);
        if (kDry.has_value()) {
            RAWFRAME_EXPECT(kDry->deltas == 0 && refusedWith(kStale, AuthoringError::TargetStale));
        } else {
            RAWFRAME_EXPECT(!kStale.has_value());
        }
    }
    RAWFRAME_EXPECT(execute(*kScene, 1, kMove, kCatalog).has_value() && kScene->scene().entities[0].id == kDoor);
}

RAWFRAME_TEST(BatchesAreAtomicOrIndependentAsDeclared) {
    const auto kScene = empty();
    const ComponentCatalog kCatalog = catalog();
    // Atomic: the sixth operation fails, and nothing of the five stays.
    std::vector<Operation> broken;
    broken.reserve(level().size() + 1);
    for (const Operation& each : level()) {
        broken.push_back(each);
    }
    broken.emplace_back(DestroyEntity{.entity = kSpawn});
    const auto kAtomic = executeAtomic(*kScene, 0, broken, kCatalog);
    RAWFRAME_EXPECT(refusedWith(kAtomic, AuthoringError::Conflict));
    RAWFRAME_EXPECT(kScene->scene().entities.empty() && kScene->generation() == 0 && !kScene->canUndo());
    // Independent, continuing: each its own entry, the failure in its slot.
    const std::vector<Operation> kMixed = {CreateEntity{.entity = kSpawn},
                                           CreateEntity{.entity = kSpawn},
                                           RenameEntity{.entity = kSpawn, .name = "spawn"}};
    const IndependentOutcome kGoing = executeIndependent(*kScene, 0, kMixed, kCatalog, OnFailure::ContinuePerItem);
    RAWFRAME_EXPECT(kGoing.outcomes.size() == 3 && kGoing.outcomes[0].has_value() &&
                    refusedWith(kGoing.outcomes[1], AuthoringError::Conflict) && kGoing.outcomes[2].has_value() &&
                    kGoing.skipped == 0 && kScene->undoable() == 2);
    // Halting: the rest are skipped and counted.
    const std::vector<Operation> kHalting = {DestroyEntity{.entity = {9, 9}},
                                             RenameEntity{.entity = kSpawn, .name = "a"},
                                             RenameEntity{.entity = kSpawn, .name = "b"}};
    const IndependentOutcome kHalted =
        executeIndependent(*kScene, kScene->generation(), kHalting, kCatalog, OnFailure::HaltRemaining);
    RAWFRAME_EXPECT(kHalted.outcomes.size() == 1 && kHalted.skipped == 2 &&
                    kScene->scene().entities[0].name == "spawn");
}

RAWFRAME_TEST(DiscoveryDeclaresEveryOperationWhole) {
    RAWFRAME_EXPECT(declarations().size() == std::variant_size_v<Operation>);
    for (const OperationDeclaration& each : declarations()) {
        RAWFRAME_EXPECT(!each.name.empty() && !each.inputs.empty() && !each.targets.empty() &&
                        each.history == HistoryClass::Undoable && each.since == kSurfaceGeneration);
    }
    RAWFRAME_EXPECT(declarationOf(SetReference{}).name == "scene.set_reference");
    // A catalog refuses a component twice, by id or by name, and a field
    // named twice.
    ComponentCatalog made = catalog();
    RAWFRAME_EXPECT(
        refusedWith(made.add(ComponentSchema{.id = kPosition, .name = "other"}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(
        refusedWith(made.add(ComponentSchema{.id = kUnknown, .name = "game.link"}), AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(
        made.add(ComponentSchema{.id = kUnknown, .name = "game.new", .fields = {{.name = "a"}, {.name = "a"}}}),
        AuthoringError::ValidationFailed));
}

RAWFRAME_TEST(AComponentIsRemarkedOnlyWhenEveryFieldCarriesOver) {
    // A scene authored against an older layout of game.position.
    const scene::Scene kOld{
        .schema = {{.component = "game.position", .mark = 0x11}},
        .entities =
            {{.id = kSpawn,
              .name = {},
              .components =
                  {{.name = "game.position",
                    .fields = {{.name = "count", .value = {.kind = scene::FieldValue::Kind::Number, .number = "3"}},
                               {.name = "x", .value = {.kind = scene::FieldValue::Kind::Number, .number = "1.5"}}}}}}},
        .instances = {}};
    auto opened = AuthoredScene::open(base::Bits128{7, 7}, *scene::writeScene(kOld));
    AuthoredScene& scene = **opened;
    const ComponentCatalog kCatalog = catalog();
    const Operation kRemark = RemarkComponent{.component = kPosition};
    const auto kDone = execute(scene, 0, kRemark, kCatalog);
    RAWFRAME_EXPECT(kDone.has_value() && kDone->deltas == 1 && scene.scene().schema[0].mark == 0xa1 &&
                    scene.scene().entities == kOld.entities);
    // Current already: nothing to do.
    const auto kAgain = execute(scene, 1, kRemark, kCatalog);
    RAWFRAME_EXPECT(kAgain.has_value() && kAgain->deltas == 0 && scene.generation() == 1);
    RAWFRAME_EXPECT(scene.undo(1).has_value() && scene.scene() == kOld);
    // x is now an integer, and count is gone: both are residual work.
    ComponentCatalog changed;
    RAWFRAME_EXPECT(changed.add(ComponentSchema{
        .id = kPosition, .name = "game.position", .mark = 0xa2, .fields = {{.name = "x", .kind = FieldKind::Signed}}}));
    const auto kRefused = execute(scene, 2, kRemark, changed);
    RAWFRAME_EXPECT(refusedWith(kRefused, AuthoringError::ValidationFailed) && scene.scene() == kOld);
    bool named = false;
    for (const auto& field : kRefused.error().context()) {
        named = named || (field.key == "residual" && field.value == "count x");
    }
    RAWFRAME_EXPECT(named);
    // A component the scene never recorded, or the catalog does not know.
    RAWFRAME_EXPECT(
        refusedWith(execute(scene, 2, RemarkComponent{.component = kLink}, kCatalog), AuthoringError::TargetNotFound));
    RAWFRAME_EXPECT(refusedWith(execute(scene, 2, RemarkComponent{.component = kUnknown}, kCatalog),
                                AuthoringError::TargetNotFound));
}
