#include "handles.h"

#include "rawframe/view/preview.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawframe::authoring_session {

namespace {

double dot(const Point& a, const Point& b) noexcept {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

/// The ray's and the axis line's parameters at their nearest points: along
/// the ray (a share of `toward`) and along the line (meters); none for
/// lines that run together.
struct Nearest {
    double ray = 0;
    double line = 0;
};

std::optional<Nearest>
nearestOf(const Point& mark, std::size_t axis, const Point& origin, const Point& toward) noexcept {
    const Point kFrom = {origin[0] - mark[0], origin[1] - mark[1], origin[2] - mark[2]};
    const double kLength = dot(toward, toward);
    const double kAcross = toward[axis];
    const double kRayFrom = dot(toward, kFrom);
    const double kLineFrom = kFrom[axis];
    const double kApart = kLength - (kAcross * kAcross);
    if (kLength <= 0 || kApart <= 1e-12 * kLength) {
        return std::nullopt;
    }
    return Nearest{.ray = ((kAcross * kLineFrom) - kRayFrom) / kApart,
                   .line = ((kLength * kLineFrom) - (kAcross * kRayFrom)) / kApart};
}

} // namespace

std::optional<Grabbed>
grabbedAxis(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept {
    const double kLength = dot(toward, toward);
    if (!(kLength > 0) || !(fieldOfView > 0) || !(fieldOfView < 180)) {
        return std::nullopt;
    }
    const double kShare = 2 * std::tan(fieldOfView * std::numbers::pi / 360) / 40;
    std::optional<Grabbed> grabbed;
    double nearest = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const std::optional<Nearest> kNearest = nearestOf(mark, axis, origin, toward);
        if (!kNearest.has_value()) {
            continue;
        }
        // The axis's point nearest the ray, held to its outer three
        // quarters, and the ray's point nearest that, ahead of the eye.
        const double kAlong = std::clamp(kNearest->line, 0.25, 1.0);
        Point on = mark;
        on[axis] += kAlong;
        const Point kTo = {on[0] - origin[0], on[1] - origin[1], on[2] - origin[2]};
        const double kRay = std::min(dot(kTo, toward) / kLength, 1.0);
        if (kRay <= 0) {
            continue;
        }
        const Point kGap = {origin[0] + (kRay * toward[0]) - on[0],
                            origin[1] + (kRay * toward[1]) - on[1],
                            origin[2] + (kRay * toward[2]) - on[2]};
        // How near, as a share of what the view's height spans there.
        const double kNear = std::sqrt(dot(kGap, kGap)) / (kRay * std::sqrt(kLength) * kShare);
        if (kNear <= nearest) {
            nearest = kNear;
            grabbed = Grabbed{.axis = axis, .along = kAlong};
        }
    }
    return grabbed;
}

std::optional<Point> levelPoint(const Point& mark, const Point& origin, const Point& toward) noexcept {
    if (std::abs(toward[1]) <= 1e-12) {
        return std::nullopt;
    }
    const double kAlong = (mark[1] - origin[1]) / toward[1];
    if (!(kAlong > 0) || kAlong > 1) {
        return std::nullopt;
    }
    return Point{origin[0] + (kAlong * toward[0]), mark[1], origin[2] + (kAlong * toward[2])};
}

bool grabbedRing(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept {
    const std::optional<Point> kMet = levelPoint(mark, origin, toward);
    if (!kMet.has_value() || !(fieldOfView > 0) || !(fieldOfView < 180)) {
        return false;
    }
    const Point kAhead = {(*kMet)[0] - origin[0], (*kMet)[1] - origin[1], (*kMet)[2] - origin[2]};
    const double kTolerance = std::sqrt(dot(kAhead, kAhead)) * 2 * std::tan(fieldOfView * std::numbers::pi / 360) / 40;
    const double kOut = std::hypot((*kMet)[0] - mark[0], (*kMet)[2] - mark[2]);
    return std::abs(kOut - view::kMarkRingRadius) <= kTolerance;
}

std::optional<double>
alongAxis(const Point& mark, std::size_t axis, const Point& origin, const Point& toward) noexcept {
    const std::optional<Nearest> kNearest = axis < 3 ? nearestOf(mark, axis, origin, toward) : std::nullopt;
    if (!kNearest.has_value() || !std::isfinite(kNearest->line) || std::abs(kNearest->line) > 1e4) {
        return std::nullopt;
    }
    return kNearest->line;
}

std::uint8_t partUnder(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept {
    if (const std::optional<Grabbed> kAxis = grabbedAxis(mark, origin, toward, fieldOfView); kAxis.has_value()) {
        return static_cast<std::uint8_t>(kAxis->axis + 1);
    }
    return grabbedRing(mark, origin, toward, fieldOfView) ? 4 : 0;
}

std::optional<Handle>
grabbedHandle(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept {
    if (const std::optional<Grabbed> kAxis = grabbedAxis(mark, origin, toward, fieldOfView); kAxis.has_value()) {
        Point at = mark;
        at[kAxis->axis] += kAxis->along;
        return Handle{.axis = kAxis->axis, .point = at};
    }
    const std::optional<Point> kMet = levelPoint(mark, origin, toward);
    if (!kMet.has_value() || !grabbedRing(mark, origin, toward, fieldOfView)) {
        return std::nullopt;
    }
    return Handle{.point = *kMet};
}

namespace {

document::Value pointValue(const Point& point) {
    document::Value numbers = document::Value::array();
    for (const double kEach : point) {
        numbers.push(document::Value::real(kEach));
    }
    return numbers;
}

} // namespace

document::Value markRecord(const std::optional<Point>& at, std::uint8_t lit) {
    constexpr std::array<std::string_view, 5> kParts = {"", "x", "y", "z", "ring"};
    document::Value mark = document::Value::object();
    mark.add("kind", document::Value::string("tooling.mark"));
    mark.add("id", document::Value::integer(0));
    mark.add("at", at.has_value() ? pointValue(*at) : document::Value{});
    if (lit != 0 && lit < kParts.size()) {
        mark.add("lit", document::Value::string(std::string{kParts[lit]}));
    }
    return mark;
}

document::Value movedRecord(const std::string& scene,
                            const std::string& source,
                            const Point& mark,
                            const Handle& grabbed,
                            const Point& origin,
                            const Point& toward) {
    document::Value moved = document::Value::object();
    moved.add("scene", document::Value::string(scene));
    moved.add("source", document::Value::string(source));
    if (grabbed.axis.has_value()) {
        const std::size_t kAxis = *grabbed.axis;
        const std::optional<double> kTo = alongAxis(mark, kAxis, origin, toward);
        if (!kTo.has_value()) {
            return document::Value{};
        }
        Point by{};
        by[kAxis] = *kTo - (grabbed.point[kAxis] - mark[kAxis]);
        moved.add("how", document::Value::string(kAxis == 1 ? "height" : "move"));
        moved.add("by", pointValue(by));
        return moved;
    }
    const std::optional<Point> kTo = levelPoint(mark, origin, toward);
    if (!kTo.has_value()) {
        return document::Value{};
    }
    const Point& kFrom = grabbed.point;
    moved.add("how", document::Value::string("turn"));
    moved.add("by", pointValue(Point{(*kTo)[0] - kFrom[0], 0, (*kTo)[2] - kFrom[2]}));
    moved.add("from", pointValue(kFrom));
    moved.add("to", pointValue(*kTo));
    return moved;
}

} // namespace rawframe::authoring_session
