// Scenes in their one form: a written scene reads back to itself and writes
// to the same bytes, and every rule of the form is enforced on both sides.

#include "rawframe/scene/errors.h"
#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/test/test.h"

#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene;

namespace {

constexpr base::Bits128 kGround = schema::parseStableIdText("0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1").value;
constexpr base::Bits128 kCrate = schema::parseStableIdText("5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8").value;

FieldValue number(std::string text) {
    return FieldValue{.kind = FieldValue::Kind::Number, .number = std::move(text)};
}

Scene sample() {
    return Scene{
        .schema = {{.component = "game.body", .mark = 0x5f3a0c2d9e81b746ULL}, {.component = "game.link", .mark = 7}},
        .entities = {
            SceneEntity{.id = kGround,
                        .name = "ground",
                        .components = {{.name = "game.body",
                                        .fields = {{.name = "height", .value = number("0.5")},
                                                   {.name = "static", .value = {.kind = FieldValue::Kind::True}},
                                                   {.name = "width", .value = number("40")}}}}},
            SceneEntity{.id = kCrate,
                        .components = {{.name = "game.body", .fields = {{.name = "x", .value = number("-0")}}},
                                       {.name = "game.link",
                                        .fields = {{.name = "to",
                                                    .value = {.kind = FieldValue::Kind::Entity, .entity = kGround}}}}}},
        }};
}

bool refused(const auto& outcome) {
    return !outcome.has_value() && outcome.error().domain() == kSceneDomain &&
           outcome.error().code() == code(SceneError::SceneInvalid);
}

} // namespace

RAWFRAME_TEST(AScenesTextIsItsOneForm) {
    const auto kWritten = writeScene(sample());
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    // The form, as a person reads it.
    RAWFRAME_EXPECT(
        kWritten->starts_with("{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {\n"
                              "    \"game.body\": \"5f3a0c2d9e81b746\",\n"));
    RAWFRAME_EXPECT(kWritten->find("\"id\": \"0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1\"") != std::string::npos &&
                    kWritten->find("\"to\": \"0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1\"") != std::string::npos &&
                    kWritten->find("\"x\": -0") != std::string::npos);
    const auto kRead = readScene(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == sample());
    // Load and save, unedited: the same bytes.
    RAWFRAME_EXPECT(kRead.has_value() && writeScene(*kRead).has_value() && *writeScene(*kRead) == *kWritten);
}

RAWFRAME_TEST(EveryRuleOfTheFormIsKept) {
    const auto kBroken = [](auto change) {
        Scene scene = sample();
        change(scene);
        return refused(writeScene(scene));
    };
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.entities[1].id = kGround;
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.entities[0].id = {};
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        std::swap(scene.entities[1].components[0], scene.entities[1].components[1]);
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        std::swap(scene.entities[0].components[0].fields[0], scene.entities[0].components[0].fields[2]);
    }));
    // Defaults are never written; numbers only in their canonical text.
    for (const std::string_view kText : {"0", "1.0", "+1", "01", "0.50", "1e3", "x"}) {
        RAWFRAME_EXPECT(kBroken([kText](Scene& scene) {
            scene.entities[0].components[0].fields[0].value.number = std::string{kText};
        }));
    }
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.entities[1].components[1].fields[0].value.entity = base::Bits128{.high = 1, .low = 2};
    }));
    // The schema is exactly what the entities use.
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.schema.pop_back();
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.schema.push_back({.component = "game.unused", .mark = 1});
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        std::swap(scene.schema[0], scene.schema[1]);
    }));

    // Read: anything the writer would not have written.
    const std::string kGood = *writeScene(sample());
    const auto kEdited = [&kGood](std::string_view from, std::string_view to) {
        std::string text = kGood;
        const std::size_t kAt = text.find(from);
        RAWFRAME_EXPECT(kAt != std::string::npos);
        text.replace(kAt, from.size(), to);
        return refused(readScene(text));
    };
    RAWFRAME_EXPECT(kEdited("\"formatVersion\": 1", "\"formatVersion\": 2"));
    RAWFRAME_EXPECT(kEdited("\"kind\": \"rawframe.scene\"", "\"kind\": \"rawframe.prefab\""));
    RAWFRAME_EXPECT(kEdited("\"width\": 40", "\"width\": 40.0"));
    RAWFRAME_EXPECT(kEdited("\"width\": 40", "\"width\": 0"));
    RAWFRAME_EXPECT(kEdited("\"static\": true", "\"static\": false"));
    RAWFRAME_EXPECT(kEdited("\"id\": \"0d3f8a3e", "\"id\": \"0D3F8A3E"));
    RAWFRAME_EXPECT(kEdited("\"5f3a0c2d9e81b746\"", "\"5f3a0c2d9e81b74\""));
    RAWFRAME_EXPECT(kEdited("\"name\": \"ground\",", "\"name\": \"\","));
    RAWFRAME_EXPECT(kEdited("  \"formatVersion\": 1,", "  \"formatVersion\":  1,"));
    RAWFRAME_EXPECT(!readScene(kGood + " ").has_value());
    RAWFRAME_EXPECT(
        readScene(
            "{\n  \"kind\": \"rawframe.scene\",\n  \"formatVersion\": 1,\n  \"schema\": {},\n  \"entities\": []\n}\n")
            .has_value());
}
