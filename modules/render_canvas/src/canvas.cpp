#include "rawframe/render_canvas/canvas.h"

#include "rawframe/physics2d/components.h"
#include "rawframe/render_canvas/errors.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>

namespace rawframe::render_canvas {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderCanvasError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderCanvasDomain, code(error), why).error()};
}

/// Whether a sprite's values are all ones it can be drawn with.
bool wellFormed(const SpriteInstance& instance) noexcept {
    const Sprite& kSprite = instance.sprite;
    const std::array<float, 10> kValues = {kSprite.u0,
                                           kSprite.v0,
                                           kSprite.u1,
                                           kSprite.v1,
                                           kSprite.pivotX,
                                           kSprite.pivotY,
                                           instance.c,
                                           instance.s,
                                           kSprite.width,
                                           kSprite.height};
    return std::ranges::all_of(kValues,
                               [](float value) {
                                   return std::isfinite(value);
                               }) &&
           std::isfinite(instance.x) && std::isfinite(instance.y) && kSprite.width > 0 && kSprite.height > 0;
}

} // namespace

struct Canvas::State {
    CanvasSettings settings;
    std::optional<world::ColumnQuery> sprites;
    std::optional<schema::ComponentRuntimeId> pose;
    std::vector<SpriteInstance> extracted;
    std::vector<const SpriteInstance*> order;
    CanvasFrame frame;

    /// The quad's corners in clip space, bottom left first, counter
    /// clockwise.
    static std::array<CanvasVertex, 4> corners(const SpriteInstance& instance, const CanvasCamera& camera) {
        const Sprite& kSprite = instance.sprite;
        const bool kTurned = instance.c != 0 || instance.s != 0;
        const float kC = kTurned ? instance.c : 1.0F;
        const float kS = kTurned ? instance.s : 0.0F;
        const float kLeft = -kSprite.pivotX * kSprite.width;
        const float kRight = (1 - kSprite.pivotX) * kSprite.width;
        const float kBottom = -kSprite.pivotY * kSprite.height;
        const float kTop = (1 - kSprite.pivotY) * kSprite.height;
        // The camera's offset in double, where a pose is: far from the
        // origin, a float would lose the sprite's place before it is small.
        const auto kDx = static_cast<float>(instance.x - camera.x);
        const auto kDy = static_cast<float>(instance.y - camera.y);
        const float kHalfHeight = camera.height / 2;
        const float kHalfWidth = kHalfHeight * camera.aspect;
        const auto kCorner = [&](float x, float y, float u, float v) {
            return CanvasVertex{.x = (kDx + (kC * x) - (kS * y)) / kHalfWidth,
                                .y = (kDy + (kS * x) + (kC * y)) / kHalfHeight,
                                .u = u,
                                .v = v,
                                .color = kSprite.color};
        };
        return {kCorner(kLeft, kBottom, kSprite.u0, kSprite.v1),
                kCorner(kRight, kBottom, kSprite.u1, kSprite.v1),
                kCorner(kRight, kTop, kSprite.u1, kSprite.v0),
                kCorner(kLeft, kTop, kSprite.u0, kSprite.v0)};
    }

    const CanvasFrame& queue(const CanvasCamera& camera) {
        frame.vertices.clear();
        frame.indices.clear();
        frame.draws.clear();
        frame.drawn = 0;
        frame.culled = 0;
        frame.hidden = 0;
        frame.malformed = 0;
        frame.unknownTextures = 0;
        frame.overLimit = 0;
        order.clear();
        for (const SpriteInstance& instance : extracted) {
            order.push_back(&instance);
        }
        std::ranges::sort(order, [](const SpriteInstance* left, const SpriteInstance* right) {
            return std::pair{left->sprite.layer, left->entity} < std::pair{right->sprite.layer, right->entity};
        });
        // A camera that sees nothing culls everything.
        const bool kSees = std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.height) &&
                           std::isfinite(camera.aspect) && camera.height > 0 && camera.aspect > 0;
        bool full = false;
        for (const SpriteInstance* instance : order) {
            const Sprite& kSprite = instance->sprite;
            if (!wellFormed(*instance)) {
                ++frame.malformed;
                continue;
            }
            if (kSprite.texture == 0 || (kSprite.color & 0xFFU) == 0) {
                ++frame.hidden;
                continue;
            }
            if (!std::ranges::binary_search(settings.textures, kSprite.texture)) {
                ++frame.unknownTextures;
                continue;
            }
            const std::array<CanvasVertex, 4> kCorners =
                kSees ? corners(*instance, camera) : std::array<CanvasVertex, 4>{};
            const auto [kLowX, kHighX] = std::ranges::minmax(kCorners, {}, &CanvasVertex::x);
            const auto [kLowY, kHighY] = std::ranges::minmax(kCorners, {}, &CanvasVertex::y);
            if (!kSees || kHighX.x < -1 || kLowX.x > 1 || kHighY.y < -1 || kLowY.y > 1) {
                ++frame.culled;
                continue;
            }
            // Past a limit, this sprite and every one after it are left out,
            // so what is drawn is a prefix of the order.
            const bool kNewDraw = frame.draws.empty() || frame.draws.back().texture != kSprite.texture;
            full = full || frame.drawn == settings.limits.maximumSprites ||
                   (kNewDraw && frame.draws.size() == settings.limits.maximumDraws);
            if (full) {
                ++frame.overLimit;
                continue;
            }
            if (kNewDraw) {
                frame.draws.push_back(CanvasDraw{.texture = kSprite.texture,
                                                 .firstIndex = static_cast<std::uint32_t>(frame.indices.size())});
            }
            const auto kFirst = static_cast<std::uint32_t>(frame.vertices.size());
            frame.vertices.insert(frame.vertices.end(), kCorners.begin(), kCorners.end());
            for (const std::uint32_t kCorner : {0U, 1U, 2U, 2U, 3U, 0U}) {
                frame.indices.push_back(kFirst + kCorner);
            }
            frame.draws.back().indexCount += 6;
            ++frame.drawn;
        }
        return frame;
    }
};

Canvas::Canvas(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Canvas::~Canvas() = default;

result::Result<std::unique_ptr<Canvas>> Canvas::create(const schema::SchemaRegistry& registry,
                                                       CanvasSettings settings) {
    RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kSprite, registry.find(settings.sprite));
    if (registry.descriptor(kSprite).size != sizeof(Sprite)) {
        return refuse(result::ErrorClass::InvalidArgument,
                      RenderCanvasError::BadComponents,
                      "the sprite component is not rawframe.canvas's size");
    }
    auto state = std::make_unique<State>();
    std::ranges::sort(settings.textures);
    state->settings = std::move(settings);
    const std::array<world::ColumnTerm, 1> kSprites = {world::ColumnTerm{kSprite, world::Access::Read}};
    RAWFRAME_TRY_ASSIGN(state->sprites, world::ColumnQuery::resolve(kSprites, registry));
    if (const auto kPose = registry.find(physics2d::Pose2D::kComponentTypeId)) {
        state->pose = *kPose;
    }
    return std::unique_ptr<Canvas>{new Canvas{std::move(state)}};
}

void Canvas::extract(world::World& world) {
    State& state = *state_;
    state.extracted.clear();
    state.sprites->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            SpriteInstance instance{.entity = chunk.entities[row]};
            std::memcpy(&instance.sprite, chunk.columns[0] + (row * sizeof(Sprite)), sizeof(Sprite));
            if (state.pose) {
                if (const auto* pose =
                        static_cast<const physics2d::Pose2D*>(world.getErased(instance.entity, *state.pose))) {
                    instance.x = pose->x;
                    instance.y = pose->y;
                    instance.c = pose->c;
                    instance.s = pose->s;
                }
            }
            state.extracted.push_back(instance);
        }
    });
}

const CanvasFrame& Canvas::queue(const CanvasCamera& camera) {
    return state_->queue(camera);
}

std::span<const SpriteInstance> Canvas::extracted() const noexcept {
    return state_->extracted;
}

result::Result<GameCanvas> loadGameCanvas(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    const world_kest::GameComponent* sprite = nullptr;
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (!world_kest::ofEngineType(component, "rawframe.canvas.Sprite")) {
            continue;
        }
        if (sprite != nullptr) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderCanvasError::BadComponents,
                          "a game declares one sprite component");
        }
        sprite = &component;
    }
    if (sprite == nullptr) {
        return refuse(result::ErrorClass::NotFound, RenderCanvasError::NoSprites, "the game declares no sprites");
    }
    // By its full name: a game's own type called `Sprite` is not this one.
    if (!world_kest::laidOutAs(program,
                               "rawframe.canvas.Sprite",
                               sizeof(Sprite),
                               {{"texture", offsetof(Sprite, texture)},
                                {"u0", offsetof(Sprite, u0)},
                                {"v0", offsetof(Sprite, v0)},
                                {"u1", offsetof(Sprite, u1)},
                                {"v1", offsetof(Sprite, v1)},
                                {"width", offsetof(Sprite, width)},
                                {"height", offsetof(Sprite, height)},
                                {"pivotX", offsetof(Sprite, pivotX)},
                                {"pivotY", offsetof(Sprite, pivotY)},
                                {"color", offsetof(Sprite, color)},
                                {"layer", offsetof(Sprite, layer)}})) {
        return refuse(result::ErrorClass::InvalidArgument,
                      RenderCanvasError::BadComponents,
                      "the program lays out rawframe.canvas's Sprite otherwise than this engine reads it");
    }
    GameCanvas loaded{.sprite = sprite->id, .cameraHeight = kDescription.cameraHeight.value_or(10.0F)};
    for (const world_kest::GameTexture& texture : kDescription.textures) {
        loaded.textures.push_back(texture.id);
    }
    return loaded;
}

} // namespace rawframe::render_canvas
