// Game descriptions: what parses, what is refused and where, and as hostile
// input: every sample game's description, mutated a line or a byte at a
// time, is read or refused with the line that refused it, never half read.

#include "game_harness.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/scratch.h"
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

RAWFRAME_TEST(TexturesAreDeclaredByLine) {
    const std::string kHead = "program p.kest\ncomponent 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 g.look Sprite\n";
    const auto kGame = parseGame(kHead + "texture 00000000000000b1 runner.png\ntexture 00000000000000b2 tiles.png\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->textures.size() == 2 && kGame->textures[1].id == 0xB2 &&
                    kGame->textures[1].path == "tiles.png");
    if (kGame.has_value()) {
        // A sprite names its texture by file; another component keeps it.
        RAWFRAME_EXPECT(world_kest::spawnValue(*kGame, "g.look", {"texture", "tiles.png"}) == "178");
        RAWFRAME_EXPECT(world_kest::spawnValue(*kGame, "g.other", {"texture", "tiles.png"}) == "tiles.png");
    }
    for (const std::string_view kLines : {"texture 00000000000000b1\n",
                                          "texture 0000000000000000 runner.png\n",
                                          "texture 00000000000000b1 a.png\ntexture 00000000000000b1 b.png\n",
                                          "texture 00000000000000b1 a.png\ntexture 00000000000000b2 a.png\n",
                                          // A client's view is a presentation component (D261).
                                          "camera 12\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(FontsAndLabelsAreDeclaredByLine) {
    const std::string kHead = "program p.kest\ntext hud.strings\n";
    const auto kGame = parseGame(kHead + "font 00000000000000f1 sans.ttf\nlabel 00000000000000a1 hud.strings hud.hit\n"
                                         "label 00000000000000a2 hud.strings hud.score points\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->fonts.size() == 1 && kGame->fonts[0].id == 0xF1 &&
                    kGame->fonts[0].path == "sans.ttf" && kGame->labels.size() == 2 &&
                    kGame->labels[0].argument.empty() && kGame->labels[1].id == 0xA2 &&
                    kGame->labels[1].table == "hud.strings" && kGame->labels[1].key == "hud.score" &&
                    kGame->labels[1].argument == "points");
    for (const std::string_view kLines :
         {"font 00000000000000f1\n",
          "font 0000000000000000 sans.ttf\n",
          "font 00000000000000f1 a.ttf\nfont 00000000000000f1 b.ttf\n",
          "font 00000000000000f1 a.ttf\nfont 00000000000000f2 a.ttf\n",
          "label 00000000000000a1 hud.strings\n",
          "label 0000000000000000 hud.strings hud.hit\n",
          "label 00000000000000a1 hud.strings hud.hit points more\n",
          "label 00000000000000a1 hud.strings a\nlabel 00000000000000a1 hud.strings b\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
    // A label's table is one a text line names, wherever that line is.
    RAWFRAME_EXPECT(refusedAt(kHead + "label 00000000000000a1 menu.strings play\n", WorldKestError::UnknownName, "3"));
    RAWFRAME_EXPECT(parseGame("label 00000000000000a1 hud.strings hud.hit\n" + kHead).has_value());
}

RAWFRAME_TEST(RenderTexturesAreDeclaredByLine) {
    const std::string kHead = "program p.kest\ncomponent 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f91 g.look Model\n";
    const auto kGame = parseGame(kHead + "rendertexture 00000000000000d1 256 128\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->renderTextures.size() == 1 && kGame->renderTextures[0].id == 0xD1 &&
                    kGame->renderTextures[0].width == 256 && kGame->renderTextures[0].height == 128 &&
                    kGame->renderTextures[0].update == world_kest::RenderTextureUpdate::EveryFrame);
    // Drawn every frame unless declared drawn on demand.
    const auto kAsked = parseGame(kHead + "rendertexture 00000000000000d1 256 128 on_demand\n"
                                          "rendertexture 00000000000000d2 64 64 every_frame\n");
    RAWFRAME_EXPECT(kAsked.has_value() && kAsked->renderTextures.size() == 2 &&
                    kAsked->renderTextures[0].update == world_kest::RenderTextureUpdate::OnDemand &&
                    kAsked->renderTextures[1].update == world_kest::RenderTextureUpdate::EveryFrame);
    // Its identity is no other texture's; each side 1 to 4096 pixels; its
    // update one of the two (D361).
    for (const std::string_view kLines : {"rendertexture 00000000000000d1 256\n",
                                          "rendertexture 0000000000000000 256 128\n",
                                          "rendertexture 00000000000000d1 0 128\n",
                                          "rendertexture 00000000000000d1 256 4097\n",
                                          "rendertexture 00000000000000d1 25.5 128\n",
                                          "rendertexture 00000000000000d1 256 128 sometimes\n",
                                          "rendertexture 00000000000000d1 256 128 on_demand more\n",
                                          "texture 00000000000000d1 a.png\nrendertexture 00000000000000d1 256 128\n",
                                          "rendertexture 00000000000000d1 256 128\ntexture 00000000000000d1 a.png\n",
                                          "rendertexture 00000000000000d1 2 2\nrendertexture 00000000000000d1 4 4\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(SplitScreenLayoutsAreDeclaredByLine) {
    const std::string kHead = "program p.kest\ncomponent 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f91 g.look Model\n";
    const auto kGame =
        parseGame(kHead + "layout 2 0 0 0.5 1 0.5 0 0.5 1\nlayout 3 0 0 1 0.5 0 0.5 0.5 0.5 0.5 0.5 0.5 0.5\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->layouts.size() == 2 && kGame->layouts[0].players == 2 &&
                    kGame->layouts[0].regions.size() == 2 && kGame->layouts[0].regions[1].x == 0.5F &&
                    kGame->layouts[0].regions[1].width == 0.5F && kGame->layouts[1].players == 3 &&
                    kGame->layouts[1].regions[2].y == 0.5F);
    // Two to four players (D362), a region each, inside the window, a
    // count laid out once.
    for (const std::string_view kLines : {"layout 1 0 0 1 1\n",
                                          "layout 5 0 0 1 1 0 0 1 1 0 0 1 1 0 0 1 1 0 0 1 1\n",
                                          "layout 2 0 0 0.5 1\n",
                                          "layout 2 0 0 0.5 1 0.5 0 0.5 1 0\n",
                                          "layout 2 0 0 0.5 1 0.6 0 0.5 1\n",
                                          "layout 2 0 0 0 1 0.5 0 0.5 1\n",
                                          "layout 2.5 0 0 0.5 1 0.5 0 0.5 1\n",
                                          "layout 2 0 0 0.5 1 0.5 0 0.5 1\nlayout 2 0 0 1 0.5 0 0.5 1 0.5\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(AConstrainedAspectIsDeclaredByLine) {
    const std::string kHead = "program p.kest\ncomponent 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f91 g.look Model\n";
    const auto kGame = parseGame(kHead + "aspect 4 3\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->aspect.has_value() && kGame->aspect->width == 4 &&
                    kGame->aspect->height == 3 && kGame->aspect->bars == (std::array<std::uint8_t, 3>{}));
    const auto kColored = parseGame(kHead + "aspect 16 9 32 0 255\n");
    RAWFRAME_EXPECT(kColored.has_value() && kColored->aspect.has_value() &&
                    kColored->aspect->bars == (std::array<std::uint8_t, 3>{32, 0, 255}));
    const auto kFilled = parseGame(kHead);
    RAWFRAME_EXPECT(kFilled.has_value() && !kFilled->aspect.has_value());
    // Two whole numbers from one, once (D369).
    for (const std::string_view kLines : {"aspect 16\n",
                                          "aspect 16 0\n",
                                          "aspect 16.5 9\n",
                                          "aspect 16 9 1\n",
                                          "aspect 16 9 0 0 256\n",
                                          "aspect 16 9 0 0.5 0\n",
                                          "aspect 16 9\naspect 4 3\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(UiNodesAreNestedByLine) {
    const std::string kHead = "program p.kest\n"
                              "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f91 g.hud Node\n"
                              "component 6c2e9a1f-3b55-4d20-8e1f-8b7d4c3f2a02 g.bar Node\n"
                              "component 7d3fab20-4c66-4e31-9f20-9c8e5d4a3b13 g.fill Node\n";
    const auto kGame = parseGame(kHead + "ui g.bar in g.hud\nui g.fill in g.bar\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->uiParents.size() == 2 && kGame->uiParents[0].node == "g.bar" &&
                    kGame->uiParents[0].parent == "g.hud" && kGame->uiParents[1].node == "g.fill");
    // One parent each, never itself however far up (D376).
    for (const std::string_view kLines : {"ui g.bar\n",
                                          "ui g.bar on g.hud\n",
                                          "ui g.bar in g.bar\n",
                                          "ui g.bar in g.hud\nui g.bar in g.fill\n",
                                          "ui g.bar in g.hud\nui g.hud in g.bar\n",
                                          "ui g.bar in g.hud\nui g.fill in g.bar\nui g.hud in g.fill\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "5") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "6") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "7"));
    }
    RAWFRAME_EXPECT(!parseGame(kHead + "ui g.bar in g.panel\n").has_value());
}

RAWFRAME_TEST(MaterialsAreDeclaredByLine) {
    const std::string kHead = "program p.kest\ncomponent 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f91 g.look Model\n";
    const auto kGame =
        parseGame(kHead + "material 00000000000000c1 brass.rfmaterial\nmaterial 00000000000000c2 stone.rfmaterial\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->materials.size() == 2 && kGame->materials[1].id == 0xC2 &&
                    kGame->materials[1].path == "stone.rfmaterial");
    for (const std::string_view kLines :
         {"material 00000000000000c1\n",
          "material 0000000000000000 brass.rfmaterial\n",
          "material 00000000000000c1 a.rfmaterial\nmaterial 00000000000000c1 b.rfmaterial\n",
          "material 00000000000000c1 a.rfmaterial\nmaterial 00000000000000c2 a.rfmaterial\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(refusedAt(kText, WorldKestError::BadGameLine, "3") ||
                        refusedAt(kText, WorldKestError::BadGameLine, "4"));
    }
}

RAWFRAME_TEST(AGameReadsItsMeshesCooked) {
    const std::filesystem::path kDirectory = test::scratchDirectory("meshes");
    std::filesystem::create_directories(kDirectory);
    const auto kWrite = [&kDirectory](std::string_view name, std::string_view text) {
        std::FILE* file = std::fopen((kDirectory / name).string().c_str(), "wb");
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

RAWFRAME_TEST(LookupsAreDeclaredByLine) {
    constexpr std::string_view kComponents = "program p.kest\n"
                                             "component 0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1 a.position Position\n"
                                             "component 5b1c9e22-4f07-4d3a-8c6b-91e7d2a0f4c8 a.target Target\n";
    auto game = parseGame(std::string{kComponents} +
                          "system a.follow simulation follow read a.target lookup a.position after a.move\n"
                          "present a.show show read a.target lookup a.position\n");
    RAWFRAME_EXPECT(game.has_value());
    if (game.has_value()) {
        RAWFRAME_EXPECT((game->systems[0].lookups == std::vector<std::string>{"a.position"}) &&
                        game->systems[0].columns.size() == 1 &&
                        (game->systems[0].after == std::vector<std::string>{"a.move"}));
        RAWFRAME_EXPECT((game->presented[0].lookups == std::vector<std::string>{"a.position"}) &&
                        game->presented[0].columns.size() == 1);
    }
    // Never of what the system writes, nor of what the game does not
    // declare.
    RAWFRAME_EXPECT(refusedAt(std::string{kComponents} + "system a.x simulation f write a.position lookup a.position\n",
                              WorldKestError::BadGameLine,
                              "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kComponents} + "present a.x f write a.position lookup a.position\n",
                              WorldKestError::BadGameLine,
                              "4"));
    RAWFRAME_EXPECT(refusedAt(std::string{kComponents} + "system a.x simulation f read a.target lookup a.nothing\n",
                              WorldKestError::UnknownName,
                              "4"));
    RAWFRAME_EXPECT(
        refusedAt(std::string{kComponents} + "system a.x simulation f lookup\n", WorldKestError::BadGameLine, "4"));
}

RAWFRAME_TEST(ANavigationLineNamesItsAgentAndNeedsThreeDimensions) {
    auto game = parseGame("program p.kest\nphysics3d\nnavigation radius 0.35 slope 30 cell 0.25\n");
    RAWFRAME_EXPECT(game.has_value() && game->navigation.has_value());
    if (game.has_value() && game->navigation.has_value()) {
        RAWFRAME_EXPECT(game->navigation->radius == 0.35 && game->navigation->slope == 30 &&
                        game->navigation->cell == 0.25 && game->navigation->height == 1.8 &&
                        game->navigation->step == 0.4);
    }
    // Before its physics line too; never twice, without a value, with an
    // unknown word or a value not above nought, nor in 2D or without physics.
    RAWFRAME_EXPECT(parseGame("program p.kest\nnavigation\nphysics3d\n").has_value());
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics3d\nnavigation\nnavigation\n", WorldKestError::BadGameLine, "4"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics3d\nnavigation radius\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics3d\nnavigation width 1\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics3d\nnavigation cell 0\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics3d\nnavigation step nan\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nphysics2d\nnavigation\n", WorldKestError::BadGameLine, "3"));
    RAWFRAME_EXPECT(refusedAt("program p.kest\nnavigation\n", WorldKestError::BadGameLine, "2"));
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
    const std::filesystem::path kDirectory = test::scratchDirectory("texts");
    std::filesystem::remove_all(kDirectory);
    std::filesystem::create_directories(kDirectory);
    const auto kWrite = [&kDirectory](std::string_view name, std::string_view text) {
        std::FILE* file = std::fopen((kDirectory / name).string().c_str(), "wb");
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

RAWFRAME_TEST(PresentationStateIsAClientsAlone) {
    const std::string kHead = "program p.kest\n"
                              "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 g.look Sprite\n"
                              "component 6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 g.stick Stick\n"
                              "component 7b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 g.score Score\n"
                              "component 8b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 g.view Camera\n";
    const auto kGame = parseGame(kHead + "presentation g.look on g.stick\n"
                                         "presentation g.view on player\n"
                                         "present g.dress dress read g.stick write g.look entities\n"
                                         "replicate g.stick g.score\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->presentation.size() == 2 &&
                    kGame->presentation[1].components == std::vector<std::string>{"g.view"} &&
                    !kGame->presentation[1].on.has_value() &&
                    kGame->presentation[0].components == std::vector<std::string>{"g.look"} &&
                    kGame->presentation[0].on == "g.stick" && kGame->presented.size() == 1 &&
                    kGame->presented[0].entry == "dress" && kGame->presented[0].columns.size() == 3 &&
                    kGame->presented[0].columns[2].entities);
    for (const std::string_view kLines : {// The shape of each line.
                                          "presentation g.look\n",
                                          "presentation on g.stick\n",
                                          "presentation g.look on\n",
                                          "presentation g.look on g.stick g.score\n",
                                          "present g.dress\n",
                                          "present g.dress dress random r\n",
                                          "present g.dress dress read\n",
                                          // Presentation state is a client's alone.
                                          "presentation g.look on g.stick\npresentation g.look on g.score\n",
                                          "presentation g.look on g.look\n",
                                          "presentation g.look on g.stick\npresentation g.look on player\n",
                                          "presentation g.look on g.stick\nreplicate g.look\n",
                                          "presentation g.look on g.stick\nplayer g.look\n",
                                          "presentation g.look on g.stick\nsystem g.s simulation s read g.look\n",
                                          // And only present systems write it, and only it.
                                          "presentation g.look on g.stick\npresent g.d d write g.score\n",
                                          "present g.d d read g.stick\npresent g.d e read g.stick\n"}) {
        const std::string kText = kHead + std::string{kLines};
        RAWFRAME_EXPECT(!parseGame(kText).has_value());
    }
    RAWFRAME_EXPECT(refusedAt(kHead + "presentation g.look on g.unknown\n", WorldKestError::UnknownName, "6"));
}

RAWFRAME_TEST(TexturesAreNamedByTheirSidecars) {
    // In development, each by the identity its sidecar gives, which must
    // name rawframe.texture; its bytes are not read here.
    const auto kHeld = [](std::string_view importer) {
        std::vector<std::pair<std::string, std::string>> held = {
            {"look.game", "program p.kest\ntexture 00000000000000b2 tiles.png\n"}, {"p.kest", "module p\n"}};
        if (!importer.empty()) {
            held.emplace_back("tiles.png.rfmeta",
                              "{\n  \"schema\": 1,\n  \"resourceId\": \"a6478ea1aa844c683e293a9754feeb95\",\n  "
                              "\"importer\": \"" +
                                  std::string{importer} + "\"\n}\n");
        }
        return world_kest::GameFiles::fromHeld("look.game", std::move(held));
    };
    const auto kFiles = kHeld("rawframe.texture");
    RAWFRAME_EXPECT(kFiles.has_value() && kFiles->textures().size() == 1 && kFiles->textures()[0].id == 0xB2 &&
                    kFiles->textures()[0].path == "tiles.png" &&
                    kFiles->textures()[0].texture == base::parseBits128Hex("a6478ea1aa844c683e293a9754feeb95").value);
    RAWFRAME_EXPECT(!kHeld("rawframe.mesh").has_value());
    RAWFRAME_EXPECT(!kHeld("").has_value());
}

RAWFRAME_TEST(FontsAreNamedByTheirSidecars) {
    const auto kHeld = [](std::string_view importer) {
        std::vector<std::pair<std::string, std::string>> held = {
            {"look.game", "program p.kest\nfont 00000000000000f1 sans.ttf\n"}, {"p.kest", "module p\n"}};
        if (!importer.empty()) {
            held.emplace_back("sans.ttf.rfmeta",
                              "{\n  \"schema\": 1,\n  \"resourceId\": \"51efbae405c153ba13e8af579ad96b5b\",\n  "
                              "\"importer\": \"" +
                                  std::string{importer} + "\"\n}\n");
        }
        return world_kest::GameFiles::fromHeld("look.game", std::move(held));
    };
    const auto kFiles = kHeld("rawframe.font");
    RAWFRAME_EXPECT(kFiles.has_value() && kFiles->fonts().size() == 1 && kFiles->fonts()[0].id == 0xF1 &&
                    kFiles->fonts()[0].font == base::parseBits128Hex("51efbae405c153ba13e8af579ad96b5b").value);
    RAWFRAME_EXPECT(!kHeld("rawframe.texture").has_value());
    RAWFRAME_EXPECT(!kHeld("").has_value());
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

RAWFRAME_TEST(MessagesAreDeclaredByLine) {
    const std::string kHead = "program p.kest\n";
    const auto kGame = parseGame(kHead + "message hurt Hurt\nmessage round_over rules.Round\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->messages.size() == 2 && kGame->messages[1].name == "round_over" &&
                    kGame->messages[1].kestType == "rules.Round");
    for (const std::string_view kLines : {"message hurt\n",
                                          "message Hurt Hurt\n",
                                          "message hurt Hurt extra\n",
                                          "message hurt Hurt\nmessage hurt Other\n"}) {
        RAWFRAME_EXPECT(!parseGame(kHead + std::string{kLines}).has_value());
    }
}

RAWFRAME_TEST(CommandsAreDeclaredByLine) {
    const std::string kHead = "program p.kest\n";
    const auto kGame = parseGame(kHead + "command collect controls.Collect\ncommand push Push\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->commands.size() == 2 && kGame->commands[0].name == "collect" &&
                    kGame->commands[0].kestType == "controls.Collect");
    for (const std::string_view kLines : {"command push\n",
                                          "command Push Push\n",
                                          "command push Push extra\n",
                                          "command push Push\ncommand push Other\n"}) {
        RAWFRAME_EXPECT(!parseGame(kHead + std::string{kLines}).has_value());
    }
    std::string many = kHead;
    for (std::size_t index = 0; index <= world_kest::kMaximumCommands; ++index) {
        many += "command c" + std::to_string(index) + " Push\n";
    }
    RAWFRAME_EXPECT(!parseGame(many).has_value());
}

RAWFRAME_TEST(ANodesWordsAreDeclaredByLine) {
    const std::string kHead =
        "program p.kest\ncomponent 6b1f0c2e-1d4a-4f57-9a3e-0c2b7d5e8f10 g.board rawframe.ui.Node\n"
        "component 7c2a1d3f-2e5b-4a68-8b4f-1d3c8e6f9a21 g.sign rawframe.ui.Typed\n";
    const auto kGame = parseGame(kHead + "ui g.board shows g.sign\n");
    RAWFRAME_EXPECT(kGame.has_value() && kGame->uiWords.size() == 1 && kGame->uiWords[0].node == "g.board" &&
                    kGame->uiWords[0].words == "g.sign");
    for (const std::string_view kLines : {"ui g.board shows g.board\n",
                                          "ui g.board shows g.sign\nui g.board shows g.sign\n",
                                          "ui g.board shows g.missing\n",
                                          "ui g.board displays g.sign\n"}) {
        RAWFRAME_EXPECT(!parseGame(kHead + std::string{kLines}).has_value());
    }
}
