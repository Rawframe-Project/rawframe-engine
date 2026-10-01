#include "rawframe/input_kest/sources.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace rawframe::input_kest {

namespace {

// The program's `rawframe.view` types, laid out as Kest lays them out.
struct Place {
    std::uint32_t failure = 0;
    float left = 0;
    float top = 0;
    float width = 0;
    float height = 0;
};

struct Ray {
    std::uint32_t failure = 0;
    double originX = 0;
    double originY = 0;
    double originZ = 0;
    float directionX = 0;
    float directionY = 0;
    float directionZ = 0;
};

struct Point {
    std::uint32_t failure = 0;
    float x = 0;
    float y = 0;
    float depth = 0;
};

struct Point2D {
    std::uint32_t failure = 0;
    double x = 0;
    double y = 0;
};

// view.kest's failure numbers: nought for none, then no view of the kind,
// then the view's own closed set in its order.
constexpr std::uint32_t kNoView = 1;

std::uint32_t numberOf(view::Failure failure) noexcept {
    return static_cast<std::uint32_t>(failure) + 2;
}

constexpr kest::Parameter kFloat{kest::Slot::F32};
constexpr kest::Parameter kReal{kest::Slot::F64};
constexpr std::array<kest::Parameter, 2> kPointTakes = {kFloat, kFloat};
constexpr std::array<kest::Parameter, 3> kWorldTakes = {kReal, kReal, kReal};
constexpr std::array<kest::Parameter, 2> kWorld2DTakes = {kReal, kReal};
constexpr std::array<kest::Parameter, 1> kPlaceGives = {kest::Parameter{kest::Slot::Value, "rawframe.view.Place"}};
constexpr std::array<kest::Parameter, 1> kRayGives = {kest::Parameter{kest::Slot::Value, "rawframe.view.Ray"}};
constexpr std::array<kest::Parameter, 1> kPointGives = {kest::Parameter{kest::Slot::Value, "rawframe.view.Point"}};
constexpr std::array<kest::Parameter, 1> kPoint2DGives = {kest::Parameter{kest::Slot::Value, "rawframe.view.Point2D"}};

template <typename Answer> void answer(kest::DoorCall& call, const Answer& value) noexcept {
    if (!call.answerValue(std::as_bytes(std::span{&value, 1}))) {
        call.fail("the program's view type is not the engine's");
    }
}

template <typename Camera> std::optional<view::Placed<Camera>> placedOf(const ViewDoorContext& context) noexcept {
    if (context.views == nullptr) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<Camera, view::Perspective>) {
        return context.views->perspective(context.player);
    } else {
        return context.views->orthographic(context.player);
    }
}

template <typename Camera> void placeDoor(kest::DoorCall& call, void* context) noexcept {
    const auto kPlaced = placedOf<Camera>(*static_cast<const ViewDoorContext*>(context));
    answer(call,
           kPlaced.has_value() ? Place{.left = kPlaced->left,
                                       .top = kPlaced->top,
                                       .width = kPlaced->size.width,
                                       .height = kPlaced->size.height}
                               : Place{.failure = kNoView});
}

void pointToRayDoor(kest::DoorCall& call, void* context) noexcept {
    const auto kPlaced = placedOf<view::Perspective>(*static_cast<const ViewDoorContext*>(context));
    if (!kPlaced.has_value()) {
        answer(call, Ray{.failure = kNoView});
        return;
    }
    const auto kRay = view::pointToRay(
        kPlaced->camera, kPlaced->size, static_cast<float>(call.real(0)), static_cast<float>(call.real(1)));
    if (!kRay.has_value()) {
        answer(call, Ray{.failure = numberOf(kRay.error())});
        return;
    }
    answer(call,
           Ray{.originX = kRay->origin[0],
               .originY = kRay->origin[1],
               .originZ = kRay->origin[2],
               .directionX = kRay->direction[0],
               .directionY = kRay->direction[1],
               .directionZ = kRay->direction[2]});
}

void worldToPointDoor(kest::DoorCall& call, void* context) noexcept {
    const auto kPlaced = placedOf<view::Perspective>(*static_cast<const ViewDoorContext*>(context));
    if (!kPlaced.has_value()) {
        answer(call, Point{.failure = kNoView});
        return;
    }
    const auto kPoint = view::worldToPoint(kPlaced->camera, kPlaced->size, {call.real(0), call.real(1), call.real(2)});
    answer(call,
           kPoint.has_value() ? Point{.x = kPoint->x, .y = kPoint->y, .depth = kPoint->depth}
                              : Point{.failure = numberOf(kPoint.error())});
}

void pointToWorld2DDoor(kest::DoorCall& call, void* context) noexcept {
    const auto kPlaced = placedOf<view::Orthographic>(*static_cast<const ViewDoorContext*>(context));
    if (!kPlaced.has_value()) {
        answer(call, Point2D{.failure = kNoView});
        return;
    }
    const auto kWorld = view::pointToWorld(
        kPlaced->camera, kPlaced->size, static_cast<float>(call.real(0)), static_cast<float>(call.real(1)));
    answer(call,
           kWorld.has_value() ? Point2D{.x = (*kWorld)[0], .y = (*kWorld)[1]}
                              : Point2D{.failure = numberOf(kWorld.error())});
}

void world2DToPointDoor(kest::DoorCall& call, void* context) noexcept {
    const auto kPlaced = placedOf<view::Orthographic>(*static_cast<const ViewDoorContext*>(context));
    if (!kPlaced.has_value()) {
        answer(call, Point{.failure = kNoView});
        return;
    }
    const auto kPoint = view::worldToPoint(kPlaced->camera, kPlaced->size, {call.real(0), call.real(1)});
    answer(call,
           kPoint.has_value() ? Point{.x = kPoint->x, .y = kPoint->y, .depth = kPoint->depth}
                              : Point{.failure = numberOf(kPoint.error())});
}

} // namespace

result::Status addViewDoors(kest::DoorTable& doors, const ViewDoorContext* context) {
    void* const kContext = const_cast<ViewDoorContext*>(context);
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.scenePlace",
                                      .function = &placeDoor<view::Perspective>,
                                      .context = kContext,
                                      .gives = kPlaceGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.canvasPlace",
                                      .function = &placeDoor<view::Orthographic>,
                                      .context = kContext,
                                      .gives = kPlaceGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.pointToRay",
                                      .function = &pointToRayDoor,
                                      .context = kContext,
                                      .takes = kPointTakes,
                                      .gives = kRayGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.worldToPoint",
                                      .function = &worldToPointDoor,
                                      .context = kContext,
                                      .takes = kWorldTakes,
                                      .gives = kPointGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.pointToWorld2D",
                                      .function = &pointToWorld2DDoor,
                                      .context = kContext,
                                      .takes = kPointTakes,
                                      .gives = kPoint2DGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "View.world2DToPoint",
                                      .function = &world2DToPointDoor,
                                      .context = kContext,
                                      .takes = kWorld2DTakes,
                                      .gives = kPointGives,
                                      .safeForUntrusted = true}));
    return {};
}

} // namespace rawframe::input_kest
