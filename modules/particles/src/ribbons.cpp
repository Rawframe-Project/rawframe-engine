#include "rawframe/base/color.h"
#include "rawframe/particles/particles.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <set>
#include <vector>

namespace rawframe::particles {

namespace {

/// A ribbon kept for the view: how far it is, its material's place, and
/// its points.
struct Kept {
    float distance = 0;
    std::uint32_t material = 0;
    std::vector<RibbonPoint> points;
};

bool finite(std::initializer_list<float> values) noexcept {
    return std::ranges::all_of(values, [](float value) {
        return std::isfinite(value);
    });
}

bool finite(const std::array<double, 3>& values) noexcept {
    return std::ranges::all_of(values, [](double value) {
        return std::isfinite(value);
    });
}

std::array<float, 4> mixed(const std::array<float, 4>& from, const std::array<float, 4>& to, float through) noexcept {
    return {from[0] + ((to[0] - from[0]) * through),
            from[1] + ((to[1] - from[1]) * through),
            from[2] + ((to[2] - from[2]) * through),
            from[3] + ((to[3] - from[3]) * through)};
}

/// A sphere about every point and its width: where its middle is, and how
/// far it reaches.
std::pair<Vector, float> boundsOf(const std::vector<RibbonPoint>& points) noexcept {
    Vector low = points.front().place;
    Vector high = points.front().place;
    float widest = 0;
    for (const RibbonPoint& kPoint : points) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            low[axis] = std::min(low[axis], kPoint.place[axis]);
            high[axis] = std::max(high[axis], kPoint.place[axis]);
        }
        widest = std::max(widest, kPoint.width);
    }
    const Vector kMiddle = {(low[0] + high[0]) / 2, (low[1] + high[1]) / 2, (low[2] + high[2]) / 2};
    return {kMiddle, (std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) / 2) + (widest / 2)};
}

/// A material's place among the frame's, its first for none it knows.
std::uint32_t placeOf(const std::map<std::uint64_t, std::uint32_t>& materials, std::uint64_t material) noexcept {
    const auto kFound = materials.find(material);
    return kFound != materials.end() ? kFound->second : 0;
}

} // namespace

void Particles::makeRibbons(Frame& frame,
                            std::span<const TrailInstance> trails,
                            std::span<const BeamInstance> beams,
                            const Viewer& viewer,
                            const std::map<std::uint64_t, std::uint32_t>& materials,
                            const Limits& limits) {
    frame.ribbons.clear();
    frame.ribbonPoints.clear();
    frame.ribbonsLeftOut = 0;
    frame.ribbonsHeld = 0;
    std::vector<Kept> kept;
    const auto kRelative = [&viewer](const std::array<double, 3>& position) {
        return Vector{static_cast<float>(position[0] - viewer.eye[0]),
                      static_cast<float>(position[1] - viewer.eye[1]),
                      static_cast<float>(position[2] - viewer.eye[2])};
    };
    const auto kKeep = [&](std::uint32_t material, std::vector<RibbonPoint> points) {
        const auto [kMiddle, kReach] = boundsOf(points);
        if (viewer.sees(kMiddle, kReach)) {
            kept.push_back(Kept{.distance = std::hypot(kMiddle[0], kMiddle[1], kMiddle[2]),
                                .material = material,
                                .points = std::move(points)});
        }
    };
    // Each trail leaves its points and lets go of the dead, seen or not.
    std::set<Key> present;
    for (const TrailInstance& kInstance : trails) {
        const Trail& kTrail = kInstance.ribbon;
        const Key kKey{kInstance.entity, kInstance.component};
        present.insert(kKey);
        if (!finite({kTrail.lifetime, kTrail.spacing, kTrail.widthStart, kTrail.widthEnd}) ||
            !finite(kInstance.position)) {
            ++frame.ribbonsLeftOut;
            continue;
        }
        // Nothing to show: no life, or no width.
        if (kTrail.lifetime <= 0 || (kTrail.widthStart <= 0 && kTrail.widthEnd <= 0)) {
            trails_.erase(kKey);
            continue;
        }
        const bool kHeld = kTrail.lifetime > limits.maximumParticleLifetime;
        const double kLifetime = std::min(kTrail.lifetime, limits.maximumParticleLifetime);
        std::deque<TrailPoint>& history = trails_[kKey];
        std::erase_if(history, [&](const TrailPoint& point) {
            return clock_ - point.left >= kLifetime;
        });
        const std::array<double, 3>& kAt = kInstance.position;
        const double kGone = history.empty() ? 0
                                             : std::hypot(kAt[0] - history.back().position[0],
                                                          kAt[1] - history.back().position[1],
                                                          kAt[2] - history.back().position[2]);
        if (history.empty() || (kGone > 0 && kGone >= std::max(kTrail.spacing, 0.0F))) {
            history.push_back(TrailPoint{.position = kAt, .left = clock_});
        }
        bool held = kHeld;
        while (history.size() > limits.maximumTrailPoints) {
            history.pop_front();
            held = true;
        }
        frame.ribbonsHeld += held ? 1 : 0;
        // From the pose, newest first, through what it holds; a point where
        // the one before it is draws nothing.
        const std::array<float, 4> kStart = base::colorAndAlphaOf(kTrail.colorStart);
        const std::array<float, 4> kEnd = base::colorAndAlphaOf(kTrail.colorEnd);
        const auto kPointOf = [&](const std::array<double, 3>& position, double age) {
            const auto kThrough = static_cast<float>(std::clamp(age / kLifetime, 0.0, 1.0));
            return RibbonPoint{
                .place = kRelative(position),
                .width = std::max(kTrail.widthStart + ((kTrail.widthEnd - kTrail.widthStart) * kThrough), 0.0F),
                .color = mixed(kStart, kEnd, kThrough),
                .along = kThrough};
        };
        std::vector<RibbonPoint> points = {kPointOf(kAt, 0)};
        std::array<double, 3> last = kAt;
        for (auto point = history.rbegin(); point != history.rend(); ++point) {
            if (std::hypot(point->position[0] - last[0], point->position[1] - last[1], point->position[2] - last[2]) <
                1e-3) {
                continue;
            }
            points.push_back(kPointOf(point->position, clock_ - point->left));
            last = point->position;
        }
        if (points.size() >= 2) {
            kKeep(placeOf(materials, kTrail.material), std::move(points));
        }
    }
    std::erase_if(trails_, [&present](const auto& each) {
        return !present.contains(each.first);
    });
    // Each beam along its curve.
    for (const BeamInstance& kInstance : beams) {
        const Beam& kBeam = kInstance.ribbon;
        if (!finite({kBeam.toX,
                     kBeam.toY,
                     kBeam.toZ,
                     kBeam.bendX,
                     kBeam.bendY,
                     kBeam.bendZ,
                     kBeam.widthStart,
                     kBeam.widthEnd,
                     kBeam.textureLength,
                     kBeam.textureSpeed}) ||
            !finite(kInstance.position)) {
            ++frame.ribbonsLeftOut;
            continue;
        }
        // Nothing to show: no length, or no width.
        if ((kBeam.toX == 0 && kBeam.toY == 0 && kBeam.toZ == 0) || (kBeam.widthStart <= 0 && kBeam.widthEnd <= 0)) {
            continue;
        }
        const std::uint32_t kSegments = std::clamp(kBeam.segments, 1U, limits.maximumBeamSegments);
        frame.ribbonsHeld += kBeam.segments > limits.maximumBeamSegments ? 1 : 0;
        // A quadratic curve whose middle is the straight line's bent by
        // `bend`: its control point twice the bend from the line's middle.
        const Vector kFrom = kRelative(kInstance.position);
        const Vector kTo = {kFrom[0] + kBeam.toX, kFrom[1] + kBeam.toY, kFrom[2] + kBeam.toZ};
        const Vector kControl = {((kFrom[0] + kTo[0]) / 2) + (2 * kBeam.bendX),
                                 ((kFrom[1] + kTo[1]) / 2) + (2 * kBeam.bendY),
                                 ((kFrom[2] + kTo[2]) / 2) + (2 * kBeam.bendZ)};
        // Its texture scrolled toward the far end: the repeats gone by.
        const auto kScrolled = static_cast<float>(std::fmod(double{kBeam.textureSpeed} * clock_, 1.0));
        const std::array<float, 4> kStart = base::colorAndAlphaOf(kBeam.colorStart);
        const std::array<float, 4> kEnd = base::colorAndAlphaOf(kBeam.colorEnd);
        std::vector<RibbonPoint> points;
        points.reserve(kSegments + 1);
        float length = 0;
        for (std::uint32_t at = 0; at <= kSegments; ++at) {
            const float kT = static_cast<float>(at) / static_cast<float>(kSegments);
            const float kA = (1 - kT) * (1 - kT);
            const float kB = 2 * kT * (1 - kT);
            const float kC = kT * kT;
            const Vector kPlace = {(kA * kFrom[0]) + (kB * kControl[0]) + (kC * kTo[0]),
                                   (kA * kFrom[1]) + (kB * kControl[1]) + (kC * kTo[1]),
                                   (kA * kFrom[2]) + (kB * kControl[2]) + (kC * kTo[2])};
            if (!points.empty()) {
                const Vector& kBefore = points.back().place;
                length += std::hypot(kPlace[0] - kBefore[0], kPlace[1] - kBefore[1], kPlace[2] - kBefore[2]);
            }
            points.push_back(
                RibbonPoint{.place = kPlace,
                            .width = std::max(kBeam.widthStart + ((kBeam.widthEnd - kBeam.widthStart) * kT), 0.0F),
                            .color = mixed(kStart, kEnd, kT),
                            .along = (kBeam.textureLength > 0 ? length / kBeam.textureLength : kT) - kScrolled});
        }
        kKeep(placeOf(materials, kBeam.material), std::move(points));
    }
    // The nearest up to the limit, drawn farthest first.
    std::ranges::stable_sort(kept, {}, &Kept::distance);
    if (kept.size() > limits.maximumRibbons) {
        frame.ribbonsLeftOut += kept.size() - limits.maximumRibbons;
        kept.resize(limits.maximumRibbons);
    }
    std::ranges::reverse(kept);
    for (Kept& ribbon : kept) {
        frame.ribbons.push_back(Ribbon{.material = ribbon.material,
                                       .first = static_cast<std::uint32_t>(frame.ribbonPoints.size()),
                                       .count = static_cast<std::uint32_t>(ribbon.points.size())});
        frame.ribbonPoints.insert(frame.ribbonPoints.end(), ribbon.points.begin(), ribbon.points.end());
    }
}

} // namespace rawframe::particles
