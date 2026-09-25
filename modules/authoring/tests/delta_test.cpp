// Document deltas (SPEC-0040): every kind applied forward then backward
// gives the scene back byte for byte, a delta lands only on the slot it was
// made against, the schema's marks follow the components used, and a
// journal is read in its one canonical form or refused whole.

#include "rawframe/authoring/delta.h"
#include "rawframe/authoring/errors.h"
#include "rawframe/test/test.h"

#include <string>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::authoring;

namespace {

bool refusedWith(const auto& outcome, AuthoringError error) {
    return !outcome.has_value() && outcome.error().domain() == kAuthoringDomain &&
           outcome.error().code() == code(error);
}

const base::Bits128 kSpawn{1, 1};
const base::Bits128 kDoor{1, 2};
const base::Bits128 kLamp{1, 3};

scene::FieldValue number(std::string_view text) {
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::string{text}};
}

scene::FieldValue to(base::Bits128 entity) {
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Entity, .entity = entity};
}

/// A spawn point at (1, 2), and a door linked to it.
scene::Scene level() {
    return scene::Scene{
        .schema = {{.component = "game.link", .mark = 0xb2}, {.component = "game.position", .mark = 0xa1}},
        .entities = {{.id = kSpawn,
                      .name = "spawn point",
                      .components = {{.name = "game.position",
                                      .fields = {{.name = "x", .value = number("1")},
                                                 {.name = "y", .value = number("2")}}}}},
                     {.id = kDoor,
                      .name = {},
                      .components = {{.name = "game.link", .fields = {{.name = "target", .value = to(kSpawn)}}}}}},
        .instances = {}};
}

std::string bytes(const scene::Scene& scene) {
    const auto kWritten = scene::writeScene(scene);
    RAWFRAME_EXPECT(kWritten.has_value());
    return kWritten.value_or("");
}

/// A lamp at the end: a new component, so a new mark.
Delta makeLamp() {
    return Delta{.kind = DeltaKind::CreateNode,
                 .entity = kLamp,
                 .after = {.node = NodeRecord{
                               .place = 2,
                               .name = "lamp",
                               .components = {ComponentRecord{
                                   .name = "game.light",
                                   .mark = 0xc3,
                                   .fields = {{.name = "on", .value = {.kind = scene::FieldValue::Kind::True}}}}}}}};
}

std::vector<Delta> everyKind() {
    const scene::Scene kLevel = level();
    return {
        makeLamp(),
        Delta{.kind = DeltaKind::DestroyNode,
              .entity = kDoor,
              .before = {.node = NodeRecord{.place = 1,
                                            .name = {},
                                            .components = {ComponentRecord{
                                                .name = "game.link",
                                                .mark = 0xb2,
                                                .fields = kLevel.entities[1].components[0].fields}}}}},
        Delta{.kind = DeltaKind::Reorder, .entity = kDoor, .before = {.place = 1}, .after = {.place = 0}},
        Delta{.kind = DeltaKind::SetName, .entity = kSpawn, .before = {.name = "spawn point"}, .after = {.name = ""}},
        Delta{.kind = DeltaKind::AddComponent,
              .entity = kDoor,
              .component = "game.position",
              .after = {.component = ComponentRecord{.name = "game.position",
                                                     .mark = 0xa1,
                                                     .fields = {{.name = "x", .value = number("4")}}}}},
        Delta{.kind = DeltaKind::RemoveComponent,
              .entity = kSpawn,
              .component = "game.position",
              .before = {.component = ComponentRecord{.name = "game.position",
                                                      .mark = 0xa1,
                                                      .fields = kLevel.entities[0].components[0].fields}}},
        Delta{.kind = DeltaKind::SetField,
              .entity = kSpawn,
              .component = "game.position",
              .field = "x",
              .before = {.field = number("1")},
              .after = {.field = number("-2.5")}},
        Delta{.kind = DeltaKind::SetField,
              .entity = kSpawn,
              .component = "game.position",
              .field = "z",
              .after = {.field = number("7")}},
        Delta{.kind = DeltaKind::SetReference,
              .entity = kDoor,
              .component = "game.link",
              .field = "target",
              .before = {.field = to(kSpawn)},
              .after = {.field = to(kDoor)}},
        Delta{.kind = DeltaKind::SetMark,
              .component = "game.position",
              .before = {.mark = 0xa1},
              .after = {.mark = 0xa9}},
    };
}

} // namespace

RAWFRAME_TEST(EveryKindGoesForwardAndBackByteForByte) {
    const std::string kOriginal = bytes(level());
    for (const Delta& delta : everyKind()) {
        scene::Scene scene = level();
        RAWFRAME_EXPECT(apply(scene, delta, true).has_value());
        const std::string kChanged = bytes(scene);
        RAWFRAME_EXPECT(!kChanged.empty() && kChanged != kOriginal);
        RAWFRAME_EXPECT(apply(scene, delta, false).has_value());
        RAWFRAME_EXPECT(bytes(scene) == kOriginal);
        // Backward first, then forward, where the backward state exists: a
        // delta applied backward to the original is a mismatch instead.
        scene::Scene again = level();
        RAWFRAME_EXPECT(refusedWith(apply(again, delta, false), AuthoringError::DeltaMismatch));
        RAWFRAME_EXPECT(bytes(again) == kOriginal);
    }
}

RAWFRAME_TEST(SchemaMarksFollowTheComponentsUsed) {
    scene::Scene scene = level();
    RAWFRAME_EXPECT(apply(scene, makeLamp(), true).has_value());
    RAWFRAME_EXPECT(scene.schema.size() == 3 && scene.schema[0].component == "game.light" &&
                    scene.schema[0].mark == 0xc3);
    RAWFRAME_EXPECT(apply(scene, makeLamp(), false).has_value() && scene.schema.size() == 2);
    // The door's link is the last use of game.link.
    const Delta kUnlink = everyKind()[1];
    RAWFRAME_EXPECT(apply(scene, kUnlink, true).has_value() && scene.schema.size() == 1 &&
                    scene.schema[0].component == "game.position");
    // A mark the schema holds otherwise is refused, and nothing changes.
    Delta otherMark = everyKind()[4];
    otherMark.after.component->mark = 0xff;
    scene::Scene same = level();
    RAWFRAME_EXPECT(refusedWith(apply(same, otherMark, true), AuthoringError::DeltaMismatch));
    RAWFRAME_EXPECT(same == level());
}

RAWFRAME_TEST(ADeltaLandsOnlyWhereItWasMade) {
    scene::Scene scene = level();
    // Made twice; a field whose value moved on; a field of a component the
    // entity lacks; an entity the scene lacks; a place past the end.
    RAWFRAME_EXPECT(apply(scene, makeLamp(), true).has_value());
    RAWFRAME_EXPECT(refusedWith(apply(scene, makeLamp(), true), AuthoringError::DeltaMismatch));
    Delta stale = everyKind()[6];
    stale.before.field = number("9");
    RAWFRAME_EXPECT(refusedWith(apply(scene, stale, true), AuthoringError::DeltaMismatch));
    Delta elsewhere = everyKind()[6];
    elsewhere.entity = kDoor;
    RAWFRAME_EXPECT(refusedWith(apply(scene, elsewhere, true), AuthoringError::DeltaMismatch));
    Delta nobody = everyKind()[3];
    nobody.entity = base::Bits128{9, 9};
    RAWFRAME_EXPECT(refusedWith(apply(scene, nobody, true), AuthoringError::DeltaMismatch));
    Delta far = everyKind()[2];
    far.after.place = 7;
    RAWFRAME_EXPECT(refusedWith(apply(scene, far, true), AuthoringError::DeltaMismatch));
    // Out of the kind's shape.
    Delta shapeless = everyKind()[6];
    shapeless.after.name = "x";
    RAWFRAME_EXPECT(refusedWith(apply(scene, shapeless, true), AuthoringError::DeltaInvalid));
    Delta wrongValue = everyKind()[8];
    wrongValue.after.field = number("1");
    RAWFRAME_EXPECT(refusedWith(apply(scene, wrongValue, true), AuthoringError::DeltaInvalid));
    // A journal is all or none.
    scene::Scene whole = level();
    const Journal kHalfGood = {everyKind()[6], stale};
    RAWFRAME_EXPECT(refusedWith(apply(whole, kHalfGood, true), AuthoringError::DeltaMismatch));
    RAWFRAME_EXPECT(whole == level());
}

RAWFRAME_TEST(AJournalRoundTripsAndAppliesBothWays) {
    // Every kind that can follow the one before it, in one journal.
    const std::vector<Delta> kAll = everyKind();
    const Journal kJournal = {kAll[0], kAll[3], kAll[6], kAll[7], kAll[8], kAll[2], kAll[4]};
    scene::Scene scene = level();
    RAWFRAME_EXPECT(apply(scene, kJournal, true).has_value());
    RAWFRAME_EXPECT(apply(scene, kJournal, false).has_value() && bytes(scene) == bytes(level()));
    const auto kWritten = writeJournal(kJournal);
    RAWFRAME_EXPECT(kWritten.has_value());
    const auto kRead = readJournal(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == kJournal);
    for (const Delta& delta : kAll) {
        const auto kOne = writeJournal({delta});
        RAWFRAME_EXPECT(kOne.has_value() && readJournal(*kOne) == Journal{delta});
    }
}

RAWFRAME_TEST(HostileJournalsAreReadOrRefusedWhole) {
    const std::string kSeed = *writeJournal(everyKind());
    RAWFRAME_EXPECT(readJournal(kSeed).has_value());
    std::size_t read = 0;
    const auto kTry = [&read](const std::string& text) {
        const auto kRead = readJournal(text);
        if (kRead.has_value()) {
            ++read;
            RAWFRAME_EXPECT(writeJournal(*kRead) == text);
        } else {
            RAWFRAME_EXPECT(refusedWith(kRead, AuthoringError::DeltaInvalid));
        }
    };
    for (std::size_t at = 0; at <= kSeed.size(); ++at) {
        kTry(kSeed.substr(0, at));
    }
    for (std::size_t at = 0; at < kSeed.size(); ++at) {
        std::string text = kSeed;
        for (const char kEach : std::string_view{"{}[]\":,0a-A\\n"}) {
            text[at] = kEach;
            kTry(text);
        }
    }
    RAWFRAME_EXPECT(read > 0);
}
