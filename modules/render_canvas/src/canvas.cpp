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
#include <map>
#include <optional>
#include <tuple>
#include <type_traits>

namespace rawframe::render_canvas {

using particles::BeamInstance;
using particles::EmitterInstance;
using particles::TrailInstance;

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
    /// The game's canvas materials by place, none first, and their places
    /// by identity (D356).
    std::vector<material::CanvasMaterial> materials;
    std::map<std::uint64_t, std::uint32_t> materialPlaces;
    std::vector<world::ColumnQuery> sprites;
    /// The particle emitters', trails', and beams' queries, what the frame
    /// extracted, and the view's accounting of them from frame to frame
    /// (D357).
    std::vector<world::ColumnQuery> emitterQueries;
    std::vector<world::ColumnQuery> trailQueries;
    std::vector<world::ColumnQuery> beamQueries;
    std::vector<EmitterInstance> emitters;
    std::vector<TrailInstance> trails;
    std::vector<BeamInstance> beams;
    rawframe::particles::Particles particles;
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
        // The frame's cell: the region moved along its row by its width,
        // then down by its height; a mirrored region moves the same way.
        const std::uint32_t kColumns = std::max(kSprite.columns, 1U);
        const auto kColumn = static_cast<float>(kSprite.frame % kColumns);
        const auto kRow = static_cast<float>(kSprite.frame / kColumns);
        const float kU = kColumn * std::abs(kSprite.u1 - kSprite.u0);
        const float kV = kRow * std::abs(kSprite.v1 - kSprite.v0);
        const float kHalfHeight = camera.height / 2;
        const float kHalfWidth = kHalfHeight * camera.aspect;
        const auto kCorner = [&](float x, float y, float u, float v) {
            return CanvasVertex{.x = (kDx + (kC * x) - (kS * y)) / kHalfWidth,
                                .y = (kDy + (kS * x) + (kC * y)) / kHalfHeight,
                                .u = u,
                                .v = v,
                                .color = kSprite.color};
        };
        return {kCorner(kLeft, kBottom, kSprite.u0 + kU, kSprite.v1 + kV),
                kCorner(kRight, kBottom, kSprite.u1 + kU, kSprite.v1 + kV),
                kCorner(kRight, kTop, kSprite.u1 + kU, kSprite.v0 + kV),
                kCorner(kLeft, kTop, kSprite.u0 + kU, kSprite.v0 + kV)};
    }

    const CanvasFrame& queue(const CanvasCamera& camera) {
        frame.vertices.clear();
        frame.indices.clear();
        frame.draws.clear();
        frame.drawn = 0;
        frame.animated = 0;
        frame.culled = 0;
        frame.hidden = 0;
        frame.malformed = 0;
        frame.unknownTextures = 0;
        frame.unknownMaterials = 0;
        frame.overLimit = 0;
        frame.materials = materials;
        order.clear();
        for (const SpriteInstance& instance : extracted) {
            order.push_back(&instance);
        }
        std::ranges::sort(order, [](const SpriteInstance* left, const SpriteInstance* right) {
            return std::tuple{left->sprite.layer, left->entity, left->component} <
                   std::tuple{right->sprite.layer, right->entity, right->component};
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
            if ((kSprite.texture == 0 && kSprite.material == 0) || (kSprite.color & 0xFFU) == 0) {
                ++frame.hidden;
                continue;
            }
            if (kSprite.texture != 0 && !std::ranges::binary_search(settings.textures, kSprite.texture)) {
                ++frame.unknownTextures;
                continue;
            }
            const auto kPlace = materialPlaces.find(kSprite.material);
            if (kPlace == materialPlaces.end()) {
                ++frame.unknownMaterials;
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
            const bool kNewDraw = frame.draws.empty() || frame.draws.back().texture != kSprite.texture ||
                                  frame.draws.back().material != kPlace->second;
            full = full || frame.drawn == settings.limits.maximumSprites ||
                   (kNewDraw && frame.draws.size() == settings.limits.maximumDraws);
            if (full) {
                ++frame.overLimit;
                continue;
            }
            if (kNewDraw) {
                frame.draws.push_back(CanvasDraw{.texture = kSprite.texture,
                                                 .material = kPlace->second,
                                                 .firstIndex = static_cast<std::uint32_t>(frame.indices.size())});
            }
            const auto kFirst = static_cast<std::uint32_t>(frame.vertices.size());
            frame.vertices.insert(frame.vertices.end(), kCorners.begin(), kCorners.end());
            for (const std::uint32_t kCorner : {0U, 1U, 2U, 2U, 3U, 0U}) {
                frame.indices.push_back(kFirst + kCorner);
            }
            frame.draws.back().indexCount += 6;
            ++frame.drawn;
            frame.animated += kSprite.frame != 0 ? 1 : 0;
        }
        // The particles over every sprite, in the canvas's plane: what
        // reaches the view's rectangle is seen (D357).
        const float kHalfHeight = kSees ? camera.height / 2 : 0.0F;
        const float kHalfWidth = kHalfHeight * (kSees ? camera.aspect : 0.0F);
        frame.extent = {kHalfWidth, kHalfHeight};
        particles.update(
            frame.particles,
            emitters,
            trails,
            beams,
            {.eye = {kSees ? camera.x : 0, kSees ? camera.y : 0, 0},
             .sees =
                 [kSees, kHalfWidth, kHalfHeight](const rawframe::particles::Vector& center, float radius) {
                     return kSees && std::abs(center[0]) - radius <= kHalfWidth &&
                            std::abs(center[1]) - radius <= kHalfHeight;
                 }},
            materialPlaces,
            settings.limits.particles,
            camera.elapsed);
        return frame;
    }
};

Canvas::Canvas(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Canvas::~Canvas() = default;

result::Result<std::unique_ptr<Canvas>> Canvas::create(const schema::SchemaRegistry& registry,
                                                       CanvasSettings settings) {
    auto state = std::make_unique<State>();
    for (const schema::ComponentTypeId kId : settings.sprites) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kSprite, registry.find(kId));
        if (registry.descriptor(kSprite).size != sizeof(Sprite)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderCanvasError::BadComponents,
                          "a sprite component is not rawframe.canvas's size");
        }
        const std::array<world::ColumnTerm, 1> kSprites = {world::ColumnTerm{kSprite, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kSprites, registry));
        state->sprites.push_back(std::move(query));
    }
    // The particle triad's components, each of its type's size (D357).
    for (const auto& [kIds, kSize, kQueries] :
         {std::tuple{&settings.emitters, sizeof(particles::ParticleEmitter), &state->emitterQueries},
          std::tuple{&settings.trails, sizeof(particles::Trail), &state->trailQueries},
          std::tuple{&settings.beams, sizeof(particles::Beam), &state->beamQueries}}) {
        for (const schema::ComponentTypeId kId : *kIds) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(kId));
            if (registry.descriptor(kComponent).size != kSize) {
                return refuse(result::ErrorClass::InvalidArgument,
                              RenderCanvasError::BadComponents,
                              "a particle component is not rawframe.model's size");
            }
            const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kComponent, world::Access::Read}};
            RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, registry));
            kQueries->push_back(std::move(query));
        }
    }
    std::ranges::sort(settings.textures);
    // Material nought is none: white, over what is behind.
    state->materials.emplace_back(material::CanvasMaterial{.shading = material::Shading::Unlit});
    state->materialPlaces.emplace(0, 0);
    for (const auto& [kId, kMaterial] : settings.materials) {
        if (kId != 0 &&
            state->materialPlaces.emplace(kId, static_cast<std::uint32_t>(state->materials.size())).second) {
            state->materials.push_back(kMaterial);
        }
    }
    state->settings = std::move(settings);
    if (const auto kPose = registry.find(physics2d::Pose2D::kComponentTypeId)) {
        state->pose = *kPose;
    }
    return std::unique_ptr<Canvas>{new Canvas{std::move(state)}};
}

void Canvas::extract(world::World& world) {
    State& state = *state_;
    state.extracted.clear();
    for (std::size_t component = 0; component < state.sprites.size(); ++component) {
        state.sprites[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                SpriteInstance instance{.entity = chunk.entities[row],
                                        .component = static_cast<std::uint32_t>(component)};
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
    // Where a pose puts each of the triad, in the canvas's plane; an
    // emitter emitting along its pose's up (D357).
    const auto kPoseOf = [&world, &state](world::EntityHandle entity) -> const physics2d::Pose2D* {
        return state.pose ? static_cast<const physics2d::Pose2D*>(world.getErased(entity, *state.pose)) : nullptr;
    };
    state.emitters.clear();
    for (std::size_t component = 0; component < state.emitterQueries.size(); ++component) {
        state.emitterQueries[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                EmitterInstance instance{.entity = chunk.entities[row],
                                         .component = static_cast<std::uint32_t>(component)};
                std::memcpy(&instance.emitter,
                            chunk.columns[0] + (row * sizeof(particles::ParticleEmitter)),
                            sizeof(particles::ParticleEmitter));
                if (const physics2d::Pose2D* pose = kPoseOf(instance.entity)) {
                    instance.position = {pose->x, pose->y, 0};
                    if (pose->c != 0 || pose->s != 0) {
                        instance.way = {-pose->s, pose->c, 0};
                    }
                }
                state.emitters.push_back(instance);
            }
        });
    }
    const auto kRibbons = [&world, &kPoseOf](std::vector<world::ColumnQuery>& queries, auto& made) {
        using Instance = std::remove_cvref_t<decltype(made)>::value_type;
        made.clear();
        for (std::size_t component = 0; component < queries.size(); ++component) {
            queries[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    Instance instance{.entity = chunk.entities[row],
                                      .component = static_cast<std::uint32_t>(component)};
                    std::memcpy(
                        &instance.ribbon, chunk.columns[0] + (row * sizeof(instance.ribbon)), sizeof(instance.ribbon));
                    if (const physics2d::Pose2D* pose = kPoseOf(instance.entity)) {
                        instance.position = {pose->x, pose->y, 0};
                    }
                    made.push_back(instance);
                }
            });
        }
    };
    kRibbons(state.trailQueries, state.trails);
    kRibbons(state.beamQueries, state.beams);
}

const CanvasFrame& Canvas::queue(const CanvasCamera& camera) {
    return state_->queue(camera);
}

std::span<const SpriteInstance> Canvas::extracted() const noexcept {
    return state_->extracted;
}

result::Result<GameCanvas> loadGameCanvas(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    GameCanvas loaded;
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (world_kest::ofEngineType(component, "rawframe.canvas.Sprite")) {
            loaded.sprites.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kEmitterType)) {
            loaded.emitters.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kTrailType)) {
            loaded.trails.push_back(component.id);
        } else if (world_kest::ofEngineType(component, particles::kBeamType)) {
            loaded.beams.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.canvas.Camera")) {
            if (loaded.camera.has_value()) {
                return refuse(result::ErrorClass::InvalidArgument,
                              RenderCanvasError::BadComponents,
                              "a game has at most one camera component: a client has one view");
            }
            loaded.camera = component.id;
        }
    }
    if (loaded.sprites.empty()) {
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
                                {"layer", offsetof(Sprite, layer)},
                                {"frame", offsetof(Sprite, frame)},
                                {"columns", offsetof(Sprite, columns)},
                                {"material", offsetof(Sprite, material)}})) {
        return refuse(result::ErrorClass::InvalidArgument,
                      RenderCanvasError::BadComponents,
                      "the program lays out rawframe.canvas's Sprite otherwise than this engine reads it");
    }
    if (loaded.camera.has_value() && !world_kest::laidOutAs(program,
                                                            "rawframe.canvas.Camera",
                                                            sizeof(Camera),
                                                            {{"offsetX", offsetof(Camera, offsetX)},
                                                             {"offsetY", offsetof(Camera, offsetY)},
                                                             {"height", offsetof(Camera, height)}})) {
        return refuse(result::ErrorClass::InvalidArgument,
                      RenderCanvasError::BadComponents,
                      "the program lays out rawframe.canvas's Camera otherwise than this engine reads it");
    }
    // The particle triad as its own module reads it (D357).
    for (const auto& [kDeclared, kType, kSize, kFields] :
         {std::tuple{!loaded.emitters.empty(),
                     particles::kEmitterType,
                     sizeof(particles::ParticleEmitter),
                     particles::emitterFields()},
          std::tuple{!loaded.trails.empty(), particles::kTrailType, sizeof(particles::Trail), particles::trailFields()},
          std::tuple{!loaded.beams.empty(), particles::kBeamType, sizeof(particles::Beam), particles::beamFields()}}) {
        if (kDeclared && !world_kest::laidOutAs(program, kType, kSize, kFields)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderCanvasError::BadComponents,
                          "the program lays out a rawframe.model type otherwise than this engine reads it");
        }
    }
    for (const world_kest::GameTexture& texture : kDescription.textures) {
        loaded.textures.push_back(texture.id);
    }
    return loaded;
}

} // namespace rawframe::render_canvas
