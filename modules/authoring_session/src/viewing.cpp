#include "viewing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <numbers>
#include <string_view>

namespace rawframe::authoring_session {

authoring::SceneView movedView(const authoring::SceneView& view, double detents, double across, double up) noexcept {
    constexpr double kPerPixel = 1.0 / 200;
    constexpr double kHighest = (std::numbers::pi / 2) - 0.05;
    const std::array<double, 3> kFrom = {
        view.eye[0] - view.target[0], view.eye[1] - view.target[1], view.eye[2] - view.target[2]};
    const double kDistance = std::hypot(kFrom[0], kFrom[1], kFrom[2]);
    if (!std::isfinite(kDistance) || kDistance <= 0) {
        return view;
    }
    const double kYaw = std::atan2(kFrom[0], kFrom[2]) - (across * kPerPixel);
    const double kPitch =
        std::clamp(std::asin(std::clamp(kFrom[1] / kDistance, -1.0, 1.0)) + (up * kPerPixel), -kHighest, kHighest);
    const double kFar = std::clamp(kDistance * std::pow(1 + (1.0 / 6), detents), 0.5, 1e4);
    authoring::SceneView moved = view;
    moved.eye = {view.target[0] + (kFar * std::cos(kPitch) * std::sin(kYaw)),
                 view.target[1] + (kFar * std::sin(kPitch)),
                 view.target[2] + (kFar * std::cos(kPitch) * std::cos(kYaw))};
    return moved;
}

authoring::SceneView framedView(const authoring::SceneView& view,
                                std::span<const std::array<double, 3>> points) noexcept {
    if (points.empty()) {
        return view;
    }
    std::array<double, 3> middle{};
    for (const auto& kPoint : points) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            middle[axis] += kPoint[axis] / static_cast<double>(points.size());
        }
    }
    double spread = 0;
    for (const auto& kPoint : points) {
        spread = std::max(spread, std::hypot(kPoint[0] - middle[0], kPoint[1] - middle[1], kPoint[2] - middle[2]));
    }
    std::array<double, 3> back = {
        view.eye[0] - view.target[0], view.eye[1] - view.target[1], view.eye[2] - view.target[2]};
    double length = std::hypot(back[0], back[1], back[2]);
    if (!std::isfinite(length) || length <= 0) {
        back = {0, 1, 1};
        length = std::numbers::sqrt2;
    }
    const double kHalf = std::clamp(view.fieldOfView, 1.0, 179.0) * std::numbers::pi / 360;
    const double kFar = std::clamp((spread + 1) / std::sin(kHalf), 3.0, 1e4);
    authoring::SceneView framed = view;
    framed.target = middle;
    framed.eye = {middle[0] + (back[0] / length * kFar),
                  middle[1] + (back[1] / length * kFar),
                  middle[2] + (back[2] / length * kFar)};
    return framed;
}

std::optional<std::array<double, 3>> standingOf(const scene::Scene& scene, base::Bits128 entity) {
    // By name: the session knows no physics module's types.
    constexpr std::string_view kPoses[] = {"rawframe.physics3d.pose", "rawframe.physics2d.pose"};
    const auto kAt = [](std::span<const scene::SceneField> fields) {
        std::array<double, 3> at{};
        for (const scene::SceneField& kField : fields) {
            const std::size_t kAxis = kField.name == "x" ? 0 : kField.name == "y" ? 1 : kField.name == "z" ? 2 : 3;
            if (kAxis < 3 && kField.value.kind == scene::FieldValue::Kind::Number) {
                at[kAxis] = std::strtod(kField.value.number.c_str(), nullptr);
            }
        }
        return at;
    };
    for (const scene::SceneEntity& kEach : scene.entities) {
        if (kEach.id != entity) {
            continue;
        }
        for (const scene::SceneComponent& kComponent : kEach.components) {
            if (std::ranges::contains(kPoses, kComponent.name)) {
                return kAt(kComponent.fields);
            }
        }
        return std::nullopt;
    }
    for (const scene::SceneInstance& kInstance : scene.instances) {
        for (const scene::Override& kOverride : kInstance.overrides) {
            if (kOverride.entity == entity && kOverride.kind != scene::Override::Kind::Remove &&
                std::ranges::contains(kPoses, kOverride.component)) {
                return kAt(kOverride.fields);
            }
        }
    }
    return std::nullopt;
}

authoring::SceneView lookedView(const authoring::SceneView& view, double across, double up) noexcept {
    constexpr double kPerPixel = 1.0 / 200;
    constexpr double kHighest = (std::numbers::pi / 2) - 0.05;
    const std::array<double, 3> kAhead = {
        view.target[0] - view.eye[0], view.target[1] - view.eye[1], view.target[2] - view.eye[2]};
    const double kDistance = std::hypot(kAhead[0], kAhead[1], kAhead[2]);
    if (!std::isfinite(kDistance) || kDistance <= 0) {
        return view;
    }
    const double kYaw = std::atan2(kAhead[0], kAhead[2]) - (across * kPerPixel);
    const double kPitch =
        std::clamp(std::asin(std::clamp(kAhead[1] / kDistance, -1.0, 1.0)) - (up * kPerPixel), -kHighest, kHighest);
    authoring::SceneView looked = view;
    looked.target = {view.eye[0] + (kDistance * std::cos(kPitch) * std::sin(kYaw)),
                     view.eye[1] + (kDistance * std::sin(kPitch)),
                     view.eye[2] + (kDistance * std::cos(kPitch) * std::cos(kYaw))};
    return looked;
}

authoring::SceneView flownView(const authoring::SceneView& view, double detents) noexcept {
    const std::array<double, 3> kAhead = {
        view.target[0] - view.eye[0], view.target[1] - view.eye[1], view.target[2] - view.eye[2]};
    // A sixth of the distance per detent, back toward the author.
    const double kBy = -detents / 6;
    if (!std::isfinite(kBy)) {
        return view;
    }
    authoring::SceneView flown = view;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        flown.eye[axis] += kAhead[axis] * kBy;
        flown.target[axis] += kAhead[axis] * kBy;
    }
    return flown;
}

namespace {

std::array<double, 2> pairOf(const document::Value* value) {
    if (value == nullptr || value->kind() != document::Value::Kind::Array || value->items().size() != 2) {
        return {};
    }
    return {value->items()[0].real().value_or(0), value->items()[1].real().value_or(0)};
}

double numberOf(const document::Value* value) {
    return value != nullptr ? value->real().value_or(0) : 0;
}

} // namespace

ViewMotion motionOf(const document::Value& clicked) {
    return ViewMotion{.wheel = numberOf(clicked.find("wheel")),
                      .orbit = pairOf(clicked.find("orbit")),
                      .fly = numberOf(clicked.find("fly")),
                      .look = pairOf(clicked.find("look"))};
}

ViewMotion motionSince(const ViewMotion& now, const ViewMotion& read) noexcept {
    return ViewMotion{.wheel = now.wheel - read.wheel,
                      .orbit = {now.orbit[0] - read.orbit[0], now.orbit[1] - read.orbit[1]},
                      .fly = now.fly - read.fly,
                      .look = {now.look[0] - read.look[0], now.look[1] - read.look[1]}};
}

bool flat(const authoring::SceneView& view) noexcept {
    return view.eye[0] == view.target[0] && view.eye[1] == view.target[1] && view.eye[2] > view.target[2];
}

authoring::SceneView pannedView(const authoring::SceneView& view, double across, double up) noexcept {
    // A pixel a two hundredth of the eye's height, as an orbit's is of a
    // radian: what is under the pointer stays near it.
    const double kPerPixel = (view.eye[2] - view.target[2]) / 200;
    authoring::SceneView panned = view;
    for (const std::size_t kAxis : {0U, 1U}) {
        const double kBy = kAxis == 0 ? -across * kPerPixel : up * kPerPixel;
        panned.eye[kAxis] += kBy;
        panned.target[kAxis] += kBy;
    }
    return panned;
}

authoring::SceneView movedBy(const authoring::SceneView& view, const ViewMotion& change) noexcept {
    // A 2D view stays one: the drag pans it, the wheel brings it nearer
    // the plane or farther (D793).
    if (flat(view)) {
        // Not by orbiting, which holds a view short of straight down.
        authoring::SceneView moved = pannedView(view, change.orbit[0], change.orbit[1]);
        moved.eye[2] = moved.target[2] +
                       std::clamp((view.eye[2] - view.target[2]) * std::pow(1 + (1.0 / 6), change.wheel), 0.5, 1e4);
        return moved;
    }
    return flownView(
        lookedView(movedView(view, change.wheel, change.orbit[0], change.orbit[1]), change.look[0], change.look[1]),
        change.fly);
}

std::vector<authoring::SceneView> othersOf(const authoring::SceneView& view, std::size_t views) {
    if (views <= 1) {
        return {};
    }
    const std::array<double, 3>& kAt = view.target;
    const double kFar = std::max(std::hypot(view.eye[0] - kAt[0], view.eye[1] - kAt[1], view.eye[2] - kAt[2]), 1.0);
    const auto kFrom = [&](double x, double y, double z) {
        return authoring::SceneView{
            .eye = {kAt[0] + x, kAt[1] + y, kAt[2] + z}, .target = kAt, .fieldOfView = view.fieldOfView};
    };
    // Straight down but a hair toward +Z, so the view knows which way is up.
    std::vector<authoring::SceneView> made{kFrom(0, kFar, kFar * 1e-3)};
    if (views >= 3) {
        made.push_back(kFrom(0, 0, kFar));
    }
    return made;
}

document::Value viewValue(const authoring::SceneView& view) {
    const auto kPoint = [](const std::array<double, 3>& at) {
        document::Value made = document::Value::array();
        for (const double kEach : at) {
            made.push(document::Value::real(kEach));
        }
        return made;
    };
    document::Value made = document::Value::object();
    made.add("eye", kPoint(view.eye));
    made.add("target", kPoint(view.target));
    made.add("fieldOfView", document::Value::real(view.fieldOfView));
    return made;
}

} // namespace rawframe::authoring_session
