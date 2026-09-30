#include "rawframe/render_scene/scene.h"

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/errors.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <tuple>

namespace rawframe::render_scene {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderSceneError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderSceneDomain, code(error), why).error()};
}

using Vector = std::array<float, 3>;

Vector cross(const Vector& a, const Vector& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

Vector normalized(const Vector& a) noexcept {
    const float kLength = std::sqrt((a[0] * a[0]) + (a[1] * a[1]) + (a[2] * a[2]));
    return kLength > 0 ? Vector{a[0] / kLength, a[1] / kLength, a[2] / kLength} : Vector{0, 1, 0};
}

/// An sRGB channel of 0xRRGGBBAA, `shift` bits up, in linear light.
float linearOf(std::uint32_t color, unsigned shift) noexcept {
    const float kEncoded = static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    return kEncoded <= 0.04045F ? kEncoded / 12.92F : std::pow((kEncoded + 0.055F) / 1.055F, 2.4F);
}

Vector colorOf(std::uint32_t color) noexcept {
    return {linearOf(color, 24), linearOf(color, 16), linearOf(color, 8)};
}

/// A clear sky's color, sRGB: the sky's default (D288).
constexpr std::uint32_t kClearSky = 0x8FB8EBFF;

/// A mesh with the sphere around it, in its own space.
struct Bounded {
    std::shared_ptr<const mesh::Mesh> mesh;
    Vector center{};
    float radius = 0;
};

Bounded bounded(std::shared_ptr<const mesh::Mesh> made) {
    Vector low = made->positions.front();
    Vector high = low;
    for (const mesh::Vector3& kPosition : made->positions) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], kPosition[axis]);
            high[axis] = std::max(high[axis], kPosition[axis]);
        }
    }
    const Vector kCenter = {(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2};
    float radius = 0;
    for (const mesh::Vector3& kPosition : made->positions) {
        const Vector kAway = {kPosition[0] - kCenter[0], kPosition[1] - kCenter[1], kPosition[2] - kCenter[2]};
        radius = std::max(radius, std::sqrt((kAway[0] * kAway[0]) + (kAway[1] * kAway[1]) + (kAway[2] * kAway[2])));
    }
    return Bounded{.mesh = std::move(made), .center = kCenter, .radius = radius};
}

/// The columns of a unit quaternion's turn.
std::array<Vector, 3> turnOf(const std::array<float, 4>& q) noexcept {
    const float kX = q[0];
    const float kY = q[1];
    const float kZ = q[2];
    const float kW = q[3];
    return {{{1 - (2 * ((kY * kY) + (kZ * kZ))), 2 * ((kX * kY) + (kZ * kW)), 2 * ((kX * kZ) - (kY * kW))},
             {2 * ((kX * kY) - (kZ * kW)), 1 - (2 * ((kX * kX) + (kZ * kZ))), 2 * ((kY * kZ) + (kX * kW))},
             {2 * ((kX * kZ) + (kY * kW)), 2 * ((kY * kZ) - (kX * kW)), 1 - (2 * ((kX * kX) + (kY * kY)))}}};
}

/// Whether an instance's values are all ones it can be drawn with.
bool wellFormed(const ModelInstance& instance) noexcept {
    const Model& kModel = instance.model;
    return std::isfinite(kModel.scaleX) && std::isfinite(kModel.scaleY) && std::isfinite(kModel.scaleZ) &&
           std::ranges::all_of(instance.position,
                               [](double value) {
                                   return std::isfinite(value);
                               }) &&
           std::ranges::all_of(instance.rotation, [](float value) {
               return std::isfinite(value);
           });
}

/// The eye's axes: right, up, and forward, from its yaw and pitch, pitch
/// kept short of straight up or down.
std::array<Vector, 3> axesOf(float yaw, float pitch) noexcept {
    constexpr float kSteepest = (std::numbers::pi_v<float> / 2) - 0.001F;
    const float kPitch = std::clamp(pitch, -kSteepest, kSteepest);
    const Vector kForward = {-std::sin(yaw) * std::cos(kPitch), std::sin(kPitch), -std::cos(yaw) * std::cos(kPitch)};
    const Vector kRight = normalized(cross(kForward, {0, 1, 0}));
    return {kRight, cross(kRight, kForward), kForward};
}

} // namespace

struct Scene::State {
    SceneSettings settings;
    std::vector<world::ColumnQuery> models;
    std::optional<world::ColumnQuery> sun;
    std::optional<world::ColumnQuery> sky;
    std::optional<schema::ComponentRuntimeId> pose;
    std::map<std::uint64_t, Bounded> meshes;
    std::vector<ModelInstance> extracted;
    std::optional<Sun> sunNow;
    std::optional<Sky> skyNow;
    std::vector<const ModelInstance*> order;
    SceneFrame frame;

    void lights() {
        const Sun kSun = sunNow.value_or(Sun{
            .directionX = -0.3F, .directionY = -1.0F, .directionZ = -0.4F, .illuminance = 100000, .color = 0xFFFFFFFF});
        // A clear day's sky: its light, and its blue.
        const Sky kSky = skyNow.value_or(Sky{.luminance = 5000, .color = kClearSky});
        const Vector kToSun = normalized({-kSun.directionX, -kSun.directionY, -kSun.directionZ});
        const Vector kSunColor = colorOf(kSun.color);
        const Vector kSkyColor = colorOf(kSky.color);
        const float kIlluminance = std::isfinite(kSun.illuminance) ? std::max(kSun.illuminance, 0.0F) : 0.0F;
        const float kLuminance = std::isfinite(kSky.luminance) ? std::max(kSky.luminance, 0.0F) : 0.0F;
        frame.lights =
            SceneLights{.toSun = kToSun,
                        .sun = {kSunColor[0] * kIlluminance, kSunColor[1] * kIlluminance, kSunColor[2] * kIlluminance},
                        .sky = {kSkyColor[0] * kLuminance, kSkyColor[1] * kLuminance, kSkyColor[2] * kLuminance}};
    }

    const SceneFrame& queue(const SceneCamera& camera) {
        frame.draws.clear();
        frame.drawn = 0;
        frame.culled = 0;
        frame.hidden = 0;
        frame.malformed = 0;
        frame.unknownMeshes = 0;
        frame.overLimit = 0;
        lights();
        order.clear();
        for (const ModelInstance& instance : extracted) {
            order.push_back(&instance);
        }
        std::ranges::sort(order, [](const ModelInstance* left, const ModelInstance* right) {
            return std::tuple{left->model.mesh, left->entity, left->component} <
                   std::tuple{right->model.mesh, right->entity, right->component};
        });
        // A camera that sees nothing culls everything.
        const bool kSees = std::ranges::all_of(camera.eye,
                                               [](double value) {
                                                   return std::isfinite(value);
                                               }) &&
                           std::isfinite(camera.yaw) && std::isfinite(camera.pitch) && std::isfinite(camera.fovY) &&
                           std::isfinite(camera.near) && std::isfinite(camera.aspect) && camera.fovY > 0 &&
                           camera.fovY < std::numbers::pi_v<float> && camera.near > 0 && camera.aspect > 0;
        const auto [kRight, kUp, kForward] = axesOf(kSees ? camera.yaw : 0, kSees ? camera.pitch : 0);
        frame.view = Matrix{kRight[0],
                            kUp[0],
                            -kForward[0],
                            0,
                            kRight[1],
                            kUp[1],
                            -kForward[1],
                            0,
                            kRight[2],
                            kUp[2],
                            -kForward[2],
                            0,
                            0,
                            0,
                            0,
                            1};
        const float kHalf = kSees ? camera.fovY / 2 : 0.5F;
        const float kFocal = 1 / std::tan(kHalf);
        const float kAspect = kSees ? camera.aspect : 1.0F;
        const float kNear = kSees ? camera.near : 0.1F;
        // Reversed-Z with the far plane at infinity (ADR-0051): clip z is
        // the near distance and w the distance ahead, so depth is one at the
        // near plane and falls toward nought.
        frame.projection = Matrix{kFocal / kAspect, 0, 0, 0, 0, kFocal, 0, 0, 0, 0, 0, -1, 0, 0, kNear, 0};
        frame.exposure = std::isfinite(camera.exposure) ? camera.exposure : 15.0F;
        // The view's side planes in the eye's axes, each normal pointing
        // out: what lies past one by more than its radius is out of view.
        const float kWide = std::atan(std::tan(kHalf) * kAspect);
        const std::array<Vector, 4> kPlanes = {{{0, std::cos(kHalf), std::sin(kHalf)},
                                                {0, -std::cos(kHalf), std::sin(kHalf)},
                                                {std::cos(kWide), 0, std::sin(kWide)},
                                                {-std::cos(kWide), 0, std::sin(kWide)}}};
        bool full = false;
        for (const ModelInstance* instance : order) {
            const Model& kModel = instance->model;
            if (!wellFormed(*instance)) {
                ++frame.malformed;
                continue;
            }
            if (kModel.mesh == 0 || (kModel.color & 0xFFU) == 0 || kModel.scaleX == 0 || kModel.scaleY == 0 ||
                kModel.scaleZ == 0) {
                ++frame.hidden;
                continue;
            }
            const auto kMesh = meshes.find(kModel.mesh);
            if (kMesh == meshes.end()) {
                ++frame.unknownMeshes;
                continue;
            }
            // Relative to the eye in doubles, then floats: far from the
            // origin, a float would lose the model's place before it is small.
            const Vector kPlace = {static_cast<float>(instance->position[0] - camera.eye[0]),
                                   static_cast<float>(instance->position[1] - camera.eye[1]),
                                   static_cast<float>(instance->position[2] - camera.eye[2])};
            const bool kTurned = std::ranges::any_of(instance->rotation, [](float value) {
                return value != 0;
            });
            const std::array<Vector, 3> kTurn = turnOf(kTurned ? instance->rotation : std::array<float, 4>{0, 0, 0, 1});
            const Vector kScale = {kModel.scaleX, kModel.scaleY, kModel.scaleZ};
            const Bounded& kBounds = kMesh->second;
            Vector center = kPlace;
            for (std::size_t column = 0; column < 3; ++column) {
                for (std::size_t row = 0; row < 3; ++row) {
                    center[row] += kTurn[column][row] * kScale[column] * kBounds.center[column];
                }
            }
            const float kRadius =
                kBounds.radius * std::max({std::abs(kScale[0]), std::abs(kScale[1]), std::abs(kScale[2])});
            const Vector kSeen = {(kRight[0] * center[0]) + (kRight[1] * center[1]) + (kRight[2] * center[2]),
                                  (kUp[0] * center[0]) + (kUp[1] * center[1]) + (kUp[2] * center[2]),
                                  -((kForward[0] * center[0]) + (kForward[1] * center[1]) + (kForward[2] * center[2]))};
            bool outside = !kSees || kSeen[2] - kRadius > -kNear;
            for (const Vector& kPlane : kPlanes) {
                outside = outside || (kPlane[0] * kSeen[0]) + (kPlane[1] * kSeen[1]) + (kPlane[2] * kSeen[2]) > kRadius;
            }
            if (outside) {
                ++frame.culled;
                continue;
            }
            // Past the limit, this model and every one after it are left
            // out, so what is drawn is a prefix of the order.
            full = full || frame.drawn == settings.limits.maximumModels;
            if (full) {
                ++frame.overLimit;
                continue;
            }
            SceneDraw draw{.mesh = kModel.mesh, .entity = instance->entity};
            for (std::size_t column = 0; column < 3; ++column) {
                for (std::size_t row = 0; row < 3; ++row) {
                    draw.model[(column * 4) + row] = kTurn[column][row] * kScale[column];
                    draw.normal[(column * 4) + row] = kTurn[column][row] / kScale[column];
                }
            }
            draw.model[12] = kPlace[0];
            draw.model[13] = kPlace[1];
            draw.model[14] = kPlace[2];
            draw.model[15] = 1;
            const Vector kColor = colorOf(kModel.color);
            draw.color = {kColor[0], kColor[1], kColor[2], static_cast<float>(kModel.color & 0xFFU) / 255.0F};
            frame.draws.push_back(draw);
            ++frame.drawn;
        }
        return frame;
    }
};

Scene::Scene(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Scene::~Scene() = default;

result::Result<std::unique_ptr<Scene>> Scene::create(const schema::SchemaRegistry& registry, SceneSettings settings) {
    auto state = std::make_unique<State>();
    const auto kQueryOf = [&registry](schema::ComponentTypeId id,
                                      std::size_t size) -> result::Result<world::ColumnQuery> {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kComponent, registry.find(id));
        if (registry.descriptor(kComponent).size != size) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "a rawframe.model component is not that module's size");
        }
        const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{kComponent, world::Access::Read}};
        return world::ColumnQuery::resolve(kTerms, registry);
    };
    for (const schema::ComponentTypeId kId : settings.models) {
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, kQueryOf(kId, sizeof(Model)));
        state->models.push_back(std::move(query));
    }
    if (settings.sun.has_value()) {
        RAWFRAME_TRY_ASSIGN(state->sun, kQueryOf(*settings.sun, sizeof(Sun)));
    }
    if (settings.sky.has_value()) {
        RAWFRAME_TRY_ASSIGN(state->sky, kQueryOf(*settings.sky, sizeof(Sky)));
    }
    for (const std::uint64_t kId : {kBox, kSphere, kCylinder, kCapsule}) {
        state->meshes.emplace(kId, bounded(engineMesh(kId)));
    }
    // A game's mesh of an engine mesh's identity is the game's.
    for (const SceneMesh& kMesh : settings.meshes) {
        if (kMesh.mesh != nullptr && !kMesh.mesh->positions.empty()) {
            state->meshes.insert_or_assign(kMesh.id, bounded(kMesh.mesh));
        }
    }
    state->settings = std::move(settings);
    if (const auto kPose = registry.find(physics3d::Pose3D::kComponentTypeId)) {
        state->pose = *kPose;
    }
    return std::unique_ptr<Scene>{new Scene{std::move(state)}};
}

void Scene::extract(world::World& world) {
    State& state = *state_;
    state.extracted.clear();
    for (std::size_t component = 0; component < state.models.size(); ++component) {
        state.models[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                ModelInstance instance{.entity = chunk.entities[row],
                                       .component = static_cast<std::uint32_t>(component)};
                std::memcpy(&instance.model, chunk.columns[0] + (row * sizeof(Model)), sizeof(Model));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                state.extracted.push_back(instance);
            }
        });
    }
    // The first set, if any: a game declares at most one of each.
    const auto kFirstOf = [&world]<typename T>(std::optional<world::ColumnQuery>& query, std::optional<T>& into) {
        into.reset();
        if (!query.has_value()) {
            return;
        }
        query->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            if (!into.has_value() && !chunk.entities.empty()) {
                T value;
                std::memcpy(&value, chunk.columns[0], sizeof(T));
                into = value;
            }
        });
    };
    kFirstOf(state.sun, state.sunNow);
    kFirstOf(state.sky, state.skyNow);
}

const SceneFrame& Scene::queue(const SceneCamera& camera) {
    return state_->queue(camera);
}

std::span<const ModelInstance> Scene::extracted() const noexcept {
    return state_->extracted;
}

std::shared_ptr<const mesh::Mesh> Scene::mesh(std::uint64_t id) const {
    const auto kFound = state_->meshes.find(id);
    return kFound != state_->meshes.end() ? kFound->second.mesh : nullptr;
}

result::Result<GameScene> loadGameScene(const world_kest::GameFiles& game, const kest::Program& program) {
    const world_kest::GameDescription& kDescription = game.description();
    GameScene loaded;
    const auto kOne = [](std::optional<schema::ComponentTypeId>& slot,
                         schema::ComponentTypeId id,
                         std::string_view why) -> result::Status {
        if (slot.has_value()) {
            return refuse(result::ErrorClass::InvalidArgument, RenderSceneError::BadComponents, why);
        }
        slot = id;
        return {};
    };
    for (const world_kest::GameComponent& component : kDescription.components) {
        if (world_kest::ofEngineType(component, "rawframe.model.Model")) {
            loaded.models.push_back(component.id);
        } else if (world_kest::ofEngineType(component, "rawframe.model.Camera")) {
            RAWFRAME_TRY(kOne(loaded.camera, component.id, "a game has at most one 3D camera: a client has one view"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sun")) {
            RAWFRAME_TRY(kOne(loaded.sun, component.id, "a game has at most one sun"));
        } else if (world_kest::ofEngineType(component, "rawframe.model.Sky")) {
            RAWFRAME_TRY(kOne(loaded.sky, component.id, "a game has at most one sky"));
        }
    }
    if (loaded.models.empty()) {
        return refuse(result::ErrorClass::NotFound, RenderSceneError::NoModels, "the game declares no models");
    }
    // By their full names: a game's own type called `Model` is not this one.
    const auto kLaidOut =
        [&program](bool declared,
                   std::string_view type,
                   std::size_t size,
                   std::initializer_list<std::pair<std::string_view, std::size_t>> fields) -> result::Status {
        if (declared && !world_kest::laidOutAs(program, type, size, fields)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          RenderSceneError::BadComponents,
                          "the program lays out a rawframe.model type otherwise than this engine reads it");
        }
        return {};
    };
    RAWFRAME_TRY(kLaidOut(true,
                          "rawframe.model.Model",
                          sizeof(Model),
                          {{"mesh", offsetof(Model, mesh)},
                           {"scaleX", offsetof(Model, scaleX)},
                           {"scaleY", offsetof(Model, scaleY)},
                           {"scaleZ", offsetof(Model, scaleZ)},
                           {"color", offsetof(Model, color)}}));
    RAWFRAME_TRY(kLaidOut(loaded.camera.has_value(),
                          "rawframe.model.Camera",
                          sizeof(Camera),
                          {{"offsetX", offsetof(Camera, offsetX)},
                           {"offsetY", offsetof(Camera, offsetY)},
                           {"offsetZ", offsetof(Camera, offsetZ)},
                           {"yaw", offsetof(Camera, yaw)},
                           {"pitch", offsetof(Camera, pitch)},
                           {"fovY", offsetof(Camera, fovY)},
                           {"near", offsetof(Camera, near)},
                           {"exposure", offsetof(Camera, exposure)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sun.has_value(),
                          "rawframe.model.Sun",
                          sizeof(Sun),
                          {{"directionX", offsetof(Sun, directionX)},
                           {"directionY", offsetof(Sun, directionY)},
                           {"directionZ", offsetof(Sun, directionZ)},
                           {"illuminance", offsetof(Sun, illuminance)},
                           {"color", offsetof(Sun, color)}}));
    RAWFRAME_TRY(kLaidOut(loaded.sky.has_value(),
                          "rawframe.model.Sky",
                          sizeof(Sky),
                          {{"luminance", offsetof(Sky, luminance)}, {"color", offsetof(Sky, color)}}));
    for (const physics3d::BodyMesh& kMesh : game.meshes()) {
        loaded.meshes.push_back(SceneMesh{.id = kMesh.id, .mesh = kMesh.mesh});
    }
    return loaded;
}

} // namespace rawframe::render_scene
