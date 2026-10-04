#include "navigation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>

namespace rawframe::world_kest {

namespace {

constexpr diagnostics::EventIdentity kNavigationSummary{"world_kest", "navigation_summary"};

/// A tile's side, in cells.
constexpr std::int32_t kTileCells = 64;

/// The way's next step, as `rawframe.navigation.Step` lays it out.
struct Step {
    double x = 0;
    double y = 0;
    double z = 0;
    float remaining = 0;
    bool found = false;
};

constexpr kest::Parameter kReal{kest::Slot::F64};
constexpr std::array<kest::Parameter, 6> kNextTakes = {kReal, kReal, kReal, kReal, kReal, kReal};
constexpr std::array<kest::Parameter, 1> kNextGives = {kest::Parameter{kest::Slot::Value, "rawframe.navigation.Step"}};

/// `Navigation.next(fromX, fromY, fromZ, toX, toY, toZ)`.
void nextDoor(kest::DoorCall& call, void* context) noexcept {
    auto* const kOwner = static_cast<GameNavmesh*>(context);
    const navigation::Point kFrom{call.real(0), call.real(1), call.real(2)};
    const navigation::Point kTo{call.real(3), call.real(4), call.real(5)};
    Step step{.x = kFrom[0], .y = kFrom[1], .z = kFrom[2]};
    navigation::Navmesh* const kNavmesh = kOwner != nullptr ? kOwner->navmesh() : nullptr;
    if (kNavmesh != nullptr) {
        const auto kPath = kNavmesh->path(kFrom, kTo);
        if (kPath.has_value() && !kPath->points.empty()) {
            const navigation::Point& kNext = kPath->points.size() > 1 ? kPath->points[1] : kPath->points[0];
            step = Step{.x = kNext[0],
                        .y = kNext[1],
                        .z = kNext[2],
                        .remaining = static_cast<float>(kPath->length),
                        .found = kPath->end == navigation::PathEnd::Reached};
        }
    }
    if (!call.answerValue(std::as_bytes(std::span{&step, 1}))) {
        call.fail("the program's Step is not the engine's");
    }
}

/// The bake as a system of the World.
class BakeSystem final : public world::System {
public:
    explicit BakeSystem(GameNavmesh& owner) noexcept : owner_(&owner) {
    }
    result::Status run(world::SystemContext& /*context*/) noexcept override {
        owner_->bake();
        return {};
    }

private:
    GameNavmesh* owner_;
};

constexpr std::array<std::string_view, 1> kAfter = {physics3d::kStepSystem};

} // namespace

bool readNavigationLine(std::span<const std::string_view> words, GameNavigation& into) {
    for (std::size_t at = 0; at < words.size(); at += 2) {
        if (at + 1 >= words.size()) {
            return false;
        }
        const std::string_view kWhat = words[at];
        double* const kInto = kWhat == "radius"   ? &into.radius
                              : kWhat == "height" ? &into.height
                              : kWhat == "step"   ? &into.step
                              : kWhat == "slope"  ? &into.slope
                              : kWhat == "cell"   ? &into.cell
                                                  : nullptr;
        const std::string_view kValue = words[at + 1];
        double value = 0;
        const auto kRead = std::from_chars(kValue.data(), kValue.data() + kValue.size(), value);
        if (kInto == nullptr || kRead.ec != std::errc{} || kRead.ptr != kValue.data() + kValue.size() ||
            !std::isfinite(value) || !(value > 0)) {
            return false;
        }
        *kInto = value;
    }
    return true;
}

GameNavmesh::GameNavmesh(const GameNavigation& navigation) noexcept
    : navigation_(navigation), system_(std::make_unique<BakeSystem>(*this)) {
}

GameNavmesh::~GameNavmesh() = default;

void GameNavmesh::attach(const physics3d::Physics3DQueries* queries) noexcept {
    queries_ = queries;
}

result::Status GameNavmesh::declareSystems(const schema::SchemaRegistry& /*registry*/,
                                           std::vector<world::SystemDeclaration>& systems) noexcept {
    systems.push_back(world::SystemDeclaration{.identity = kNavigationBakeSystem,
                                               .phase = world::Phase::Simulation,
                                               .after = kAfter,
                                               .system = system_.get()});
    return {};
}

void GameNavmesh::bake() {
    if (queries_ == nullptr) {
        return;
    }
    const std::uint64_t kRevision = queries_->staticRevision();
    if (baked_ && kRevision == revision_) {
        return;
    }
    revision_ = kRevision;
    baked_ = true;
    queries_->staticGeometry(geometry_);
    std::map<world::EntityHandle, Box> boxes;
    for (const physics3d::StaticShape& kShape : geometry_.shapes) {
        boxes.emplace(kShape.entity, Box{kShape.low, kShape.high});
    }
    // The ground every change covers, before it and after.
    std::optional<Box> changed;
    const auto kGrow = [&changed](const Box& box) {
        if (!changed.has_value()) {
            changed = box;
            return;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            (*changed)[0][axis] = std::min((*changed)[0][axis], box[0][axis]);
            (*changed)[1][axis] = std::max((*changed)[1][axis], box[1][axis]);
        }
    };
    for (const auto& [kEntity, kBox] : boxes) {
        const auto kBefore = boxes_.find(kEntity);
        if (kBefore == boxes_.end() || kBefore->second != kBox) {
            kGrow(kBox);
            if (kBefore != boxes_.end()) {
                kGrow(kBefore->second);
            }
        }
    }
    for (const auto& [kEntity, kBox] : boxes_) {
        if (!boxes.contains(kEntity)) {
            kGrow(kBox);
        }
    }
    boxes_ = std::move(boxes);
    if (!changed.has_value()) {
        return;
    }
    if (navmesh_ == nullptr) {
        // The tile grid starts a tile short of the first geometry's low
        // corner, and stays there.
        const double kTile = navigation_.cell * kTileCells;
        auto made = navigation::Navmesh::create(
            navigation::Settings{.origin = {(*changed)[0][0] - kTile, (*changed)[0][1], (*changed)[0][2] - kTile},
                                 .cellSize = navigation_.cell,
                                 .cellHeight = navigation_.cell / 2,
                                 .tileCells = kTileCells,
                                 .agent = {.radius = navigation_.radius,
                                           .height = navigation_.height,
                                           .step = navigation_.step,
                                           .maxSlope = navigation_.slope}});
        if (!made.has_value()) {
            ++refused_;
            return;
        }
        navmesh_ = std::move(*made);
    }
    const std::array<navigation::Mesh, 1> kInput = {
        navigation::Mesh{.vertices = geometry_.vertices, .indices = geometry_.indices}};
    if (!navmesh_
             ->bake(kInput,
                    navigation::Area{.minX = (*changed)[0][0],
                                     .minZ = (*changed)[0][2],
                                     .maxX = (*changed)[1][0],
                                     .maxZ = (*changed)[1][2]})
             .has_value()) {
        ++refused_;
    }
}

navigation::Navmesh* GameNavmesh::navmesh() noexcept {
    return navmesh_.get();
}

GameNavmeshStatistics GameNavmesh::statistics() const noexcept {
    GameNavmeshStatistics statistics{.bakesRefused = refused_};
    if (navmesh_ != nullptr) {
        const navigation::NavmeshStatistics& kBaked = navmesh_->statistics();
        statistics.bakes = kBaked.bakes;
        statistics.tilesBaked = kBaked.tilesBaked;
        statistics.tiles = kBaked.tiles;
        statistics.polygons = kBaked.polygons;
        statistics.searches = kBaked.searches;
        statistics.fingerprint = navmesh_->fingerprint();
    }
    return statistics;
}

void GameNavmesh::report(diagnostics::Emitter& emitter) const {
    const GameNavmeshStatistics kNavigation = statistics();
    std::array<char, 16> fingerprint{};
    for (std::size_t index = 0; index < fingerprint.size(); ++index) {
        fingerprint[index] = "0123456789abcdef"[(kNavigation.fingerprint >> (4U * (15U - index))) & 0xFU];
    }
    emitter.log(diagnostics::Severity::Info,
                kNavigationSummary,
                "what the navmesh held and was asked",
                {diagnostics::field("bakes", kNavigation.bakes),
                 diagnostics::field("bakesRefused", kNavigation.bakesRefused),
                 diagnostics::field("tilesBaked", kNavigation.tilesBaked),
                 diagnostics::field("tiles", static_cast<std::uint64_t>(kNavigation.tiles)),
                 diagnostics::field("polygons", kNavigation.polygons),
                 diagnostics::field("searches", kNavigation.searches),
                 diagnostics::field("fingerprint", std::string_view{fingerprint.data(), fingerprint.size()})});
}

result::Status addNavigationDoors(kest::DoorTable& doors, GameNavmesh* navmesh) {
    return doors.add(kest::Door{.name = "Navigation.next",
                                .function = &nextDoor,
                                .context = navmesh,
                                .takes = kNextTakes,
                                .gives = kNextGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
