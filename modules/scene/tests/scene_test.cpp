// Scenes in their one form: a written scene reads back to itself and writes
// to the same bytes, and every rule of the form is enforced on both sides.

#include "rawframe/scene/errors.h"
#include "rawframe/scene/scene.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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

namespace {

constexpr base::Bits128 kPlatform = schema::parseStableIdText("2b1f6a3c-0e4d-4c8b-9a7e-5d3c1b2a0f9e").value;
constexpr base::Bits128 kLeftPlatform = schema::parseStableIdText("7c2e4b1a-9f3d-4e6c-8b5a-1d0f2e3c4b5a").value;

/// Sample's two entities and an instance of a one-platform scene, moved,
/// given a link to the crate, and without its body.
Scene instancing() {
    Scene scene = sample();
    scene.schema = {{.component = "game.body", .mark = 0x5f3a0c2d9e81b746ULL},
                    {.component = "game.link", .mark = 7},
                    {.component = "game.pose", .mark = 9}};
    scene.instances = {SceneInstance{
        .scene = base::parseBits128Hex("52771075251e7361deaecf4939c72e56").value,
        .entities = {{.source = kPlatform, .instance = kLeftPlatform}},
        .overrides = {
            {.entity = kLeftPlatform, .component = "game.body", .kind = Override::Kind::Remove, .fields = {}},
            {.entity = kLeftPlatform,
             .component = "game.link",
             .kind = Override::Kind::Add,
             .fields = {{.name = "to", .value = {.kind = FieldValue::Kind::Entity, .entity = kCrate}}}},
            {.entity = kLeftPlatform,
             .component = "game.pose",
             .kind = Override::Kind::Set,
             .fields = {{.name = "turned", .value = {.kind = FieldValue::Kind::False}},
                        {.name = "x", .value = number("0")},
                        {.name = "y", .value = number("3")}}},
        }}};
    return scene;
}

} // namespace

RAWFRAME_TEST(AnInstanceIsItsSourceItsMappingAndItsPatch) {
    const auto kWritten = writeScene(instancing());
    RAWFRAME_EXPECT(kWritten.has_value());
    if (!kWritten.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kWritten->find("  \"instances\": [\n    {\n      \"scene\": \"52771075251e7361deaecf4939c72e56\",\n"
                                   "      \"entities\": {\n        \"2b1f6a3c-0e4d-4c8b-9a7e-5d3c1b2a0f9e\": "
                                   "\"7c2e4b1a-9f3d-4e6c-8b5a-1d0f2e3c4b5a\"\n      },\n") != std::string::npos);
    RAWFRAME_EXPECT(kWritten->find("\"remove\": true") != std::string::npos &&
                    kWritten->find("\"turned\": false") != std::string::npos &&
                    kWritten->find("\"x\": 0") != std::string::npos);
    const auto kRead = readScene(*kWritten);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == instancing());
    RAWFRAME_EXPECT(kRead.has_value() && *writeScene(*kRead) == *kWritten);
    // Without instances the member is left out, not written empty.
    RAWFRAME_EXPECT(writeScene(sample())->find("instances") == std::string::npos);
}

RAWFRAME_TEST(AnInstanceKeepsEveryRuleOfItsForm) {
    const auto kBroken = [](auto change) {
        Scene scene = instancing();
        change(scene);
        return refused(writeScene(scene));
    };
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].scene = {};
    }));
    // An instance id is the scene's own: not an entity's, not nought.
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].entities[0].instance = kGround;
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].entities[0].instance = {};
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].entities.push_back({.source = kGround, .instance = base::Bits128{.high = 9, .low = 9}});
    }));
    // Overrides: in order, of the instance's entities, in the form of their
    // kind.
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        std::swap(scene.instances[0].overrides[0], scene.instances[0].overrides[2]);
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].overrides[0].entity = kGround;
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].overrides[0].fields.push_back({.name = "x", .value = number("1")});
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].overrides[1].fields.push_back({.name = "zero", .value = number("0")});
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].overrides[2].fields.clear();
    }));
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.instances[0].overrides[1].fields[0].value.entity = base::Bits128{.high = 5, .low = 5};
    }));
    // The schema covers what the overrides name.
    RAWFRAME_EXPECT(kBroken([](Scene& scene) {
        scene.schema.pop_back();
    }));
    // An entity may name an instance's entity.
    Scene linked = instancing();
    linked.entities[1].components[1].fields[0].value.entity = kLeftPlatform;
    RAWFRAME_EXPECT(writeScene(linked).has_value());

    const std::string kGood = *writeScene(instancing());
    const auto kEdited = [&kGood](std::string_view from, std::string_view to) {
        std::string text = kGood;
        const std::size_t kAt = text.find(from);
        RAWFRAME_EXPECT(kAt != std::string::npos);
        text.replace(kAt, from.size(), to);
        return refused(readScene(text));
    };
    RAWFRAME_EXPECT(kEdited("\"remove\": true", "\"remove\": false"));
    RAWFRAME_EXPECT(kEdited("\"scene\": \"52771075", "\"scene\": \"5277107X"));
    RAWFRAME_EXPECT(kEdited("\"component\": \"game.pose\",\n          \"set\"",
                            "\"component\": \"game.pose\",\n          \"put\""));
    RAWFRAME_EXPECT(kEdited("\"2b1f6a3c-0e4d-4c8b-9a7e-5d3c1b2a0f9e\":", "\"2B1F6A3C-0e4d-4c8b-9a7e-5d3c1b2a0f9e\":"));
    std::string empty = *writeScene(sample());
    empty.insert(empty.size() - 2, ",\n  \"instances\": []");
    RAWFRAME_EXPECT(refused(readScene(empty)));
}

RAWFRAME_TEST(HostileScenesReadOnlyAsTheyWrite) {
    // A mod's scene is an untrusted author's text (D183): seeded mutations of
    // a real one, and whatever the reader accepts writes back to the very
    // same bytes, so there is no second reading of any scene.
    const auto kSeed = writeScene(sample());
    RAWFRAME_EXPECT(kSeed.has_value());
    if (!kSeed.has_value()) {
        return;
    }
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto kNext = [&state] {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    constexpr std::string_view kInserted = "{}[]\",: \n-.0123456789abcdefe";
    int accepted = 0;
    for (int round = 0; round < 20'000; ++round) {
        std::string text = *kSeed;
        const int kEdits = 1 + static_cast<int>(kNext() % 4);
        for (int edit = 0; edit < kEdits && !text.empty(); ++edit) {
            const std::size_t kAt = kNext() % text.size();
            switch (kNext() % 3) {
            case 0:
                text[kAt] = static_cast<char>(kNext() & 0xFFU);
                break;
            case 1:
                text.erase(kAt, 1 + (kNext() % 8));
                break;
            default:
                text.insert(kAt, 1, kInserted[kNext() % kInserted.size()]);
                break;
            }
        }
        const auto kRead = readScene(text);
        if (!kRead.has_value()) {
            continue;
        }
        ++accepted;
        const auto kWritten = writeScene(*kRead);
        RAWFRAME_EXPECT(kWritten.has_value() && *kWritten == text);
    }
    std::printf("  %d of 20000 read\n", accepted);
    RAWFRAME_EXPECT(accepted > 0);
}
