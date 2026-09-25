// Read operations (SPEC-0040): every entity a scene holds, its own and its
// instances', and one entity whole, typed through the catalog; what the
// catalog does not know answered and marked, never dropped; and an answer's
// values in the form a request sets them with, so an agent's next request
// is built from the answer alone.

#include "rawframe/authoring/errors.h"
#include "rawframe/authoring/queries.h"
#include "rawframe/authoring/request.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

constexpr schema::ComponentTypeId kPosition = schema::ComponentTypeId::fromText("5f3a0c2d-9e81-4b74-8a1d-000000000001");
constexpr schema::ComponentTypeId kLink = schema::ComponentTypeId::fromText("5f3a0c2d-9e81-4b74-8a1d-000000000002");
const base::Bits128 kSpawn{1, 1};
const base::Bits128 kCrate{1, 4};
const base::Bits128 kShelf{1, 5};

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
                                             .fields = {{.name = "target", .kind = FieldKind::Reference}}})
                        .has_value());
    return made;
}

scene::FieldValue number(std::string_view text) {
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::string{text}};
}

/// A spawn point holding a component the game no longer has and a field its
/// layout lost, and a storeroom instance whose crate is moved and linked
/// and whose shelf is removed.
scene::Scene room() {
    return scene::Scene{
        .schema = {{.component = "game.link", .mark = 0xb2},
                   {.component = "game.old", .mark = 0x01},
                   {.component = "game.position", .mark = 0xa1}},
        .entities = {{.id = kSpawn,
                      .name = "spawn",
                      .components = {{.name = "game.old", .fields = {{.name = "a", .value = number("1")}}},
                                     {.name = "game.position",
                                      .fields = {{.name = "lit", .value = {.kind = scene::FieldValue::Kind::True}},
                                                 {.name = "x", .value = number("1.5")},
                                                 {.name = "z", .value = number("2")}}}}}},
        .instances = {
            {.scene = base::Bits128{9, 1},
             .entities = {{.source = base::Bits128{7, 1}, .instance = kCrate},
                          {.source = base::Bits128{7, 2}, .instance = kShelf}},
             .overrides = {
                 {.entity = kCrate,
                  .component = "game.link",
                  .kind = scene::Override::Kind::Add,
                  .fields = {{.name = "target", .value = {.kind = scene::FieldValue::Kind::Entity, .entity = kSpawn}}}},
                 {.entity = kCrate,
                  .component = "game.position",
                  .kind = scene::Override::Kind::Set,
                  .fields = {{.name = "count", .value = number("0")}}},
                 {.entity = kShelf, .component = {}, .kind = scene::Override::Kind::Remove}}}}};
}

/// A field's value in an answer, by component and field place.
const document::Value& valueAt(const document::Value& reading, std::size_t component, std::size_t field) {
    return *reading.find("components")->items()[component].find("fields")->items()[field].find("value");
}

bool same(const document::Value& value, std::string_view text) {
    const auto kExpected = document::parse(text);
    RAWFRAME_EXPECT(kExpected.has_value());
    return kExpected.has_value() && document::write(value) == document::write(*kExpected);
}

} // namespace

RAWFRAME_TEST(EveryEntityIsListedOwnThenBrought) {
    const auto kList = answer(room(), ListEntities{}, catalog());
    RAWFRAME_EXPECT(kList.has_value());
    const std::vector<EntityEntry>& entities = std::get<EntityList>(*kList).entities;
    RAWFRAME_EXPECT(entities.size() == 3 && entities[0].id == kSpawn && entities[0].name == "spawn" &&
                    entities[0].place == 0 && !entities[0].brought.has_value());
    const Brought kFromStoreroom{.instance = 0, .scene = {9, 1}, .source = {7, 1}};
    RAWFRAME_EXPECT(entities[1].id == kCrate && !entities[1].place.has_value() && !entities[1].removed &&
                    entities[1].brought == kFromStoreroom);
    RAWFRAME_EXPECT(entities[2].id == kShelf && entities[2].removed);
}

RAWFRAME_TEST(AnEntityIsReadWholeAndWhatIsUnknownIsMarked) {
    const ComponentCatalog kCatalog = catalog();
    const auto kSpawnRead = answer(room(), ReadEntity{.entity = kSpawn}, kCatalog);
    RAWFRAME_EXPECT(kSpawnRead.has_value());
    const EntityReading& spawn = std::get<EntityReading>(*kSpawnRead);
    RAWFRAME_EXPECT(spawn.components.size() == 2 && !spawn.components[0].component.has_value() &&
                    spawn.components[0].name == "game.old" && spawn.components[1].component == kPosition &&
                    !spawn.components[1].patch.has_value());
    const std::vector<FieldReading>& fields = spawn.components[1].fields;
    RAWFRAME_EXPECT(fields.size() == 3 && fields[0].kind == FieldKind::Truth && fields[1].kind == FieldKind::Real &&
                    !fields[2].kind.has_value());

    const auto kCrateRead = answer(room(), ReadEntity{.entity = kCrate}, kCatalog);
    const EntityReading& crate = std::get<EntityReading>(*kCrateRead);
    RAWFRAME_EXPECT(crate.components.size() == 2 && crate.components[0].patch == scene::Override::Kind::Add &&
                    crate.components[1].patch == scene::Override::Kind::Set &&
                    crate.components[1].fields[0].value.number == "0");
    const auto kShelfRead = answer(room(), ReadEntity{.entity = kShelf}, kCatalog);
    RAWFRAME_EXPECT(kShelfRead.has_value() && std::get<EntityReading>(*kShelfRead).entity.removed &&
                    std::get<EntityReading>(*kShelfRead).components.empty());

    const auto kNobody = answer(room(), ReadEntity{.entity = base::Bits128{8, 8}}, kCatalog);
    RAWFRAME_EXPECT(refusedWith(kNobody, AuthoringError::TargetNotFound));
    bool named = false;
    for (const auto& field : kNobody.error().context()) {
        named = named || (field.key == "operation" && field.value == "scene.read_entity");
    }
    RAWFRAME_EXPECT(named);
}

RAWFRAME_TEST(AnAnswersValuesAreWhatARequestSets) {
    const ComponentCatalog kCatalog = catalog();
    const document::Value kSpawnAnswer = answerValue(*answer(room(), ReadEntity{.entity = kSpawn}, kCatalog));
    RAWFRAME_EXPECT(kSpawnAnswer.find("components")->items()[0].find("component")->isNull());
    RAWFRAME_EXPECT(same(valueAt(kSpawnAnswer, 1, 0), R"({"truth": true})") &&
                    same(valueAt(kSpawnAnswer, 1, 1), R"({"real": 1.5})") &&
                    same(valueAt(kSpawnAnswer, 1, 2), R"({"recorded": {"number": "2"}})"));
    const document::Value kCrateAnswer = answerValue(*answer(room(), ReadEntity{.entity = kCrate}, kCatalog));
    RAWFRAME_EXPECT(*kCrateAnswer.find("components")->items()[0].find("patch")->text() == "add" &&
                    same(valueAt(kCrateAnswer, 0, 0), R"({"entity": "00000000-0000-0001-0000-000000000001"})") &&
                    same(valueAt(kCrateAnswer, 1, 0), R"({"unsigned": "0"})"));
    RAWFRAME_EXPECT(same(*kCrateAnswer.find("entity"), R"({"id": "00000000-0000-0001-0000-000000000004",
        "brought": {"instance": 0, "scene": "00000000000000090000000000000001",
        "source": "00000000-0000-0007-0000-000000000001"}, "removed": false})"));

    // The value read back, set again, changes nothing.
    auto opened = AuthoredScene::open(base::Bits128{7, 7}, *scene::writeScene(room()));
    AuthoredScene& scene = **opened;
    const auto kRequest = readRequest(R"({"formatVersion": 1, "kind": "authoring.request", "batch": "atomic",
        "operations": [{"operation": "scene.set_field", "entity": "00000000-0000-0001-0000-000000000001",
        "component": "5f3a0c2d-9e81-4b74-8a1d-000000000001", "field": "x", "value": {"real": 1.5}}]})");
    RAWFRAME_EXPECT(kRequest.has_value());
    const auto kSame = executeAtomic(scene, 0, kRequest->operations, kCatalog);
    RAWFRAME_EXPECT(kSame.has_value() && kSame->deltas == 0);
}

RAWFRAME_TEST(QueriesAndRequestsAreKeptApart) {
    const auto kRead = readQueries(R"({"formatVersion": 1, "kind": "authoring.query", "queries": [
        {"operation": "scene.list_entities"},
        {"operation": "scene.read_entity", "entity": "00000000-0000-0001-0000-000000000001"}]})");
    RAWFRAME_EXPECT(kRead.has_value() && kRead->size() == 2 && std::holds_alternative<ListEntities>((*kRead)[0]) &&
                    std::get<ReadEntity>((*kRead)[1]).entity == kSpawn);
    RAWFRAME_EXPECT(refusedWith(readQueries(R"({"formatVersion": 1, "kind": "authoring.query", "queries": [
        {"operation": "scene.destroy_entity", "entity": "00000000-0000-0001-0000-000000000001"}]})"),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readQueries(R"({"formatVersion": 1, "kind": "authoring.query", "queries": [
        {"operation": "scene.list_entities", "entity": "00000000-0000-0001-0000-000000000001"}]})"),
                                AuthoringError::ValidationFailed));
    RAWFRAME_EXPECT(refusedWith(readRequest(R"({"formatVersion": 1, "kind": "authoring.request", "batch": "atomic",
        "operations": [{"operation": "scene.list_entities"}]})"),
                                AuthoringError::ValidationFailed));

    // Discovery declares queries read-only and, given a catalog, publishes
    // the components a request names by id.
    const std::string kPlain = writeDiscovery();
    RAWFRAME_EXPECT(kPlain.find(R"("history": "read_only")") != std::string::npos &&
                    kPlain.find(R"("components")") == std::string::npos);
    const ComponentCatalog kCatalog = catalog();
    const std::string kWithCatalog = writeDiscovery(&kCatalog);
    RAWFRAME_EXPECT(kWithCatalog.find(R"("id": "5f3a0c2d-9e81-4b74-8a1d-000000000002")") != std::string::npos &&
                    kWithCatalog.find(R"("mark": "00000000000000b2")") != std::string::npos &&
                    kWithCatalog.find(R"("kind": "reference")") != std::string::npos);
}
