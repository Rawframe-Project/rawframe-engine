#include "rawframe/spline/spline.h"

#include <algorithm>
#include <cmath>

namespace rawframe::spline {

namespace {

Point minus(const Point& left, const Point& right) noexcept {
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

double lengthOf(const Point& vector) noexcept {
    return std::sqrt((vector[0] * vector[0]) + (vector[1] * vector[1]) + (vector[2] * vector[2]));
}

/// `left` and `right` blended: nought is `left`, one `right`.
Point blend(const Point& left, const Point& right, double toward) noexcept {
    return {left[0] + ((right[0] - left[0]) * toward),
            left[1] + ((right[1] - left[1]) * toward),
            left[2] + ((right[2] - left[2]) * toward)};
}

/// The control point `index` of a span: the spline's own, wrapped where it
/// is closed, and past an open end the end's mirror of its neighbor.
Point controlOf(const Spline& spline, std::ptrdiff_t index) noexcept {
    const auto kCount = static_cast<std::ptrdiff_t>(spline.points.size());
    if (spline.closed) {
        return spline.points[static_cast<std::size_t>(((index % kCount) + kCount) % kCount)];
    }
    if (index < 0) {
        return blend(spline.points[0], spline.points[1], -1);
    }
    if (index >= kCount) {
        return blend(spline.points[static_cast<std::size_t>(kCount - 1)],
                     spline.points[static_cast<std::size_t>(kCount - 2)],
                     -1);
    }
    return spline.points[static_cast<std::size_t>(index)];
}

} // namespace

std::size_t spans(const Spline& spline) noexcept {
    if (spline.points.size() < (spline.closed ? 3U : 2U)) {
        return 0;
    }
    return spline.closed ? spline.points.size() : spline.points.size() - 1;
}

Point at(const Spline& spline, double along) noexcept {
    const std::size_t kSpans = spans(spline);
    if (kSpans == 0) {
        return spline.points.empty() ? Point{} : spline.points[0];
    }
    const double kHeld = std::clamp(along, 0.0, static_cast<double>(kSpans));
    const auto kSpan = static_cast<std::ptrdiff_t>(std::min(std::floor(kHeld), static_cast<double>(kSpans - 1)));
    const double kU = kHeld - static_cast<double>(kSpan);
    const Point kP0 = controlOf(spline, kSpan - 1);
    const Point kP1 = controlOf(spline, kSpan);
    const Point kP2 = controlOf(spline, kSpan + 1);
    const Point kP3 = controlOf(spline, kSpan + 2);
    // Centripetal knots, each step the square root of the distance, kept
    // from nought so points that meet still blend.
    const auto kStep = [](const Point& from, const Point& to) {
        return std::max(std::sqrt(lengthOf(minus(to, from))), 1e-9);
    };
    const double kT0 = 0;
    const double kT1 = kT0 + kStep(kP0, kP1);
    const double kT2 = kT1 + kStep(kP1, kP2);
    const double kT3 = kT2 + kStep(kP2, kP3);
    const double kT = kT1 + (kU * (kT2 - kT1));
    // Barry and Goldman's pyramid.
    const Point kA1 = blend(kP0, kP1, (kT - kT0) / (kT1 - kT0));
    const Point kA2 = blend(kP1, kP2, (kT - kT1) / (kT2 - kT1));
    const Point kA3 = blend(kP2, kP3, (kT - kT2) / (kT3 - kT2));
    const Point kB1 = blend(kA1, kA2, (kT - kT0) / (kT2 - kT0));
    const Point kB2 = blend(kA2, kA3, (kT - kT1) / (kT3 - kT1));
    return blend(kB1, kB2, (kT - kT1) / (kT2 - kT1));
}

Walk walk(const Spline& spline) {
    Walk made;
    const std::size_t kSpans = spans(spline);
    const std::uint32_t kPieces = kPiecesEach;
    for (std::size_t span = 0; span < kSpans; ++span) {
        for (std::uint32_t piece = 0; piece < kPieces; ++piece) {
            made.points.push_back(
                at(spline, static_cast<double>(span) + (static_cast<double>(piece) / static_cast<double>(kPieces))));
        }
    }
    made.points.push_back(at(spline, static_cast<double>(kSpans)));
    double distance = 0;
    for (std::size_t each = 0; each < made.points.size(); ++each) {
        distance += each == 0 ? 0 : lengthOf(minus(made.points[each], made.points[each - 1]));
        made.distances.push_back(distance);
    }
    return made;
}

Along along(const Walk& walk, double meters, bool closed) noexcept {
    if (walk.points.empty()) {
        return {};
    }
    const double kLength = walk.length();
    double held = std::isfinite(meters) ? meters : 0;
    if (closed && kLength > 0) {
        held = std::fmod(held, kLength);
        held = held < 0 ? held + kLength : held;
    }
    held = std::clamp(held, 0.0, kLength);
    // The piece it falls on: the last whose start is not past it.
    const auto kAfter = std::ranges::upper_bound(walk.distances, held);
    std::size_t piece = static_cast<std::size_t>(std::max<std::ptrdiff_t>(kAfter - walk.distances.begin() - 1, 0));
    piece = std::min(piece, walk.points.size() >= 2 ? walk.points.size() - 2 : 0);
    if (walk.points.size() < 2) {
        return Along{.point = walk.points[0], .heading = {1, 0, 0}};
    }
    const double kSpan = walk.distances[piece + 1] - walk.distances[piece];
    const double kToward = kSpan > 0 ? (held - walk.distances[piece]) / kSpan : 0;
    Along made{.point = blend(walk.points[piece], walk.points[piece + 1], kToward), .heading = {1, 0, 0}};
    // The way of the nearest piece that has a length.
    for (std::size_t offset = 0; offset < walk.points.size() - 1; ++offset) {
        for (const std::size_t kTry : {piece + offset, piece >= offset ? piece - offset : piece}) {
            if (kTry + 1 >= walk.points.size()) {
                continue;
            }
            const Point kWay = minus(walk.points[kTry + 1], walk.points[kTry]);
            const double kWayLength = lengthOf(kWay);
            if (kWayLength > 0) {
                made.heading = {kWay[0] / kWayLength, kWay[1] / kWayLength, kWay[2] / kWayLength};
                return made;
            }
        }
    }
    return made;
}

} // namespace rawframe::spline
