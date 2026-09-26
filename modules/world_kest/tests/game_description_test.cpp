// Game descriptions: what parses, what is refused and where, and as hostile
// input: every sample game's description, mutated a line or a byte at a
// time, is read or refused with the line that refused it, never half read.

#include "game_harness.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/spawn_scene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;
using world_kest::parseGame;
using world_kest::WorldKestError;

namespace {

bool refusedAt(std::string_view text, WorldKestError error, std::string_view line) {
    auto game = parseGame(text);
    if (game.has_value() || game.error().code() != world_kest::code(error)) {
        return false;
    }
    for (const auto& field : game.error().context()) {
        if (field.key == "line") {
            return field.value == line;
        }
    }
    return false;
}

} // namespace

RAWFRAME_TEST(AGameDescriptionParses) {
    auto game = parseGame("# movers\n"
                          "program movers.kest\n"
                          "\n"
                          "system a.move simulation integrate entities write a.position read a.velocity after a.input "
                          "random drift\n"
                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position  # later is fine\n"
                          "component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 a.velocity Velocity\r\n"
                          "spawn 2 a.position x=1 a.velocity\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->program == "movers.kest");
    // The two declared, then the engine's persistent identity, which any
    // entity may carry.
    RAWFRAME_EXPECT(game->components.size() == 3 && game->components[1].kestType == "Velocity" &&
                    game->components[2].name == "rawframe.world.persistent");
    RAWFRAME_EXPECT(game->systems.size() == 1 && game->systems[0].columns.size() == 3);
    RAWFRAME_EXPECT(game->systems[0].columns[0].entities);
    RAWFRAME_EXPECT(game->systems[0].columns[1].access == world::Access::Write);
    RAWFRAME_EXPECT((game->systems[0].randomStreams == std::vector<std::string>{"drift"}));
    RAWFRAME_EXPECT((game->systems[0].after == std::vector<std::string>{"a.input"}));
    RAWFRAME_EXPECT(game->spawns.size() == 1 && game->spawns[0].count == 2);
    RAWFRAME_EXPECT(game->spawns[0].components.size() == 2 && game->spawns[0].components[0].fields.size() == 1);
}

RAWFRAME_TEST(PredictionIsDeclaredByLine) {
    auto game = parseGame("program p.kest\n"
                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position\n"
                          "system a.move simulation move write a.position predicted\n"
                          "system a.other simulation other predicted write a.position\n"
                          "system a.server simulation serve read a.position\n"
                          "predict a.position\n"
                          "interpolate a.position\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->systems[0].predicted && game->systems[1].predicted && !game->systems[2].predicted);
    RAWFRAME_EXPECT(game->systems[0].columns.size() == 1 && game->systems[1].columns.size() == 1);
    RAWFRAME_EXPECT((game->predicted == std::vector<std::string>{"a.position"}));
    RAWFRAME_EXPECT((game->interpolated == std::vector<std::string>{"a.position"}));
    RAWFRAME_EXPECT(refusedAt("program p.kest\npredict\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\ninterpolate\n", WorldKestError::BadGameLine, "2"));
}

RAWFRAME_TEST(ControlsAreDeclaredByLine) {
    constexpr std::string_view kHead = "program p.kest\n"
                                       "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.stick Stick\n";
    auto game = parseGame(std::string{kHead} + "input a.stick\nactions a.actions\nsample sample.kest sample\n");
    RAWFRAME_EXPECT(game.has_value() && game->controls.has_value() && game->controls->actions == "a.actions" &&
                    game->controls->program == "sample.kest" && game->controls->entry == "sample");
    RAWFRAME_EXPECT(parseGame(kHead).has_value() && !parseGame(kHead)->controls.has_value());
    // Together, once each, and with an input line.
    RAWFRAME_EXPECT(
        refusedAt(std::string{kHead} + "input a.stick\nactions a.actions\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kHead} + "actions a.actions\nsample s.kest f\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kHead} + "input a.stick\nactions a b\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kHead} + "input a.stick\nsample s.kest\n", WorldKestError::BadGameLine, "4"));
}

RAWFRAME_TEST(AudioIsDeclaredByLine) {
    auto game = parseGame("program p.kest\nmixer game.mixer\nsound 00000000000000a1 steps.sound\n"
                          "sound 00000000000000a2 shot.sound\n");
    RAWFRAME_EXPECT(game.has_value() && game->audio.has_value() && game->audio->mixer == "game.mixer" &&
                    game->audio->sounds.size() == 2 && game->audio->sounds[1].id == 0xa2 &&
                    game->audio->sounds[1].path == "shot.sound");
    RAWFRAME_EXPECT(!parseGame("program p.kest\n")->audio.has_value());
    RAWFRAME_EXPECT(refusedAt("program p.kest\nsound 00000000000000a1 a.sound\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nsound a1 a.sound\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nsound 00000000000000a1 a.sound\nsound 00000000000000a1 "
                              "b.sound\n",
                              WorldKestError::BadGameLine,
                              "4"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nmixer m\nmixer n\n", WorldKestError::BadGameLine, "3"));
}

RAWFRAME_TEST(InterestIsDeclaredByLine) {
    constexpr std::string_view kProgram = "program p.kest\n"
                                          "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position\n";
    auto game = parseGame(std::string{kProgram} + "interest a.position x y within 40.5\n");
    RAWFRAME_EXPECT(game.has_value() && game->interest.has_value());
    if (!game.has_value() || !game->interest.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->interest->component == "a.position" && game->interest->radius == 40.5);
    RAWFRAME_EXPECT((game->interest->axes == std::vector<std::string>{"x", "y"}));
    // No field, four fields, no radius, a radius that is not positive, a
    // second line, and an undeclared component.
    for (const std::string_view kLine : {"interest a.position within 4\n",
                                         "interest a.position x y z w within 4\n",
                                         "interest a.position x y\n",
                                         "interest a.position x within 0\n",
                                         "interest a.position x within nan\n",
                                         "interest a.position x within 4\n\ninterest a.position y within 4\n"}) {
        RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + std::string{kLine}, WorldKestError::BadGameLine, "3") ||
                        refusedAt(std::string{kProgram} + std::string{kLine}, WorldKestError::BadGameLine, "5"));
    }
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "interest a.velocity x within 4\n", WorldKestError::UnknownName, "3"));
}

RAWFRAME_TEST(PhysicsIsDeclaredByLine) {
    auto game = parseGame("program p.kest\nphysics2d gravity 0.5 -9.8 substeps 8\n"
                          "spawn 1 rawframe.physics2d.body width=1 rawframe.physics2d.pose y=2\n");
    RAWFRAME_EXPECT(game.has_value() && game->physics.has_value());
    if (!game.has_value() || !game->physics.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->physics->dimensions == 2 && game->physics->gravityX == 0.5F &&
                    game->physics->gravityY == -9.8F && game->physics->substeps == 8);
    // The engine's nine components, under their engine names, and its
    // persistent identity.
    RAWFRAME_EXPECT(game->components.size() == 10 && game->components[0].name == "rawframe.physics2d.body" &&
                    game->components[1].kestType == "Pose2D");
    const auto kDefaults = parseGame("program p.kest\nphysics2d\n");
    RAWFRAME_EXPECT(kDefaults.has_value() && kDefaults->physics->gravityY == -10.0F &&
                    kDefaults->physics->substeps == 4);
    // In three dimensions: three numbers of gravity, and the ten 3D
    // components.
    const auto kThree = parseGame("program p.kest\nphysics3d gravity 0 -9.8 1.5\n");
    RAWFRAME_EXPECT(kThree.has_value() && kThree->physics->dimensions == 3 && kThree->physics->gravityZ == 1.5F &&
                    kThree->components.size() == 11 && kThree->components[1].name == "rawframe.physics3d.pose");
    for (const std::string_view kLine : {"physics2d gravity 1\n",
                                         "physics2d spin 3\n",
                                         "physics2d substeps four\n",
                                         "physics2d\nphysics2d\n",
                                         "physics3d gravity 0 -10\n",
                                         "physics2d\nphysics3d\n"}) {
        const std::string kText = "program p.kest\n" + std::string{kLine};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "2") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "3"));
    }
}

RAWFRAME_TEST(MeshesAreDeclaredByLineAndNamedByFile) {
    const std::string kHead = "program p.kest\nphysics3d\n";
    const auto kGame = parseGame(kHead + "mesh 00000000000000a1 hill.gltf\nmesh 00000000000000a2 cave.glb\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->meshes.size() == 2 && kGame->meshes[1].id == 0xA2 &&
                    kGame->meshes[1].path == "cave.glb");
    if (kGame.has_value()) {
        // A spawn names a mesh by its file, and a class by its name.
        RAWFRAME_EXPECT(world_kest::spawnValue(*kGame, "rawframe.physics3d.mesh", {"mesh", "cave.glb"}) == "162");
        RAWFRAME_EXPECT(world_kest::spawnValue(*kGame, "rawframe.physics3d.mesh", {"mesh", "7"}) == "7");
        RAWFRAME_EXPECT(world_kest::spawnValue(*kGame, "rawframe.physics3d.body", {"mesh", "cave.glb"}) == "cave.glb");
    }
    for (const std::string_view kLines : {"mesh 00000000000000a1\n",
                                          "mesh 0000000000000000 hill.gltf\n",
                                          "mesh a1 hill.gltf\n",
                                          "mesh 00000000000000a1 hill.gltf\nmesh 00000000000000a1 cave.glb\n",
                                          "mesh 00000000000000a1 hill.gltf\nmesh 00000000000000a2 hill.gltf\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(AGameReadsItsMeshesCooked) {
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-meshes-" + std::to_string(::getpid()));
    std::filesystem::create_directories(kDirectory);
    const auto kWrite = [&kDirectory](std::string_view name, std::string_view text) {
        std::FILE* file = std::fopen((kDirectory / name).c_str(), "wb");
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    };
    kWrite("g.game", "program p.kest\nmesh 00000000000000a1 hill.gltf\n");
    kWrite("hill.gltf", "{}");
    // Without a sidecar naming the mesh importer, the mesh has no resource.
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromDirectory(kDirectory / "g.game").has_value());
    kWrite("hill.gltf.rfmeta",
           "{\n  \"schema\": 1,\n  \"resourceId\": \"46837f0a03812629d3a7e3df63fa0905\",\n  \"importer\": "
           "\"rawframe.mesh\"\n}\n");
    // With one, it is read cooked: without content, not at all, since the
    // runtime decodes no glTF.
    const auto kUncooked = world_kest::GameFiles::fromDirectory(kDirectory / "g.game");
    RAWFRAME_EXPECT(!kUncooked.has_value() && kUncooked.error().code() == code(WorldKestError::UnreadableFile));
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(CollisionIsDeclaredByLine) {
    const std::string kPhysics = "program p.kest\nphysics2d\n";
    auto game = parseGame(kPhysics + "collision class ball 3f1c9a7e52d04b18\ncollision class wall 00000000000000a1\n"
                                     "collision rule ball wall trigger\ncollision default ignore\n");
    RAWFRAME_EXPECT(game.has_value());
    if (!game.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(game->collision.classes.size() == 2 && game->collision.classes[0].id == 0x3f1c9a7e52d04b18 &&
                    game->collision.classes[1].name == "wall" && game->collision.classes[1].id == 0xa1);
    RAWFRAME_EXPECT(game->collision.rules.size() == 1 &&
                    game->collision.rules[0].rule == physics::CollisionRule::Trigger &&
                    game->collision.fallback == physics::CollisionRule::Ignore);
    // A short identity, nought, no hex, an unknown rule, a second default,
    // a class not declared, and collision without physics.
    for (const std::string_view kLine : {"collision class ball 3f1c9a7e52d04b1\n",
                                         "collision class ball 0000000000000000\n",
                                         "collision class ball 3f1c9a7e52d04b1z\n",
                                         "collision class ball 3f1c9a7e52d04b18\ncollision rule ball ball bounce\n",
                                         "collision default ignore\ncollision default collide\n"}) {
        const std::string kText = kPhysics + std::string{kLine};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
    RAWFRAME_EXPECT(refusedAt(kPhysics + "collision rule ball wall ignore\n", WorldKestError::UnknownName, "3"));
    RAWFRAME_EXPECT(
        refusedAt("program p.kest\ncollision class ball 3f1c9a7e52d04b18\n", WorldKestError::BadGameLine, "2"));
}

RAWFRAME_TEST(BadLinesAreRefusedWhereTheyAre) {
    constexpr std::string_view kProgram = "program p.kest\n";
    RAWFRAME_EXPECT(refusedAt("program p.kest\nbuild x\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("component 1234 a.b T\n", WorldKestError::BadGameLine, "1"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nprogram q.kest\n", WorldKestError::BadGameLine, "2"));
    // No program at all is said at the last line read.
    RAWFRAME_EXPECT(refusedAt("spawn 1 a.b\n", WorldKestError::BadGameLine, "1"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x later f read a.b\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x simulation f read\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "system s.x simulation f read a.b\n", WorldKestError::UnknownName, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "spawn 0 a.b\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "spawn 1 x=1\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt(std::string{kProgram} + "prefab 0 a.scene\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "prefab 00000000000000a1 a.scene\nprefab 00000000000000a1 b.scene\n",
                  WorldKestError::BadGameLine,
                  "3"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "prefab 00000000000000a1 a.scene\nprefab 00000000000000a2 a.scene\n",
                  WorldKestError::BadGameLine,
                  "3"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kProgram} + "scene a.scene\nscene a.scene\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("# nothing\n", WorldKestError::BadGameLine, "1"));
}

RAWFRAME_TEST(TextLinesNameDocumentsByTheirSidecars) {
    auto game = parseGame("program p.kest\ntext hud.strings\ntext hud.tr.translations\nlocale en-GB\n");
    RAWFRAME_EXPECT(game.has_value() && game->texts.size() == 2 && game->texts[1] == "hud.tr.translations" &&
                    game->locale == "en-GB");
    RAWFRAME_EXPECT(refusedAt("program p.kest\ntext\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\ntext a b\n", WorldKestError::BadGameLine, "2"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\ntext a\ntext a\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nlocale en\nlocale tr\n", WorldKestError::BadGameLine, "3"));

    // In development, each by the identity its sidecar gives, which must
    // name rawframe.text; its bytes are not read here.
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-texts-" + std::to_string(::getpid()));
    std::filesystem::remove_all(kDirectory);
    std::filesystem::create_directories(kDirectory);
    const auto kWrite = [&kDirectory](std::string_view name, std::string_view text) {
        std::FILE* file = std::fopen((kDirectory / name).c_str(), "wb");
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    };
    const auto kSidecar = [](std::string_view id, std::string_view importer) {
        return "{\n  \"schema\": 1,\n  \"resourceId\": \"" + std::string{id} + "\",\n  \"importer\": \"" +
               std::string{importer} + "\"\n}\n";
    };
    kWrite("hud.game", "program p.kest\ntext hud.strings\n");
    kWrite("hud.strings.rfmeta", kSidecar("749e2ba7d0067a4db2348b183fc4d55f", "rawframe.text"));
    const auto kFiles = world_kest::GameFiles::fromDirectory(kDirectory / "hud.game");
    RAWFRAME_EXPECT(kFiles.has_value() && kFiles->texts().size() == 1 && kFiles->texts()[0].path == "hud.strings" &&
                    kFiles->texts()[0].document == base::parseBits128Hex("749e2ba7d0067a4db2348b183fc4d55f").value);
    kWrite("hud.strings.rfmeta", kSidecar("749e2ba7d0067a4db2348b183fc4d55f", "rawframe.scene"));
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromDirectory(kDirectory / "hud.game").has_value());
    std::filesystem::remove(kDirectory / "hud.strings.rfmeta");
    RAWFRAME_EXPECT(!world_kest::GameFiles::fromDirectory(kDirectory / "hud.game").has_value());
    std::filesystem::remove_all(kDirectory);
}

RAWFRAME_TEST(HostileGameDescriptionsAreReadOrRefusedAtALine) {
    // A game's description comes with content a client fetched, from a
    // publisher the player may never have met.
    constexpr std::array<std::string_view, 5> kGames = {
        "arena/arena.game", "crates/crates.game", "plaza/plaza.game", "runners/duel.game", "runners/runners.game"};
    constexpr std::array<std::string_view, 10> kLines = {"program other.kest\n",
                                                         "scene level.scene\n",
                                                         "mods closed\n",
                                                         "modapi runners 2\n",
                                                         "extension more data runners.age exclusive\n",
                                                         "interest rawframe.physics2d.pose x y within 1e40\n",
                                                         "predict runners.score\n",
                                                         "input runners.score\n",
                                                         "physics3d gravity 0 -10 0\n",
                                                         "save hall runners.stick\n"};
    std::size_t read = 0;
    std::size_t refused = 0;
    test::Mutations mutations;
    for (const std::string_view kGame : kGames) {
        const std::string kSeed = game_test::readText(std::string{RAWFRAME_SAMPLE_GAMES} + std::string{kGame});
        RAWFRAME_EXPECT(world_kest::parseGame(kSeed).has_value());
        for (int round = 0; round < 1'000; ++round) {
            const std::string kText = mutations.mutate(kSeed, " \t\r\n#.-_09aZ\xC3", kLines);
            const auto kParsed = world_kest::parseGame(kText);
            if (kParsed.has_value()) {
                ++read;
                RAWFRAME_EXPECT(!kParsed->program.empty());
                continue;
            }
            ++refused;
            const auto kContext = kParsed.error().context();
            RAWFRAME_EXPECT(kParsed.error().domain() == world_kest::kWorldKestDomain &&
                            std::ranges::any_of(kContext, [](const result::ContextField& field) {
                                return field.key == "line";
                            }));
        }
    }
    std::printf("  %zu of %zu read\n", read, read + refused);
    RAWFRAME_EXPECT(read > 0 && refused > 0);
}
