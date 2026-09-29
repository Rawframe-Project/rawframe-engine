// The canvas's CPU half: a sprite's quad is where its pose and pivot put it,
// turned as the pose turns, seen through the camera; a sheet's frame is its
// cell; what is off the view, draws nothing, is malformed, or names an
// undeclared texture is left out and counted; sprites draw in the order of
// their layers, then entities, and batch only where the order allows; the
// limits leave out a suffix of the order; a game's canvas loads against its
// program; and its textures are read decoded by identity from cooked
// content.

#include "rawframe/physics2d/components.h"
#include "rawframe/render_canvas/canvas.h"
#include "rawframe/render_canvas/errors.h"
#include "rawframe/render_canvas/textures.h"
#include "rawframe/test/test.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;
using namespace rawframe::render_canvas;

namespace {

constexpr auto kSpriteId = schema::ComponentTypeId::fromText("5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90");
constexpr std::uint64_t kRunner = 0xb1;
constexpr std::uint64_t kTiles = 0xb2;

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(schema::ComponentDescriptor{.id = kSpriteId,
                                            .name = "test.sprite",
                                            .size = sizeof(Sprite),
                                            .alignment = alignof(Sprite),
                                            .plainData = true});
    builder.add<physics2d::Pose2D>();
    return *builder.freeze();
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Canvas> canvas;

    explicit Rig(CanvasLimits limits = {}) {
        canvas = *Canvas::create(*schema, {.sprite = kSpriteId, .textures = {kTiles, kRunner}, .limits = limits});
    }

    world::EntityHandle spawn(Sprite sprite, std::optional<physics2d::Pose2D> pose) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kSpriteId), &sprite).has_value());
        if (pose) {
            RAWFRAME_EXPECT(world.insert(kEntity, *schema->key<physics2d::Pose2D>(), *pose).has_value());
        }
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
    const std::string kUses = "fn draw(sprites: [canvas.Sprite]) {\n}\n";
    const auto kLoaded = kLoad(kUses,
                               "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n"
                               "texture 00000000000000b2 tiles.png\ntexture 00000000000000b1 runner.png\ncamera 12\n");
    RAWFRAME_EXPECT(kLoaded.has_value() && kLoaded->sprite == kSpriteId &&
                    kLoaded->textures == (std::vector<std::uint64_t>{kTiles, kRunner}) && kLoaded->cameraHeight == 12);
    const auto kPlain =
        kLoad(kUses, "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n");
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->textures.empty() && kPlain->cameraHeight == 10);
    const auto kNone = kLoad(kUses, "");
    RAWFRAME_EXPECT(!kNone.has_value() && kNone.error().code() == code(RenderCanvasError::NoSprites));
    // A type of the game's own by that name is not rawframe.canvas's.
    const auto kOwn = kLoad("struct Sprite {\n    texture: u64\n}\n\nfn draw(sprites: [Sprite]) {\n}\n",
                            "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look Sprite\n");
    RAWFRAME_EXPECT(!kOwn.has_value() && kOwn.error().code() == code(RenderCanvasError::BadComponents));
    const auto kTwo = kLoad(kUses,
                            "component 5b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.look rawframe.canvas.Sprite\n"
                            "component 6b1d8f0e-2a44-4c1f-9d0e-7a6c3b2e1f90 drawn.other rawframe.canvas.Sprite\n");
    RAWFRAME_EXPECT(!kTwo.has_value() && kTwo.error().code() == code(RenderCanvasError::BadComponents));
}

namespace {

/// A content store over two cooked textures, as a client composes it from a
/// game's cooked output: 1 is a 2 by 2 texture, 2's bytes are no texture.
struct Content {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    execution::Executor cpu{execution::ExecutorSettings{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    std::unique_ptr<content::ContentStore> store;

    static std::vector<std::byte> cooked() {
        texture::Texture made{.format = texture::Format::Rgba8Srgb};
        made.levels.push_back(
            texture::Level{.width = 2, .height = 2, .bytes = std::vector<std::byte>(16, std::byte{9})});
        return *texture::encode(made);
    }

    static content::ManifestEntry
    entryOf(std::uint64_t id, const std::string& locator, const std::vector<std::byte>& bytes) {
        return content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                      .type = content::ResourceTypeId{texture::kTextureType},
                                      .representation =
                                          *content::RepresentationId::parse(texture::kTextureRepresentation),
                                      .byteLength = bytes.size(),
                                      .digest = content::ContentDigest::of(bytes),
                                      .locator = locator};
    }

    Content() {
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        RAWFRAME_EXPECT(cpu.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 16}).has_value());
        const std::vector<std::byte> kGood = cooked();
        const std::vector<std::byte> kBroken(64, std::byte{7});
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(*content::ContentSource::memory({{"a", kGood}, {"b", kBroken}})));
        store = std::move(*content::ContentStore::create(io, execution::OwnerId{1}, root, clock, std::move(sources)));
        const std::vector<content::BoundManifest> kManifests = {
            {.entries = {entryOf(1, "a", kGood), entryOf(2, "b", kBroken)}, .source = 0}};
        store->publish(*content::ContentCatalog::build(kManifests, textureRepresentations(), 1, 1));
    }
    ~Content() {
        store.reset();
        cpu.stop();
        io.stop();
    }
    Content(const Content&) = delete;
    Content& operator=(const Content&) = delete;

    result::Result<std::unique_ptr<CanvasTextures>> textures(std::vector<world_kest::GameTextureResource> declared) {
        return CanvasTextures::create(*store, cpu, execution::OwnerId{1}, root, clock, std::move(declared), 1U << 20U);
    }

    /// Updates `textures` until none is pending, within ten seconds; the
    /// failures it reported.
    std::vector<std::pair<std::uint64_t, result::Error>> settle(CanvasTextures& textures) {
        std::vector<std::pair<std::uint64_t, result::Error>> failed;
        const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < kDeadline) {
            for (auto& each : textures.update(1)) {
                failed.push_back(std::move(each));
            }
            if (textures.counts().pending == 0) {
                break;
            }
#if RAWFRAME_THREADS
            std::this_thread::yield();
#else
            while (io.runOne() || cpu.runOne()) {
            }
#endif
        }
        return failed;
    }
};

world_kest::GameTextureResource declared(std::uint64_t id, std::uint64_t resource) {
    return {.id = id, .path = "t" + std::to_string(id) + ".png", .texture = base::Bits128{.high = 0, .low = resource}};
}

} // namespace

RAWFRAME_TEST(TexturesAreReadByIdentityFromCookedContent) {
    Content content;
    auto textures = content.textures({declared(kRunner, 1), declared(kTiles, 2)});
    RAWFRAME_EXPECT(textures.has_value());
    if (!textures.has_value()) {
        return;
    }
    // The good one decodes to its levels; the broken one fails, reported
    // once, by the identity the game gives it.
    const auto kFailed = content.settle(**textures);
    RAWFRAME_EXPECT(kFailed.size() == 1 && kFailed[0].first == kTiles && (*textures)->update(1).empty());
    const auto kRunnerTexture = (*textures)->texture(kRunner, 1);
    RAWFRAME_EXPECT(kRunnerTexture != nullptr && kRunnerTexture->levels.size() == 1 &&
                    kRunnerTexture->levels[0].width == 2 && kRunnerTexture->format == texture::Format::Rgba8Srgb);
    RAWFRAME_EXPECT((*textures)->texture(kTiles, 1) == nullptr && (*textures)->texture(0xdead, 1) == nullptr);
    const TextureCounts kCounts = (*textures)->counts();
    RAWFRAME_EXPECT(kCounts.ready == 1 && kCounts.failed == 1 && kCounts.pending == 0 && kCounts.bytes == 16);
    // A texture the content does not hold is refused when asked for.
    RAWFRAME_EXPECT(!content.textures({declared(kRunner, 9)}).has_value());
}
