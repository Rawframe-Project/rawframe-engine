#include "splines.h"

#include "rawframe/physics2d/components.h"
#include "rawframe/physics3d/components.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <tuple>

namespace rawframe::world_kest {

namespace {

/// `rawframe.spline.Along`, as the program lays it out.
struct Along {
    double x = 0;
    double y = 0;
    double z = 0;
    float headingX = 0;
    float headingY = 0;
    float headingZ = 0;
    double length = 0;
    bool found = false;
};

constexpr std::array<kest::Parameter, 2> kAlongTakes = {kest::Parameter{kest::Slot::Value, "rawframe.world.Entity"},
                                                        kest::Parameter{kest::Slot::F64}};
constexpr std::array<kest::Parameter, 1> kAlongGives = {kest::Parameter{kest::Slot::Value, "rawframe.spline.Along"}};

/// `Splines.along(spline, meters)`.
void alongDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kSplines = static_cast<const GameSplines*>(context);
    world::EntityHandle entity;
    if (!call.value(0, std::as_writable_bytes(std::span{&entity, 1}))) {
        call.fail("the program's Entity is not the engine's");
        return;
    }
    Along answer;
    const std::optional<spline::Along> kAt =
        kSplines != nullptr ? kSplines->along(entity, call.real(1)) : std::optional<spline::Along>{};
    if (kAt.has_value()) {
        answer = Along{.x = kAt->point[0],
                       .y = kAt->point[1],
                       .z = kAt->point[2],
                       .headingX = static_cast<float>(kAt->heading[0]),
                       .headingY = static_cast<float>(kAt->heading[1]),
                       .headingZ = static_cast<float>(kAt->heading[2]),
                       .length = kSplines->lengthOf(entity),
                       .found = true};
    }
    if (!call.answerValue(std::as_bytes(std::span{&answer, 1}))) {
        call.fail("the program's Along is not the engine's");
    }
}

/// The measuring as a system of the World.
class MeasureSystem final : public world::System {
public:
    explicit MeasureSystem(GameSplines& owner) noexcept : owner_(&owner) {
    }
    result::Status run(world::SystemContext& context) noexcept override {
        owner_->measure(context.world);
        return {};
    }

private:
    GameSplines* owner_;
};

} // namespace

GameSplines::GameSplines(GameSplineComponents components) noexcept : components_(std::move(components)) {
}

GameSplines::~GameSplines() = default;

result::Status GameSplines::declareSystems(const schema::SchemaRegistry& registry,
                                           std::vector<world::SystemDeclaration>& systems) noexcept {
    RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kPose,
                        registry.find(components_.dimensions == 3 ? physics3d::Pose3D::kComponentTypeId
                                                                  : physics2d::Pose2D::kComponentTypeId));
    pointQueries_.clear();
    splineIds_.clear();
    reads_ = {kPose};
    for (const schema::ComponentTypeId kId : components_.points) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kPoint, registry.find(kId));
        const std::array<world::ColumnTerm, 2> kTerms = {world::ColumnTerm{kPoint, world::Access::Read},
                                                         world::ColumnTerm{kPose, world::Access::Read}};
        RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, registry));
        pointQueries_.push_back(std::move(query));
        reads_.push_back(kPoint);
    }
    for (const schema::ComponentTypeId kId : components_.splines) {
        RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kSpline, registry.find(kId));
        splineIds_.push_back(kSpline);
        reads_.push_back(kSpline);
    }
    system_ = std::make_unique<MeasureSystem>(*this);
    systems.push_back(world::SystemDeclaration{.identity = kSplineMeasureSystem,
                                               .phase = world::Phase::PreSimulation,
                                               .reads = reads_,
                                               .system = system_.get()});
    return {};
}

void GameSplines::measure(world::World& world) {
    // Each spline's points, by their place among its points and then by
    // entity, so the same points give the same path.
    std::map<world::EntityHandle, std::vector<std::tuple<std::uint32_t, world::EntityHandle, spline::Point>>> found;
    const bool kThree = components_.dimensions == 3;
    for (world::ColumnQuery& query : pointQueries_) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                SplinePoint point;
                std::memcpy(&point, chunk.columns[0] + (row * sizeof(SplinePoint)), sizeof(SplinePoint));
                spline::Point at{};
                if (kThree) {
                    physics3d::Pose3D pose;
                    std::memcpy(&pose, chunk.columns[1] + (row * sizeof(physics3d::Pose3D)), sizeof(pose));
                    at = {pose.x, pose.y, pose.z};
                } else {
                    physics2d::Pose2D pose;
                    std::memcpy(&pose, chunk.columns[1] + (row * sizeof(physics2d::Pose2D)), sizeof(pose));
                    at = {pose.x, pose.y, 0};
                }
                found[point.spline].emplace_back(point.index, chunk.entities[row], at);
            }
        });
    }
    std::erase_if(paths_, [&found](const auto& path) {
        return !found.contains(path.first);
    });
    for (auto& [kSpline, points] : found) {
        std::ranges::sort(points, [](const auto& left, const auto& right) {
            return std::tie(std::get<0>(left), std::get<1>(left)) < std::tie(std::get<0>(right), std::get<1>(right));
        });
        spline::Spline made;
        for (const auto& [kIndex, kEntity, kAt] : points) {
            made.points.push_back(kAt);
        }
        for (const schema::ComponentRuntimeId kId : splineIds_) {
            if (const auto* closed = static_cast<const bool*>(world.getErased(kSpline, kId))) {
                made.closed = made.closed || *closed;
            }
        }
        Measured& held = paths_[kSpline];
        if (!held.walk.points.empty() && held.points.points == made.points && held.points.closed == made.closed) {
            continue;
        }
        held.walk = spline::walk(made);
        held.points = std::move(made);
        ++measured_;
    }
}

std::optional<spline::Along> GameSplines::along(world::EntityHandle spline, double meters) const {
    const auto kPath = paths_.find(spline);
    if (kPath == paths_.end() || spline::spans(kPath->second.points) == 0) {
        return std::nullopt;
    }
    return spline::along(kPath->second.walk, meters, kPath->second.points.closed);
}

double GameSplines::lengthOf(world::EntityHandle spline) const {
    const auto kPath = paths_.find(spline);
    return kPath == paths_.end() ? 0 : kPath->second.walk.length();
}

result::Status addSplineDoors(kest::DoorTable& doors, const GameSplines* splines) {
    return doors.add(kest::Door{.name = "Splines.along",
                                .function = &alongDoor,
                                .context = const_cast<GameSplines*>(splines),
                                .takes = kAlongTakes,
                                .gives = kAlongGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
