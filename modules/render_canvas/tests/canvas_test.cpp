// The canvas's CPU half: a sprite's quad is where its pose and pivot put it,
// turned as the pose turns, seen through the camera; a sheet's frame is its
// cell; what is off the view, draws nothing, is malformed, or names an
// undeclared texture is left out and counted; sprites draw in the order of
// their layers, then entities, and batch only where the order allows, by
// texture and material (D356); the limits leave out a suffix of the order;
// particles, trails, and beams sit in the canvas's plane, seen through its
// rectangle (D357); and a game's canvas loads against its program.

#include "rawframe/physics2d/components.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/render_canvas/errors.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::render_canvas;

namespace {

constexpr auto kSpriteId = schema::ComponentTypeId::fromText("5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90");
constexpr auto kHatId = schema::ComponentTypeId::fromText("6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90");
constexpr auto kSparksId = schema::ComponentTypeId::fromText("8b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90");
constexpr auto kStreakId = schema::ComponentTypeId::fromText("9b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90");
constexpr std::uint64_t kRunner = 0xb1;
constexpr std::uint64_t kTiles = 0xb2;
constexpr std::uint64_t kGlow = 0xc1;

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{
        .id = kHatId, .name = "test.hat", .size = sizeof(Sprite), .alignment = alignof(Sprite), .plainData = true});
    builder.add(schema::ComponentDescriptor{.id = kSpriteId,
                                            .name = "test.sprite",
                                            .size = sizeof(Sprite),
                                            .alignment = alignof(Sprite),
                                            .plainData = true});
    builder.add(schema::ComponentDescriptor{.id = kSparksId,
                                            .name = "test.sparks",
                                            .size = sizeof(particles::ParticleEmitter),
                                            .alignment = alignof(particles::ParticleEmitter),
                                            .plainData = true});
    builder.add(schema::ComponentDescriptor{.id = kStreakId,
                                            .name = "test.streak",
                                            .size = sizeof(particles::Trail),
                                            .alignment = alignof(particles::Trail),
                                            .plainData = true});
    builder.add<physics2d::Pose2D>();
    return *builder.freeze();
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Canvas> canvas;

    explicit Rig(CanvasLimits limits = {}) {
        canvas =
            *Canvas::create(*schema,
                            {.sprites = {kSpriteId, kHatId},
                             .textures = {kTiles, kRunner},
                             .materials = {{kGlow, material::CanvasMaterial{.blend = material::CanvasBlend::Additive}}},
                             .emitters = {kSparksId},
                             .trails = {kStreakId},
                             .limits = limits});
    }

    world::EntityHandle spawn(Sprite sprite, std::optional<physics2d::Pose2D> pose) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kSpriteId), &sprite).has_value());
        if (pose) {
            RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<physics2d::Pose2D>(), *pose).has_value());
        }
        return kEntity;
    }

    template <typename Component>
    world::EntityHandle place(schema::ComponentTypeId id, Component component, physics2d::Pose2D pose) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(id), &component).has_value());
        RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<physics2d::Pose2D>(), pose).has_value());
        return kEntity;
    }

    const CanvasFrame& frame(const CanvasCamera& camera) {
        canvas->extract(world);
        return canvas->queue(camera);
    }
};

bool near(float value, float expected) {
    return std::abs(value - expected) < 1e-5F;
}

bool at(const CanvasVertex& vertex, float x, float y, float u, float v) {
    return near(vertex.x, x) && near(vertex.y, y) && near(vertex.u, u) && near(vertex.v, v);
}

} // namespace

RAWFRAME_TEST(ASpritesQuadIsWhereItsPoseAndPivotPutIt) {
    Rig rig;
    // Two meters wide and one high, its pivot at its bottom middle, three
    // meters right of a camera ten meters tall and twenty wide.
    rig.spawn(
        Sprite{.texture = kRunner, .u0 = 0, .v0 = 0.5F, .u1 = 0.5F, .v1 = 1, .width = 2, .height = 1, .pivotY = 0},
        physics2d::Pose2D{.x = 1003, .y = -2});
    const CanvasFrame& kFrame = rig.frame({.x = 1000, .y = -2, .height = 10, .aspect = 2});
    RAWFRAME_EXPECT(kFrame.drawn == 1 && kFrame.vertices.size() == 4 && kFrame.indices.size() == 6 &&
                    kFrame.draws.size() == 1 && kFrame.draws[0].texture == kRunner && kFrame.draws[0].indexCount == 6);
    if (kFrame.vertices.size() == 4) {
        // Counter clockwise from the bottom left; the region's top is v0.
        RAWFRAME_EXPECT(at(kFrame.vertices[0], 0.2F, 0, 0, 1) && at(kFrame.vertices[1], 0.4F, 0, 0.5F, 1) &&
                        at(kFrame.vertices[2], 0.4F, 0.2F, 0.5F, 0.5F) && at(kFrame.vertices[3], 0.2F, 0.2F, 0, 0.5F));
        RAWFRAME_EXPECT(kFrame.vertices[0].color == 0xFFFFFFFF);
    }
    RAWFRAME_EXPECT(kFrame.indices == (std::vector<std::uint32_t>{0, 1, 2, 2, 3, 0}));
}

RAWFRAME_TEST(PickingAgreesWithTheCanvasCorners) {
    // Where the view's geometry says a World point shows is where the
    // canvas puts a sprite's corner there (D366): clip space mapped to a
    // view of 800 by 400.
    Rig rig;
    rig.spawn(Sprite{.texture = kRunner, .width = 2, .height = 1, .pivotX = 0, .pivotY = 0},
              physics2d::Pose2D{.x = 1003, .y = -2});
    const CanvasCamera kCamera{.x = 1000, .y = -2, .height = 10, .aspect = 2};
    const CanvasFrame& kFrame = rig.frame(kCamera);
    RAWFRAME_EXPECT(kFrame.vertices.size() == 4);
    if (kFrame.vertices.size() == 4) {
        const view::ViewSize kSize{.width = 800, .height = 400};
        const auto kPoint = view::worldToPoint(orthographicOf(kCamera), kSize, std::array<double, 2>{1003, -2});
        const CanvasVertex& kCorner = kFrame.vertices[0];
        RAWFRAME_EXPECT(kPoint.has_value() && std::abs(kPoint->x - ((kCorner.x + 1) / 2 * 800)) < 1e-3F &&
                        std::abs(kPoint->y - ((1 - kCorner.y) / 2 * 400)) < 1e-3F);
    }
}

RAWFRAME_TEST(ASpriteTurnsAsItsPoseTurns) {
    Rig rig;
    // A quarter turn about its center: its bottom left goes to the right.
    rig.spawn(Sprite{.texture = kRunner, .width = 2, .height = 2}, physics2d::Pose2D{.c = 0, .s = 1});
    const CanvasFrame& kFrame = rig.frame({.height = 10, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.vertices.size() == 4 && near(kFrame.vertices[0].x, 0.2F) &&
                    near(kFrame.vertices[0].y, -0.2F));
    // No pose: at the origin, unturned.
    Rig unposed;
    unposed.spawn(Sprite{.texture = kRunner, .width = 2, .height = 2}, std::nullopt);
    const CanvasFrame& kUnposed = unposed.frame({.x = 1, .height = 10, .aspect = 1});
    RAWFRAME_EXPECT(kUnposed.vertices.size() == 4 && near(kUnposed.vertices[0].x, -0.4F) &&
                    near(kUnposed.vertices[0].y, -0.2F));
}

RAWFRAME_TEST(ASheetsFrameIsItsCell) {
    Rig rig;
    // A sheet of cells a quarter wide and half high, four to a row: frame
    // 5 is the second row's second cell. Mirrored, the same cell turned.
    rig.spawn(Sprite{.texture = kRunner, .u1 = 0.25F, .v1 = 0.5F, .frame = 5, .columns = 4}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .u0 = 0.25F, .u1 = 0, .v1 = 0.5F, .layer = 1, .frame = 5, .columns = 4},
              physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .u1 = 0.25F, .layer = 2, .frame = 7}, physics2d::Pose2D{});
    const CanvasFrame& kFrame = rig.frame({.height = 10});
    RAWFRAME_EXPECT(kFrame.vertices.size() == 12 && kFrame.animated == 3);
    if (kFrame.vertices.size() == 12) {
        RAWFRAME_EXPECT(near(kFrame.vertices[0].u, 0.25F) && near(kFrame.vertices[0].v, 1.0F) &&
                        near(kFrame.vertices[2].u, 0.5F) && near(kFrame.vertices[2].v, 0.5F));
        RAWFRAME_EXPECT(near(kFrame.vertices[4].u, 0.5F) && near(kFrame.vertices[5].u, 0.25F));
        // Columns nought is one: frame 7 is seven cells along, row by row.
        RAWFRAME_EXPECT(near(kFrame.vertices[8].u, 0.0F) && near(kFrame.vertices[8].v, 8.0F));
    }
}

RAWFRAME_TEST(WhatCannotBeSeenIsLeftOutAndCounted) {
    Rig rig;
    const Sprite kSeen{.texture = kRunner};
    rig.spawn(kSeen, physics2d::Pose2D{});
    // Off the view, though a corner is only just past it.
    rig.spawn(kSeen, physics2d::Pose2D{.x = 5.51});
    rig.spawn(kSeen, physics2d::Pose2D{.y = -5.6});
    // Texture nought, and a color with no alpha.
    rig.spawn(Sprite{.texture = 0}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .color = 0xFFFFFF00}, physics2d::Pose2D{});
    // A side not above nought, a value that is not a number, a pose far off
    // to infinity.
    rig.spawn(Sprite{.texture = kRunner, .width = 0}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .u0 = std::numeric_limits<float>::quiet_NaN()}, physics2d::Pose2D{});
    rig.spawn(kSeen, physics2d::Pose2D{.x = std::numeric_limits<double>::infinity()});
    // A texture the game does not declare.
    rig.spawn(Sprite{.texture = 0xdead}, physics2d::Pose2D{});
    const CanvasFrame& kFrame = rig.frame({.height = 10, .aspect = 1});
    RAWFRAME_EXPECT(kFrame.drawn == 1 && kFrame.culled == 2 && kFrame.hidden == 2 && kFrame.malformed == 3 &&
                    kFrame.unknownTextures == 1 && kFrame.overLimit == 0 && kFrame.vertices.size() == 4);
    // A camera that sees nothing culls all it would have drawn.
    const CanvasFrame& kBlind = rig.frame({.height = 0});
    RAWFRAME_EXPECT(kBlind.drawn == 0 && kBlind.culled == 3 && kBlind.vertices.empty());
}

RAWFRAME_TEST(SpritesDrawInOrderAndBatchOnlyWhereTheOrderAllows) {
    Rig rig;
    // By layer, then entity: tiles(0) runner(0) tiles(1) tiles(1) runner(2).
    const auto kFirst = rig.spawn(Sprite{.texture = kTiles, .layer = 0}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .layer = 2}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kTiles, .layer = 1}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kTiles, .layer = 1}, physics2d::Pose2D{});
    const auto kFifth = rig.spawn(Sprite{.texture = kRunner, .layer = 0}, physics2d::Pose2D{});
    const CanvasFrame& kFrame = rig.frame({.height = 10});
    RAWFRAME_EXPECT(kFrame.drawn == 5 && kFrame.draws.size() == 4);
    if (kFrame.draws.size() == 4) {
        RAWFRAME_EXPECT(kFrame.draws[0].texture == kTiles && kFrame.draws[1].texture == kRunner &&
                        kFrame.draws[2].texture == kTiles && kFrame.draws[2].indexCount == 12 &&
                        kFrame.draws[2].firstIndex == 12 && kFrame.draws[3].texture == kRunner &&
                        kFrame.draws[3].firstIndex == 24);
    }
    // Each frame is queued afresh: what left the World is gone from it, and
    // the two tiles left side by side batch into one draw.
    RAWFRAME_EXPECT(rig.world.destroy(kFirst).has_value() && rig.world.destroy(kFifth).has_value());
    const CanvasFrame& kAgain = rig.frame({.height = 10});
    RAWFRAME_EXPECT(kAgain.drawn == 3 && kAgain.draws.size() == 2 && kAgain.draws[0].indexCount == 12 &&
                    kAgain.vertices.size() == 12);
}

RAWFRAME_TEST(AnEntityShowsOneSpriteOfEachComponent) {
    // A body and its hat on one entity, one layer: the body first, as the
    // game orders its components; a hat alone draws too.
    Rig rig;
    const auto kEntity = rig.spawn(Sprite{.texture = kRunner}, physics2d::Pose2D{});
    Sprite hat{.texture = kTiles, .height = 0.5F};
    RAWFRAME_EXPECT(rig.world.insertErased(kEntity, *rig.schema->find(kHatId), &hat).has_value());
    const world::EntityHandle kHatOnly = *rig.world.create();
    RAWFRAME_EXPECT(rig.world.insertErased(kHatOnly, *rig.schema->find(kHatId), &hat).has_value());
    const CanvasFrame& kFrame = rig.frame({.height = 10});
    RAWFRAME_EXPECT(kFrame.drawn == 3 && kFrame.draws.size() == 2 && kFrame.draws[0].texture == kRunner &&
                    kFrame.draws[1].texture == kTiles && kFrame.draws[1].indexCount == 12);
}

RAWFRAME_TEST(TheLimitsLeaveOutTheLastInOrder) {
    Rig sprites{{.maximumSprites = 2}};
    for (std::int32_t layer = 0; layer < 4; ++layer) {
        sprites.spawn(
            Sprite{.texture = kRunner, .color = 0x000000FFU + static_cast<std::uint32_t>(layer << 8), .layer = layer},
            physics2d::Pose2D{});
    }
    const CanvasFrame& kSprites = sprites.frame({.height = 10});
    RAWFRAME_EXPECT(kSprites.drawn == 2 && kSprites.overLimit == 2 && kSprites.vertices.size() == 8 &&
                    kSprites.vertices[4].color == 0x000001FFU);
    // Two draws at most: the third texture change and all after it go.
    Rig draws{{.maximumDraws = 2}};
    std::int32_t layer = 0;
    for (const std::uint64_t kTexture : {kRunner, kTiles, kRunner, kTiles}) {
        draws.spawn(Sprite{.texture = kTexture, .layer = layer++}, physics2d::Pose2D{});
    }
    const CanvasFrame& kDraws = draws.frame({.height = 10});
    RAWFRAME_EXPECT(kDraws.drawn == 2 && kDraws.draws.size() == 2 && kDraws.overLimit == 2);
}

RAWFRAME_TEST(AGamesCanvasLoadsAgainstItsProgram) {
    const auto kLoad = [](std::string_view kest, std::string_view gameLines) {
        std::vector<std::pair<std::string, std::string>> held;
        held.emplace_back("drawn.kest", std::string{"module drawn\n\nimport rawframe.canvas\n\n"} + std::string{kest});
        held.emplace_back("drawn.game", std::string{"program drawn.kest\n"} + std::string{gameLines});
        for (const char* kImage : {"tiles.png", "runner.png"}) {
            held.emplace_back(std::string{kImage} + ".rfmeta",
                              "{\n  \"schema\": 1,\n  \"resourceId\": \"a6478ea1aa844c683e293a9754feeb95\",\n  "
                              "\"importer\": \"rawframe.texture\"\n}\n");
        }
        const auto kFiles = world_kest::GameFiles::fromHeld("drawn.game", std::move(held));
        RAWFRAME_EXPECT(kFiles.has_value());
        std::string report;
        const auto kProgram = kFiles->compile("drawn.kest", {}, &report);
        RAWFRAME_EXPECT(kProgram.has_value());
        if (!kProgram.has_value()) {
            std::fprintf(stderr, "%s\n", report.c_str());
        }
        return loadGameCanvas(*kFiles, **kProgram);
    };
    // A Kest type is laid out when a function uses it.
    const std::string kUses = "fn draw(sprites: [canvas.Sprite], views: [canvas.Camera]) {\n}\n";
    const std::string kView = "component 6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.view rawframe.canvas.Camera\n";
    const auto kLoaded = kLoad(kUses,
                               "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n" +
                                   kView + "texture 00000000000000b2 tiles.png\ntexture 00000000000000b1 runner.png\n");
    RAWFRAME_EXPECT(kLoaded.has_value() && kLoaded->sprites == (std::vector<schema::ComponentTypeId>{kSpriteId}) &&
                    kLoaded->textures == (std::vector<std::uint64_t>{kTiles, kRunner}) &&
                    kLoaded->camera == schema::ComponentTypeId::fromText("6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90"));
    const auto kPlain =
        kLoad(kUses, "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n");
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->textures.empty() && !kPlain->camera.has_value());
    // A client has one view (D261).
    const auto kTwoViews =
        kLoad(kUses,
              "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n" + kView +
                  "component 7b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.other rawframe.canvas.Camera\n");
    RAWFRAME_EXPECT(!kTwoViews.has_value() && kTwoViews.error().code() == code(RenderCanvasError::BadComponents));
    const auto kNone = kLoad(kUses, "");
    RAWFRAME_EXPECT(!kNone.has_value() && kNone.error().code() == code(RenderCanvasError::NoSprites));
    // A type of the game's own by that name is not rawframe.canvas's.
    const auto kOwn = kLoad("struct Sprite {\n    texture: u64\n}\n\nfn draw(sprites: [Sprite]) {\n}\n",
                            "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look Sprite\n");
    RAWFRAME_EXPECT(!kOwn.has_value() && kOwn.error().code() == code(RenderCanvasError::BadComponents));
    const auto kTwo = kLoad(kUses,
                            "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n"
                            "component 6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.other rawframe.canvas.Sprite\n");
    RAWFRAME_EXPECT(kTwo.has_value() && kTwo->sprites == (std::vector<schema::ComponentTypeId>{kSpriteId, kHatId}));
    // The particle triad of rawframe.model, as the scene finds it (D357).
    const auto kSparkling =
        kLoad("import rawframe.model\n\nfn draw(sprites: [canvas.Sprite], sparks: [model.ParticleEmitter], streaks: "
              "[model.Trail], rays: [model.Beam]) {\n}\n",
              "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n"
              "component 8b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.sparks rawframe.model.ParticleEmitter\n"
              "component 9b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.streak rawframe.model.Trail\n"
              "component ab1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.ray rawframe.model.Beam\n");
    RAWFRAME_EXPECT(
        kSparkling.has_value() && kSparkling->emitters == (std::vector<schema::ComponentTypeId>{kSparksId}) &&
        kSparkling->trails == (std::vector<schema::ComponentTypeId>{kStreakId}) && kSparkling->beams.size() == 1);
}

RAWFRAME_TEST(SpritesDrawByTheirMaterials) {
    // A runner, then the same runner glowing, then a glow with no texture,
    // then one naming a material the game has not (D356).
    Rig rig;
    rig.spawn(Sprite{.texture = kRunner}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .layer = 1, .material = kGlow}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = 0, .layer = 2, .material = kGlow}, physics2d::Pose2D{});
    rig.spawn(Sprite{.texture = kRunner, .layer = 3, .material = 0xdead}, physics2d::Pose2D{});
    const CanvasFrame& kFrame = rig.frame({.height = 10});
    RAWFRAME_EXPECT(kFrame.drawn == 3 && kFrame.unknownMaterials == 1 && kFrame.draws.size() == 3);
    RAWFRAME_EXPECT(kFrame.materials.size() == 2 && kFrame.materials[1].blend == material::CanvasBlend::Additive);
    if (kFrame.draws.size() == 3) {
        RAWFRAME_EXPECT(kFrame.draws[0].texture == kRunner && kFrame.draws[0].material == 0);
        RAWFRAME_EXPECT(kFrame.draws[1].texture == kRunner && kFrame.draws[1].material == 1);
        RAWFRAME_EXPECT(kFrame.draws[2].texture == 0 && kFrame.draws[2].material == 1);
    }
}

RAWFRAME_TEST(ParticlesSitInTheCanvasPlane) {
    // Far along the plane, so the anchors are placed relative to the eye.
    constexpr double kX = 100000;
    Rig rig;
    // Glowing sparks above the view's middle, turned a quarter so their up
    // is -X; and sparks past the view's right edge.
    const particles::ParticleEmitter kSparks{
        .material = kGlow, .rate = 10, .lifetime = 1, .speed = 1, .sizeStart = 0.2F, .sizeEnd = 0.2F};
    rig.place(kSparksId, kSparks, {.x = kX, .y = 3, .c = 0, .s = 1});
    rig.place(kSparksId, kSparks, {.x = kX + 12, .y = 0, .c = 1, .s = 0});
    // A streak, moved a meter between frames.
    const world::EntityHandle kStreak = rig.place(
        kStreakId, particles::Trail{.lifetime = 1, .spacing = 0.5F, .widthStart = 0.2F}, {.x = kX, .y = -2, .c = 1});
    const CanvasCamera kCamera{.x = kX, .y = 0, .height = 10, .aspect = 1, .elapsed = 0.1F};
    static_cast<void>(rig.frame(kCamera));
    auto* pose = rig.world.get(kStreak, *rig.schema->key<physics2d::Pose2D>());
    RAWFRAME_EXPECT(pose != nullptr);
    if (pose != nullptr) {
        pose->x = kX + 1;
    }
    const CanvasFrame& kFrame = rig.frame(kCamera);
    RAWFRAME_EXPECT(near(kFrame.extent[0], 5) && near(kFrame.extent[1], 5));
    RAWFRAME_EXPECT(kFrame.particles.emitters.size() == 1 && kFrame.particles.emittersLeftOut == 0);
    if (kFrame.particles.emitters.size() == 1) {
        const particles::EmitterDraw& kDrawn = kFrame.particles.emitters[0];
        RAWFRAME_EXPECT(near(kDrawn.anchor[0], 0) && near(kDrawn.anchor[1], 3) && near(kDrawn.anchor[2], 0));
        RAWFRAME_EXPECT(near(kDrawn.direction[0], -1) && near(kDrawn.direction[1], 0));
        // Its material by its place among the frame's: the glow's.
        RAWFRAME_EXPECT(kDrawn.material == 1 && kDrawn.spawned == 1);
    }
    RAWFRAME_EXPECT(kFrame.particles.ribbons.size() == 1 && kFrame.particles.ribbonPoints.size() == 2);
    if (kFrame.particles.ribbonPoints.size() == 2) {
        const particles::RibbonPoint& kHead = kFrame.particles.ribbonPoints[0];
        RAWFRAME_EXPECT(near(kHead.place[0], 1) && near(kHead.place[1], -2) && near(kHead.place[2], 0));
    }
}
